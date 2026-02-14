// Crumb_D: ESP-NOW relay → main network (HTTP POST to dashboard API)
// When a message is received via ESP-NOW, relay it by calling the API as in https.ino.

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_now.h>
#include <esp_wifi.h>

// Copy secrets.h from hardware/https/ (WIFI_SSID, WIFI_PASSWORD)
#include "secrets.h"

#define LED_PIN 2   // Built-in LED on many ESP32 boards; change if using external LED

// Must match Crumb_C and your WiFi AP's channel (router/hotspot). Many APs use 6 or 11; try 6 first.
#define ESP_NOW_CHANNEL 6

// Fixed layout: parse by offset (must match sender order). +4 hop_count, +4 delay_ms
#define MSG_ID_LEN   24
#define CRUMB_ID_LEN 8
#define TYPE_LEN     8
#define MESSAGE_LEN  64
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

// Same contract as https.ino: POST JSON to serverURL
void sendMessageToAPI(const char* id, const char* crumb_id, const char* type,
                      const char* message, int hop_count, uint32_t delay_ms) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, skip relay.");
    return;
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
    "{\"id\":\"" + String(id) + "\","
    "\"crumb_id\":\"" + String(crumb_id) + "\","
    "\"type\":\"" + String(type) + "\","
    "\"message\":\"" + msgEscaped + "\","
    "\"hop_count\":" + String(hop_count) + ","
    "\"delay_ms\":" + String((unsigned long)delay_ms) + "}";

  int code = https.POST(json);
  Serial.print("Relay response code: ");
  Serial.println(code);
  Serial.println(https.getString());
  https.end();
}

void OnDataRecv(const uint8_t* mac, const uint8_t* incomingData, int len) {
  if (len != CRUMB_PAYLOAD_LEN) return;

  int nextHead = (pendingHead + 1) % PENDING_QUEUE_LEN;
  if (nextHead == pendingTail) return;  // queue full, drop this packet

  struct pending* m = &pendingQueue[pendingHead];
  const uint8_t* p = incomingData;
  memcpy(m->message_id, p, MSG_ID_LEN);
  m->message_id[MSG_ID_LEN] = '\0';
  p += MSG_ID_LEN;
  memcpy(m->crumb_id, p, CRUMB_ID_LEN);
  m->crumb_id[CRUMB_ID_LEN] = '\0';
  p += CRUMB_ID_LEN;
  memcpy(m->type, p, TYPE_LEN);
  m->type[TYPE_LEN] = '\0';
  p += TYPE_LEN;
  memcpy(m->message, p, MESSAGE_LEN);
  m->message[MESSAGE_LEN] = '\0';
  p += MESSAGE_LEN;
  memcpy(&m->hop_count, p, 4);
  p += 4;
  memcpy(&m->delay_ms, p, 4);

  pendingHead = nextHead;
  digitalWrite(LED_PIN, HIGH);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
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

    if (m->delay_ms > 0) {
      Serial.print("Delay ");
      Serial.print((unsigned long)m->delay_ms);
      Serial.println(" ms before relay");
      delay(m->delay_ms);
    }

    sendMessageToAPI(m->message_id, m->crumb_id, m->type, m->message, m->hop_count, m->delay_ms);

    delay(200);  // LED on time
    digitalWrite(LED_PIN, LOW);
  }
  delay(10);
}
