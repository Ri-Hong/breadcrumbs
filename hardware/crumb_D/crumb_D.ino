// Crumb_D: ESP-NOW relay → main network (HTTP POST to dashboard API)
// When a message is received via ESP-NOW, relay it by calling the API as in https.ino.

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

// Copy secrets.h from hardware/https/ (WIFI_SSID, WIFI_PASSWORD) from hardware/https/ (WIFI_SSID, WIFI_PASSWORD)
#include "secrets.h"

// Bread MAC (from hardware/MACs.md) — for beacon so Bread can track this crumb
uint8_t bread_Mac[] = {0xE4, 0x65, 0xB8, 0x83, 0x56, 0x30};

#define LED_PIN 32             // External LED
#define BUZZER_PIN 5           // Active buzzer for ripple; set to -1 if no buzzer
#define MESSAGE_DELAY_MS 1000  // Delay before relaying a standard MSG to API so the wave is visible along the trail

// Must match Crumb_C and your WiFi AP's channel (router/hotspot). Many APs use 6 or 11; try 6 first.
#define ESP_NOW_CHANNEL 6
#define BEACON_INTERVAL_MS 400  // Send beacon to Bread for RSSI / drop order

// Fixed layout: parse by offset (must match sender order). +4 hop_count, +4 delay_ms
#define MSG_ID_LEN 24
#define CRUMB_ID_LEN 8
#define TYPE_LEN 8
#define MESSAGE_LEN 64
#define CRUMB_PAYLOAD_LEN (MSG_ID_LEN + CRUMB_ID_LEN + TYPE_LEN + MESSAGE_LEN + 4 + 4)  // hop_count, delay_ms

const char* serverURL = "https://breadcrumbs-phi.vercel.app/api/message";

// Queue so we don't drop messages when they arrive faster than we can relay (callback has small stack)
#define PENDING_QUEUE_LEN 8
struct pending {
    char message_id[MSG_ID_LEN + 1];
    char crumb_id[CRUMB_ID_LEN + 1];
    char type[TYPE_LEN + 1];
    char message[MESSAGE_LEN + 1];
    int hop_count;
    uint32_t delay_ms;
};
static struct pending pendingQueue[PENDING_QUEUE_LEN];
static volatile int pendingHead = 0;  // callback writes here
static volatile int pendingTail = 0;  // loop reads here; head==tail => empty

// Dedupe: ignore same message_id from C's retries
static char lastQueuedMsgId[MSG_ID_LEN + 1] = {0};
static unsigned long lastBeaconMs = 0;

esp_now_peer_info_t peerInfo;

// Sanity limits so garbage packets don't hang or crash
#define MAX_DELAY_MS 60000  // cap delay at 60 s
#define MAX_HOP_COUNT 16
#define MIN_HOP_COUNT 0

// Connect at boot so we don't block the ESP-NOW callback for 20s on first message
static bool connectWiFiAtBoot() {
    Serial.println("Connecting to WiFi...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    const unsigned long timeoutMs = 20000;
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        if (millis() - start > timeoutMs) {
            Serial.println("\nWiFi timeout. Check SSID, password, and that the network is on.");
            return false;
        }
    }
    Serial.println("\nWiFi connected.");
    return true;
}

// Try to reconnect WiFi once (non-blocking would need state machine; short timeout here)
static bool tryReconnectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return true;
    Serial.println("WiFi down, attempting reconnect...");
    WiFi.disconnect();
    delay(200);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    const unsigned long timeoutMs = 15000;
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        if (millis() - start > timeoutMs) {
            Serial.println("Reconnect timeout.");
            return false;
        }
    }
    Serial.println("WiFi reconnected.");
    return true;
}

// Same contract as https.ino: POST JSON to serverURL
void sendMessageToAPI(const char* id, const char* crumb_id, const char* type,
                      const char* message, int hop_count, uint32_t delay_ms) {
    if (WiFi.status() != WL_CONNECTED) {
        if (!tryReconnectWiFi()) {
            Serial.println("WiFi not connected, skip relay.");
            return;
        }
    }

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient https;
    if (!https.begin(client, serverURL)) {
        Serial.println("HTTPS begin failed");
        return;
    }

    https.addHeader("Content-Type", "application/json");

    // Escape quotes in message for JSON (simple: replace " with \")
    String msgEscaped = String(message);
    msgEscaped.replace("\"", "\\\"");

    String json =
        "{\"id\":\"" + String(id) +
        "\","
        "\"crumb_id\":\"" +
        String(crumb_id) +
        "\","
        "\"type\":\"" +
        String(type) +
        "\","
        "\"message\":\"" +
        msgEscaped +
        "\","
        "\"hop_count\":" +
        String(hop_count) +
        ","
        "\"delay_ms\":" +
        String((unsigned long)delay_ms) + "}";

    int code = https.POST(json);
    Serial.print("Relay response code: ");
    Serial.println(code);
    Serial.println(https.getString());
    https.end();
}

// Return true if payload looks like a real crumb message (reject garbage/corruption)
static bool validatePayload(const char* msg_id, const char* crumb_id, int hop_count, uint32_t delay_ms) {
    if (hop_count < MIN_HOP_COUNT || hop_count > MAX_HOP_COUNT) return false;
    if (delay_ms > MAX_DELAY_MS) return false;  // reject insane delay
    for (int i = 0; i < MSG_ID_LEN && msg_id[i]; i++) {
        char c = msg_id[i];
        if (c != '-' && c != '_' && (c < '0' || c > '9') && (c < 'A' || c > 'Z') && (c < 'a' || c > 'z'))
            return false;
    }
    for (int i = 0; i < CRUMB_ID_LEN && crumb_id[i]; i++) {
        char c = crumb_id[i];
        if ((c < '0' || c > '9') && (c < 'A' || c > 'Z') && (c < 'a' || c > 'z')) return false;
    }
    return true;
}

