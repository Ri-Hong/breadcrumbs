// Crumb_D: ESP-NOW relay → main network (HTTP POST to dashboard API)
// When a message is received via ESP-NOW, relay it by calling the API as in https.ino.

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_now.h>

// Copy secrets.h from hardware/https/ (WIFI_SSID, WIFI_PASSWORD)
#include "secrets.h"

#define LED_PIN 2   // Built-in LED on many ESP32 boards; change if using external LED

// Message structure: must match Crumb_C (and any other ESP-NOW senders)
typedef struct struct_crumb_message {
  char message_id[24];
  char crumb_id[8];
  char type[8];
  char message[64];
  int hop_count;
} struct_crumb_message;

const char* serverURL = "https://breadcrumbs-phi.vercel.app/api/message";

static bool wifiConnected = false;

void ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    return;
  }
  if (!wifiConnected) {
    Serial.println("Connecting to WiFi...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
  const unsigned long timeoutMs = 20000;
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - start > timeoutMs) {
      Serial.println("\nWiFi timeout. Cannot relay.");
      return;
    }
  }
  Serial.println("\nWiFi connected.");
  wifiConnected = true;
}

// Same contract as https.ino: POST JSON to serverURL
void sendMessageToAPI(const char* id, const char* crumb_id, const char* type,
                      const char* message, int hop_count) {
  ensureWiFi();
  if (WiFi.status() != WL_CONNECTED) return;

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
    "\"hop_count\":" + String(hop_count) + "}";

  int code = https.POST(json);
  Serial.print("Relay response code: ");
  Serial.println(code);
  Serial.println(https.getString());
  https.end();
}

void OnDataRecv(const uint8_t* mac, const uint8_t* incomingData, int len) {
//  if (len < (int)sizeof(struct_crumb_message)) {
//    Serial.println("Received packet too short, ignoring");
//    return;
//  }

  struct_crumb_message msg;
  memcpy(&msg, incomingData, sizeof(msg));

  // Null-terminate strings in case sender didn't
  msg.message_id[sizeof(msg.message_id) - 1] = '\0';
  msg.crumb_id[sizeof(msg.crumb_id) - 1] = '\0';
  msg.type[sizeof(msg.type) - 1] = '\0';
  msg.message[sizeof(msg.message) - 1] = '\0';

  Serial.print("ESP-NOW received from ");
  for (int i = 0; i < 6; i++) {
    if (mac[i] < 0x10) Serial.print("0");
    Serial.print(mac[i], HEX);
    if (i < 5) Serial.print(":");
  }
  Serial.println();
  Serial.print("  id=");
  Serial.println(msg.message_id);
  Serial.print("  crumb_id=");
  Serial.println(msg.crumb_id);
  Serial.print("  message=");
  Serial.println(msg.message);

  digitalWrite(LED_PIN, HIGH);
  delay(200);
  digitalWrite(LED_PIN, LOW);

  sendMessageToAPI(msg.message_id, msg.crumb_id, msg.type, msg.message, msg.hop_count);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);
  Serial.println("Crumb_D: listening for ESP-NOW messages (relay to API on receive).");
}

void loop() {
  delay(100);
}
