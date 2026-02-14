// Crumb_A: Origin sender — sends one ESP-NOW message to Crumb_B every 10 seconds.
// Chain: A -> B -> C -> D (D relays to API).

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <string.h>

// Crumb_B MAC (from hardware/MACs.md)
uint8_t crumbB_Mac[] = {0x24, 0x0A, 0xC4, 0xAE, 0x97, 0xA8};

#define LED_PIN 2

#define ESP_NOW_CHANNEL 6

#define MSG_ID_LEN   24
#define CRUMB_ID_LEN 8
#define TYPE_LEN     8
#define MESSAGE_LEN  64
#define CRUMB_PAYLOAD_LEN (MSG_ID_LEN + CRUMB_ID_LEN + TYPE_LEN + MESSAGE_LEN + 4 + 4)

#define SEND_DELAY_MS 1000

uint8_t outgoingBuf[CRUMB_PAYLOAD_LEN];
esp_now_peer_info_t peerInfo;

static uint32_t messageCounter = 0;

void OnDataSent(const uint8_t* mac_addr, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("Send OK");
  } else {
    Serial.println("Send FAIL (set ESP_NOW_CHANNEL to your WiFi AP channel)");
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  esp_wifi_set_channel(ESP_NOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  esp_now_register_send_cb(OnDataSent);

  memcpy(peerInfo.peer_addr, crumbB_Mac, 6);
  peerInfo.channel = ESP_NOW_CHANNEL;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer (Crumb_B)");
    return;
  }

  Serial.print("Crumb_A: sending ");
  Serial.print(CRUMB_PAYLOAD_LEN);
  Serial.println(" bytes to Crumb_B every 10s");
}

void loop() {
  messageCounter++;

  char msgId[MSG_ID_LEN];
  char cid[CRUMB_ID_LEN];
  char typ[TYPE_LEN];
  char msg[MESSAGE_LEN];
  snprintf(msgId, sizeof(msgId), "A1-%lu", (unsigned long)messageCounter);
  snprintf(cid, sizeof(cid), "A1");
  snprintf(typ, sizeof(typ), "MSG");
  snprintf(msg, sizeof(msg), "Test from Crumb_A #%lu", (unsigned long)messageCounter);

  memset(outgoingBuf, 0, CRUMB_PAYLOAD_LEN);
  size_t n;
  n = strlen(msgId) + 1; if (n > MSG_ID_LEN) n = MSG_ID_LEN; memcpy(outgoingBuf, msgId, n);
  n = strlen(cid) + 1;   if (n > CRUMB_ID_LEN) n = CRUMB_ID_LEN; memcpy(outgoingBuf + MSG_ID_LEN, cid, n);
  n = strlen(typ) + 1;   if (n > TYPE_LEN) n = TYPE_LEN; memcpy(outgoingBuf + MSG_ID_LEN + CRUMB_ID_LEN, typ, n);
  n = strlen(msg) + 1;   if (n > MESSAGE_LEN) n = MESSAGE_LEN; memcpy(outgoingBuf + MSG_ID_LEN + CRUMB_ID_LEN + TYPE_LEN, msg, n);
  int32_t hc = 1;
  memcpy(outgoingBuf + MSG_ID_LEN + CRUMB_ID_LEN + TYPE_LEN + MESSAGE_LEN, &hc, 4);
  uint32_t delayMs = SEND_DELAY_MS;
  memcpy(outgoingBuf + MSG_ID_LEN + CRUMB_ID_LEN + TYPE_LEN + MESSAGE_LEN + 4, &delayMs, 4);

  Serial.print("Sending id=");
  Serial.println(msgId);

  for (int r = 0; r < 3; r++) {
    esp_err_t result = esp_now_send(crumbB_Mac, outgoingBuf, CRUMB_PAYLOAD_LEN);
    if (result != ESP_OK) {
      Serial.println("esp_now_send error");
    }
    if (r < 2) delay(80);
  }

  digitalWrite(LED_PIN, HIGH);
  delay(200);
  digitalWrite(LED_PIN, LOW);

  delay(10000);
}