void pulseBuzzer() {
#if BUZZER_PIN >= 0
    for (int i = 0; i < 2; i++) {
        digitalWrite(BUZZER_PIN, HIGH);
        delay(80);
        digitalWrite(BUZZER_PIN, LOW);
        delay(60);
    }
#endif
}

void OnDataRecv(const esp_now_recv_info_t* info, const uint8_t* incomingData, int len) {
    (void)info;
    if (len != CRUMB_PAYLOAD_LEN) return;

    struct pending tmp;
    const uint8_t* p = incomingData;
    memcpy(tmp.message_id, p, MSG_ID_LEN);
    tmp.message_id[MSG_ID_LEN] = '\0';
    p += MSG_ID_LEN;
    memcpy(tmp.crumb_id, p, CRUMB_ID_LEN);
    tmp.crumb_id[CRUMB_ID_LEN] = '\0';
    p += CRUMB_ID_LEN;
    memcpy(tmp.type, p, TYPE_LEN);
    tmp.type[TYPE_LEN] = '\0';
    p += TYPE_LEN;
    memcpy(tmp.message, p, MESSAGE_LEN);
    tmp.message[MESSAGE_LEN] = '\0';
    p += MESSAGE_LEN;
    memcpy(&tmp.hop_count, p, 4);
    p += 4;
    memcpy(&tmp.delay_ms, p, 4);

    if (!validatePayload(tmp.message_id, tmp.crumb_id, tmp.hop_count, tmp.delay_ms)) {
        Serial.println("Dropping invalid ESP-NOW packet (garbage/corrupt).");
        return;
    }
    if (strcmp(tmp.message_id, lastQueuedMsgId) == 0) {
        return;  // duplicate from C's retries, don't queue again
    }

    int nextHead = (pendingHead + 1) % PENDING_QUEUE_LEN;
    if (nextHead == pendingTail) return;  // queue full, drop this packet

    struct pending* m = &pendingQueue[pendingHead];
    memcpy(m, &tmp, sizeof(struct pending));
    strncpy(lastQueuedMsgId, tmp.message_id, MSG_ID_LEN);
    lastQueuedMsgId[MSG_ID_LEN] = '\0';

    pendingHead = nextHead;
    digitalWrite(LED_PIN, HIGH);
    digitalWrite(LED_BUILTIN, HIGH);
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    digitalWrite(LED_BUILTIN, LOW);
#if BUZZER_PIN >= 0
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
#endif
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    // Fix channel so ESP-NOW and WiFi STA stay on same channel (AP must use this channel too)
    esp_wifi_set_channel(ESP_NOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

    // Init ESP-NOW before connecting to WiFi
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }
    esp_now_register_recv_cb(OnDataRecv);

    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, bread_Mac, 6);
    peerInfo.channel = ESP_NOW_CHANNEL;
    peerInfo.encrypt = false;
    peerInfo.ifidx = WIFI_IF_STA;
    if (esp_now_add_peer(&peerInfo) == ESP_OK)
        Serial.println("Peer Bread added (beacon)");

    Serial.println("Crumb_D: ESP-NOW listening.");

    if (!connectWiFiAtBoot()) {
        Serial.println("Will not relay until WiFi is available. Restart or fix credentials.");
    }
    Serial.println("Crumb_D: ready (relay to API on receive).");
    Serial.println("(WiFi AP must be on channel " + String(ESP_NOW_CHANNEL) + " for ESP-NOW to keep working.)");
}

void loop() {
    if (pendingTail != pendingHead) {
        struct pending* m = &pendingQueue[pendingTail];
        pendingTail = (pendingTail + 1) % PENDING_QUEUE_LEN;

        Serial.print("ESP-NOW received: id=");
        Serial.print(m->message_id);
        Serial.print(" crumb_id=");
        Serial.print(m->crumb_id);
        Serial.print(" message=");
        Serial.println(m->message);

        uint32_t delayMs = m->delay_ms;
        if (delayMs > MAX_DELAY_MS) delayMs = MAX_DELAY_MS;
        if (delayMs > 0) {
            Serial.print("Delay ");
            Serial.print((unsigned long)delayMs);
            Serial.println(" ms before relay");
            delay(delayMs);
        }

        if (strcmp(m->type, "MSG") == 0) {
            delay(MESSAGE_DELAY_MS);
        }

        sendMessageToAPI(m->message_id, m->crumb_id, m->type, m->message, m->hop_count, m->delay_ms);

        if (strcmp(m->type, "RIPPLE") == 0) {
            pulseBuzzer();
        }
        delay(200);  // LED on time
        digitalWrite(LED_PIN, LOW);
        digitalWrite(LED_BUILTIN, LOW);
    }

    if ((unsigned long)(millis() - lastBeaconMs) >= BEACON_INTERVAL_MS) {
        lastBeaconMs = millis();
        uint8_t beacon[2] = {0x02, 'D'};
        esp_now_send(bread_Mac, beacon, 2);
    }
    delay(10);
}
