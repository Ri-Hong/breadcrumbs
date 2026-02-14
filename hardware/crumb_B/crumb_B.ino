// Crumb_B: ESP-NOW relay — receive from A, forward to C.
// Chain: A -> B -> C -> D. Same payload layout; increments hop_count on forward.

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <string.h>

// Crumb_C MAC (from hardware/MACs.md)
uint8_t crumbC_Mac[] = {0x98, 0xF4, 0xAB, 0x6F, 0xFC, 0x80};

#define LED_PIN 2
#define ESP_NOW_CHANNEL 6

#define MSG_ID_LEN   24
#define CRUMB_ID_LEN 8
#define TYPE_LEN     8
#define MESSAGE_LEN  64
#define CRUMB_PAYLOAD_LEN (MSG_ID_LEN + CRUMB_ID_LEN + TYPE_LEN + MESSAGE_LEN + 4 + 4)

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
static volatile int pendingHead = 0;
static volatile int pendingTail = 0;

uint8_t forwardBuf[CRUMB_PAYLOAD_LEN];
esp_now_peer_info_t peerInfo;

void OnDataRecv(const uint8_t* mac, const uint8_t* incomingData, int len) {
  if (len != CRUMB_PAYLOAD_LEN) return;

  int nextHead = (pendingHead + 1) % PENDING_QUEUE_LEN;
  if (nextHead == pendingTail) return;

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

void OnDataSent(const uint8_t* mac_addr, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("Forward OK");
  } else {
    Serial.println("Forward FAIL");
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
  esp_now_register_recv_cb(OnDataRecv);
  esp_now_register_send_cb(OnDataSent);

  memcpy(peerInfo.peer_addr, crumbC_Mac, 6);
  peerInfo.channel = ESP_NOW_CHANNEL;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer (Crumb_C)");
    return;
  }

  Serial.println("Crumb_B: listening for A, forwarding to C");
}

void loop() {
  if (pendingTail != pendingHead) {
    struct pending* m = &pendingQueue[pendingTail];
    pendingTail = (pendingTail + 1) % PENDING_QUEUE_LEN;

    Serial.print("Forwarding id=");
    Serial.println(m->message_id);

    int32_t hc = m->hop_count + 1;

    memset(forwardBuf, 0, CRUMB_PAYLOAD_LEN);
    size_t n;
    n = strlen(m->message_id) + 1; if (n > MSG_ID_LEN) n = MSG_ID_LEN; memcpy(forwardBuf, m->message_id, n);
    n = strlen(m->crumb_id) + 1;   if (n > CRUMB_ID_LEN) n = CRUMB_ID_LEN; memcpy(forwardBuf + MSG_ID_LEN, m->crumb_id, n);
    n = strlen(m->type) + 1;       if (n > TYPE_LEN) n = TYPE_LEN; memcpy(forwardBuf + MSG_ID_LEN + CRUMB_ID_LEN, m->type, n);
    n = strlen(m->message) + 1;    if (n > MESSAGE_LEN) n = MESSAGE_LEN; memcpy(forwardBuf + MSG_ID_LEN + CRUMB_ID_LEN + TYPE_LEN, m->message, n);
    memcpy(forwardBuf + MSG_ID_LEN + CRUMB_ID_LEN + TYPE_LEN + MESSAGE_LEN, &hc, 4);
    memcpy(forwardBuf + MSG_ID_LEN + CRUMB_ID_LEN + TYPE_LEN + MESSAGE_LEN + 4, &m->delay_ms, 4);

    for (int r = 0; r < 3; r++) {
      esp_err_t result = esp_now_send(crumbC_Mac, forwardBuf, CRUMB_PAYLOAD_LEN);
      if (result != ESP_OK) {
        Serial.println("esp_now_send error");
      }
      if (r < 2) delay(80);
    }

    delay(200);
    digitalWrite(LED_PIN, LOW);
  }
  delay(10);
}
