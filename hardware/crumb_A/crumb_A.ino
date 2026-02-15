// Crumb: no Hall sensor. RELEASE from Bread → LED on. Bread says "in pouch" (RSSI > -15) → LED off.
// Always sends beacons so Bread can measure RSSI (drop when < -60, in pouch when > -15).

#include <esp_now.h>
#include <WiFi.h>

#define LED_PIN 14

// Peer: Bread (pouch). MACs from hardware/MACs.md — Bread: E4:65:B8:83:56:30; A: 24:0A:C4:AF:63:A4
const uint8_t bread_mac[6] = {0xE4, 0x65, 0xB8, 0x83, 0x56, 0x30};  // Bread
#define CRUMB_ID 'A'   // this firmware runs on the board with Crumb A MAC (24:0A:C4:AF:63:A4)

#define MSG_RELEASE  0x01
#define MSG_IN_POUCH 0x03
#define MSG_BEACON   0x02

typedef struct __attribute__((packed)) {
  uint8_t type;
  uint8_t crumb_id;
} crumb_message_t;

bool dropped_led_on = false;
unsigned long last_beacon_ms = 0;
#define BEACON_INTERVAL_MS 400

void sendToBread(uint8_t type) {
  crumb_message_t msg;
  msg.type = type;
  msg.crumb_id = CRUMB_ID;
  esp_now_send(bread_mac, (uint8_t *)&msg, sizeof(msg));
}

void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  (void)info;
  if (len >= (int)sizeof(crumb_message_t)) {
    const crumb_message_t *msg = (const crumb_message_t *)data;
    if (msg->type == MSG_RELEASE && (msg->crumb_id == CRUMB_ID || msg->crumb_id == 0)) {
      dropped_led_on = true;
      digitalWrite(LED_PIN, HIGH);
      Serial.println("RELEASE — LED on (drop me)");
    } else if (msg->type == MSG_IN_POUCH && (msg->crumb_id == CRUMB_ID || msg->crumb_id == 0)) {
      dropped_led_on = false;
      digitalWrite(LED_PIN, LOW);
      Serial.println("In pouch (Bread RSSI > -15) — LED off");
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  WiFi.mode(WIFI_STA);
  delay(100);
  WiFi.setChannel(1);
  delay(100);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }
  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, bread_mac, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
  esp_now_register_recv_cb(OnDataRecv);
}

void loop() {
  digitalWrite(LED_PIN, dropped_led_on ? HIGH : LOW);

  if ((unsigned long)(millis() - last_beacon_ms) >= BEACON_INTERVAL_MS) {
    last_beacon_ms = millis();
    sendToBread(MSG_BEACON);
  }
}
