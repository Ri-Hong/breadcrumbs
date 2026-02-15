// Crumb_A: Relay — receive from Bread (pouch), forward to B.
// Chain: Bread -> A -> B -> C -> D (D relays to API).

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <string.h>

// Bread MAC (sender) — A must receive from Bread
uint8_t bread_Mac[] = {0xE4, 0x65, 0xB8, 0x83, 0x56, 0x30};
// Crumb_B MAC (from hardware/MACs.md)
uint8_t crumbB_Mac[] = {0x24, 0x0A, 0xC4, 0xAE, 0x97, 0xA8};

#define LED_PIN 2
#define BUZZER_PIN 25   // Active buzzer (or passive with external driver); set to -1 if no buzzer
#define RIPPLE_DELAY_MS 500   // Delay before forwarding a RIPPLE so the wave is visible A→B→C→D
#define MESSAGE_DELAY_MS 1000  // Delay before forwarding a standard MSG so the wave is visible along the trail
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

static char lastQueuedMsgId[MSG_ID_LEN + 1] = {0};

uint8_t forwardBuf[CRUMB_PAYLOAD_LEN];
esp_now_peer_info_t peerInfo;

void OnDataRecv(const uint8_t* mac, const uint8_t* incomingData, int len) {
  Serial.print("Recv ");
  Serial.print(len);
  Serial.println(" bytes");
  if (len != CRUMB_PAYLOAD_LEN) return;

  char msgId[MSG_ID_LEN + 1];
  memcpy(msgId, incomingData, MSG_ID_LEN);
  msgId[MSG_ID_LEN] = '\0';
  if (strcmp(msgId, lastQueuedMsgId) == 0) return;

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

  strncpy(lastQueuedMsgId, m->message_id, MSG_ID_LEN);
  lastQueuedMsgId[MSG_ID_LEN] = '\0';
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

// Pulse buzzer for ripple messages (two short beeps). No-op if BUZZER_PIN < 0.
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

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
#if BUZZER_PIN >= 0
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
#endif

  WiFi.mode(WIFI_STA);
  delay(100);
  esp_wifi_set_channel(ESP_NOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  delay(100);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);
  esp_now_register_send_cb(OnDataSent);

  // Add Bread as peer so we receive from it (some stacks only deliver from known peers)
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, bread_Mac, 6);
  peerInfo.channel = ESP_NOW_CHANNEL;
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer (Bread)");
  } else {
    Serial.println("Peer Bread added");
  }

  memcpy(peerInfo.peer_addr, crumbB_Mac, 6);
  peerInfo.channel = ESP_NOW_CHANNEL;
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer (Crumb_B)");
    return;
  }

  Serial.print("Crumb_A MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.println("Crumb_A: listening for Bread, forwarding to B");
}

void loop() {
  if (pendingTail != pendingHead) {
    struct pending* m = &pendingQueue[pendingTail];
    pendingTail = (pendingTail + 1) % PENDING_QUEUE_LEN;

    Serial.print("Forwarding id=");
    Serial.println(m->message_id);

    if (strcmp(m->type, "RIPPLE") == 0) {
      delay(RIPPLE_DELAY_MS);
    } else if (strcmp(m->type, "MSG") == 0) {
      delay(MESSAGE_DELAY_MS);
    }

    int32_t hc = m->hop_count + 1;

    memset(forwardBuf, 0, CRUMB_PAYLOAD_LEN);
    size_t n;
    n = strlen(m->message_id) + 1; if (n > MSG_ID_LEN) n = MSG_ID_LEN; memcpy(forwardBuf, m->message_id, n);
    n = strlen(m->crumb_id) + 1;   if (n > CRUMB_ID_LEN) n = CRUMB_ID_LEN; memcpy(forwardBuf + MSG_ID_LEN, m->crumb_id, n);
    n = strlen(m->type) + 1;       if (n > TYPE_LEN) n = TYPE_LEN; memcpy(forwardBuf + MSG_ID_LEN + CRUMB_ID_LEN, m->type, n);
    n = strlen(m->message) + 1;   if (n > MESSAGE_LEN) n = MESSAGE_LEN; memcpy(forwardBuf + MSG_ID_LEN + CRUMB_ID_LEN + TYPE_LEN, m->message, n);
    memcpy(forwardBuf + MSG_ID_LEN + CRUMB_ID_LEN + TYPE_LEN + MESSAGE_LEN, &hc, 4);
    memcpy(forwardBuf + MSG_ID_LEN + CRUMB_ID_LEN + TYPE_LEN + MESSAGE_LEN + 4, &m->delay_ms, 4);

    for (int r = 0; r < 3; r++) {
      esp_err_t result = esp_now_send(crumbB_Mac, forwardBuf, CRUMB_PAYLOAD_LEN);
      if (result != ESP_OK) {
        Serial.println("esp_now_send error");
      }
      if (r < 2) delay(80);
    }

    if (strcmp(m->type, "RIPPLE") == 0) {
      pulseBuzzer();
    }
    delay(200);
    digitalWrite(LED_PIN, LOW);
  }
  delay(10);
}
