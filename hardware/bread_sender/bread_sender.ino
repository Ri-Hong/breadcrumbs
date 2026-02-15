// Bread: Pouch device. Runs WiFi AP + web server so the hiker can connect
// with a phone (no internet), open a page, type a message, and send it.
// Bread then sends via ESP-NOW to Crumb_A -> B -> C -> D -> API.
// Chain: Bread -> A -> B -> C -> D

#include <WiFi.h>
#include <WebServer.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

// Crumb_A MAC (from hardware/MACs.md) — Bread sends to A
uint8_t crumbA_Mac[] = {0x24, 0x0A, 0xC4, 0xAF, 0x63, 0xA4};

#define LED_PIN 2
#define ESP_NOW_CHANNEL 6

#define MSG_ID_LEN   24
#define CRUMB_ID_LEN 8
#define TYPE_LEN     8
#define MESSAGE_LEN  64
#define CRUMB_PAYLOAD_LEN (MSG_ID_LEN + CRUMB_ID_LEN + TYPE_LEN + MESSAGE_LEN + 4 + 4)

const char* ap_ssid     = "Breadcrumbs-Pouch";
const char* ap_password = "trail123";  // optional; use "" for open network

WebServer server(80);
esp_now_peer_info_t peerInfo = {};

uint8_t outgoingBuf[CRUMB_PAYLOAD_LEN];
static uint32_t messageCounter = 0;

void OnDataSent(const uint8_t* mac_addr, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("Send to A OK");
  } else {
    Serial.println("Send to A FAIL");
  }
}

// Build payload and send to Crumb_A (same layout as crumb_A uses when sending to B)
void sendToTrail(const char* message) {
  if (!message || strlen(message) == 0) return;

  messageCounter++;
  char msgId[MSG_ID_LEN];
  char cid[CRUMB_ID_LEN];
  char typ[TYPE_LEN];
  char msg[MESSAGE_LEN];

  snprintf(msgId, sizeof(msgId), "BR%lu-%lu", (unsigned long)messageCounter, (unsigned long)millis());
  snprintf(cid, sizeof(cid), "BREAD");
  snprintf(typ, sizeof(typ), "MSG");
  strncpy(msg, message, MESSAGE_LEN - 1);
  msg[MESSAGE_LEN - 1] = '\0';

  memset(outgoingBuf, 0, CRUMB_PAYLOAD_LEN);
  size_t n;
  n = strlen(msgId) + 1; if (n > MSG_ID_LEN) n = MSG_ID_LEN; memcpy(outgoingBuf, msgId, n);
  n = strlen(cid) + 1;   if (n > CRUMB_ID_LEN) n = CRUMB_ID_LEN; memcpy(outgoingBuf + MSG_ID_LEN, cid, n);
  n = strlen(typ) + 1;   if (n > TYPE_LEN) n = TYPE_LEN; memcpy(outgoingBuf + MSG_ID_LEN + CRUMB_ID_LEN, typ, n);
  n = strlen(msg) + 1;   if (n > MESSAGE_LEN) n = MESSAGE_LEN; memcpy(outgoingBuf + MSG_ID_LEN + CRUMB_ID_LEN + TYPE_LEN, msg, n);
  int32_t hop_count = 1;
  uint32_t delay_ms = 0;
  memcpy(outgoingBuf + MSG_ID_LEN + CRUMB_ID_LEN + TYPE_LEN + MESSAGE_LEN, &hop_count, 4);
  memcpy(outgoingBuf + MSG_ID_LEN + CRUMB_ID_LEN + TYPE_LEN + MESSAGE_LEN + 4, &delay_ms, 4);

  Serial.print("Sending to trail: ");
  Serial.println(msg);

  for (int r = 0; r < 3; r++) {
    esp_err_t result = esp_now_send(crumbA_Mac, outgoingBuf, CRUMB_PAYLOAD_LEN);
    if (result != ESP_OK) {
      Serial.print("esp_now_send error: 0x");
      Serial.println((int)result, HEX);
    }
    if (r < 2) delay(80);
  }
  digitalWrite(LED_PIN, HIGH);
  delay(200);
  digitalWrite(LED_PIN, LOW);
}

const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Breadcrumbs — Send message</title>
  <style>
    * { box-sizing: border-box; }
    body { font-family: system-ui, sans-serif; margin: 0; padding: 20px; background: #1a1a1a; color: #eee; min-height: 100vh; }
    h1 { font-size: 1.3rem; margin-bottom: 4px; }
    p { color: #999; font-size: 0.9rem; margin-top: 0; }
    form { margin-top: 16px; }
    textarea { width: 100%; padding: 12px; font-size: 16px; border-radius: 8px; border: 1px solid #444; background: #2a2a2a; color: #eee; resize: vertical; min-height: 100px; }
    button { margin-top: 12px; padding: 12px 24px; font-size: 16px; background: #0d6efd; color: #fff; border: none; border-radius: 8px; cursor: pointer; }
    button:disabled { opacity: 0.6; cursor: not-allowed; }
    .status { margin-top: 12px; font-size: 0.9rem; }
    .ok { color: #4ade80; }
    .err { color: #f87171; }
  </style>
</head>
<body>
  <h1>Send message out</h1>
  <p>Type a message below. It will be sent along the trail to the gateway and then to the internet.</p>
  <form method="POST" action="/send" id="f">
    <textarea name="message" maxlength="64" placeholder="e.g. I'm OK, at the creek" required></textarea>
    <br>
    <button type="submit" id="btn">Send</button>
  </form>
  <div class="status" id="status"></div>
  <script>
    document.getElementById('f').onsubmit = function() {
      document.getElementById('btn').disabled = true;
      document.getElementById('status').textContent = 'Sending…';
      document.getElementById('status').className = 'status';
    };
  </script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send(200, "text/html", htmlPage);
}

void handleSend() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  String message = server.hasArg("message") ? server.arg("message") : "";
  message.trim();

  if (message.length() == 0) {
    server.send(400, "text/plain", "Message is empty");
    return;
  }

  if (message.length() > MESSAGE_LEN - 1) {
    message = message.substring(0, MESSAGE_LEN - 1);
  }

  sendToTrail(message.c_str());

  server.send(200, "text/html",
    "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Sent</title><style>body{font-family:system-ui;padding:20px;background:#1a1a1a;color:#eee;} .ok{color:#4ade80;}</style></head>"
    "<body><p class='ok'>Message sent. It will relay along the trail.</p><p><a href='/'>Send another</a></p></body></html>");
}

void handleNotFound() {
  server.send(404, "text/plain", "Not Found");
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Start AP on channel 6 so ESP-NOW (same channel) works with Crumb_A
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password, ESP_NOW_CHANNEL);
  IPAddress ip = WiFi.softAPIP();
  Serial.print("AP IP: ");
  Serial.println(ip);

  // ESP-NOW: same channel, add Crumb_A as peer
  esp_wifi_set_channel(ESP_NOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }
  esp_now_register_send_cb(OnDataSent);

  // In AP mode, peer must be added on the AP interface (else esp_now_send fails)
  memcpy(peerInfo.peer_addr, crumbA_Mac, 6);
  peerInfo.channel = ESP_NOW_CHANNEL;
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_AP;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer (Crumb_A)");
  } else {
    Serial.println("Bread: ESP-NOW ready, peer A added");
  }

  server.on("/", handleRoot);
  server.on("/send", HTTP_POST, handleSend);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("Web server started. Connect to Breadcrumbs-Pouch and open http://192.168.4.1");
}

void loop() {
  server.handleClient();
  delay(2);
}
