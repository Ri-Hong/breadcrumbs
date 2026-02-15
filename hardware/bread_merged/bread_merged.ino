/*
 * Bread (Pouch) — Merged: drop controller + web server.
 * Drop order: D → C → B → A (first to drop = D, last = A).
 * Trail (messages/ripple): Bread → A → B → C → D. Web UI at http://192.168.4.1
 * See hardware/MACs.md.
 *
 * LED: SOLID ON = tracked crumb signal good; OFF = bad → then drop next;
 *      BLINKING = crumb in pouch (RSSI > -40).
 */

#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <WebServer.h>
#include <string.h>

#ifndef BREAD_USE_ESPNOW_RSSI
#define BREAD_USE_ESPNOW_RSSI 1  // 1 = real RSSI (newer core); 0 = Arduino ESP32 1.0.x
#endif

#define LED_PIN 2
#define BUTTON_PIN 0
const char* ap_ssid = "Bread";
const char* ap_password = "trail123";
#define ESP_NOW_CHANNEL 6

#define TYPE_RIPPLE "RIPPLE"
#define MSG_ID_LEN 24
#define CRUMB_ID_LEN 8
#define TYPE_LEN 8
#define MESSAGE_LEN 64
#define CRUMB_PAYLOAD_LEN (MSG_ID_LEN + CRUMB_ID_LEN + TYPE_LEN + MESSAGE_LEN + 4 + 4)

// Drop order: D first, then C, B, A (index 0 = first to drop).
const int N = 4;
const char crumb_ids[N] = {'D', 'C', 'B', 'A'};
const uint8_t crumb_macs[][6] = {
    {0xE4, 0x65, 0xB8, 0x80, 0x08, 0xC4},  // D  first to drop
    {0x98, 0xF4, 0xAB, 0x6F, 0xFC, 0x80},  // C
    {0x24, 0x0A, 0xC4, 0xAE, 0x97, 0xA8},  // B
    {0x24, 0x0A, 0xC4, 0xAF, 0x63, 0xA4},  // A  trail head (Bread sends messages here)
};
#define CRUMB_TRAIL_HEAD_IDX 3  // A: Bread → A → B → C → D

#define MSG_EMPTY 0x00
#define MSG_RELEASE 0x01
#define MSG_PING 0x02
#define MSG_IN_POUCH 0x03
typedef struct __attribute__((packed)) bread_message {
    uint8_t type;
    uint8_t crumb_id;
} bread_message_t;

enum State { INIT, WAIT_FOR_START, DROP_NEXT, TRACK_SIGNAL, DONE, PICKUP, ERROR };
State st = INIT;
int next_idx = 0;
char c_id = 0;
// Pickup order: first to pick = A (index 3), then B, C, D (indices 2, 1, 0)
static const int pickup_order[4] = {3, 2, 1, 0};
int pickup_k = 0;  // 0..4; when 4, all picked up
unsigned long last_drop_ms = 0;
unsigned long weak_start_ms = 0;
const int RSSI_DROP = -60;
const int RSSI_IN_POUCH = -40;
const unsigned long WEAK_FOR_MS = 500;
const unsigned long MIN_TIME_BETWEEN_DROPS = 5000;

int last_rssi[4] = {-100, -100, -100, -100};
unsigned long last_seen_ms[4] = {0, 0, 0, 0};
unsigned long last_print_ms = 0;
unsigned long in_pouch_blink_until_ms = 0;
unsigned long last_error_ms = 0;
unsigned long last_recv_debug_ms = 0;
unsigned long last_in_pouch_sent_ms[4] = {0, 0, 0, 0};

WebServer server(80);
uint8_t outgoingBuf[CRUMB_PAYLOAD_LEN];
static uint32_t messageCounter = 0;
static unsigned long lastRipplePressMs = 0;
#define RIPPLE_DEBOUNCE_MS 600

static int idxFromId(char id) {
    for (int i = 0; i < N; i++)
        if (crumb_ids[i] == id) return i;
    return -1;
}
bool armed() { return true; }

bool releaseCrumb(char crumb_id) {
    int idx = idxFromId(crumb_id);
    if (idx < 0) return false;
    bread_message_t msg;
    msg.type = MSG_RELEASE;
    msg.crumb_id = (uint8_t)crumb_id;
    if (esp_now_send(crumb_macs[idx], (uint8_t*)&msg, sizeof(msg)) != ESP_OK) {
        Serial.printf("  >>> RELEASE to %c failed\n", crumb_id);
        return false;
    }
    return true;
}

int rssiForCid(char id) {
    int idx = idxFromId(id);
    if (idx < 0) return -100;
    unsigned long t = last_seen_ms[idx];
    if (t == 0) return -95;
    if (millis() - t > WEAK_FOR_MS) return -90;
    return last_rssi[idx];
}

static const char* stateName(State s) {
    switch (s) {
        case INIT: return "INIT";
        case WAIT_FOR_START: return "WAIT_FOR_START";
        case DROP_NEXT: return "DROP_NEXT";
        case TRACK_SIGNAL: return "TRACK_SIGNAL";
        case DONE: return "DONE";
        case PICKUP: return "PICKUP";
        case ERROR: return "ERROR";
        default: return "?";
    }
}

void broadcastEmpty() {
    bread_message_t msg;
    msg.type = MSG_EMPTY;
    msg.crumb_id = 0;
    esp_now_send(crumb_macs[0], (uint8_t*)&msg, sizeof(msg));
}

void sendToTrail(const char* message) {
    if (!message || strlen(message) == 0) return;
    messageCounter++;
    char msgId[MSG_ID_LEN], cid[CRUMB_ID_LEN], typ[TYPE_LEN], msg[MESSAGE_LEN];
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
    for (int r = 0; r < 3; r++) {
        esp_now_send(crumb_macs[CRUMB_TRAIL_HEAD_IDX], outgoingBuf, CRUMB_PAYLOAD_LEN);
        if (r < 2) delay(80);
    }
    Serial.print("Sent to trail: ");
    Serial.println(msg);
}

void sendRippleToTrail() {
    messageCounter++;
    char msgId[MSG_ID_LEN], cid[CRUMB_ID_LEN], typ[TYPE_LEN], msg[MESSAGE_LEN];
    snprintf(msgId, sizeof(msgId), "RP%lu-%lu", (unsigned long)messageCounter, (unsigned long)millis());
    snprintf(cid, sizeof(cid), "BREAD");
    snprintf(typ, sizeof(typ), "%s", TYPE_RIPPLE);
    snprintf(msg, sizeof(msg), "ping");
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
    for (int r = 0; r < 3; r++) {
        esp_now_send(crumb_macs[CRUMB_TRAIL_HEAD_IDX], outgoingBuf, CRUMB_PAYLOAD_LEN);
        if (r < 2) delay(80);
    }
    Serial.println("Ripple sent (A → B → C → D)");
}

static bool recv_ever_called = false;

#if BREAD_USE_ESPNOW_RSSI
void OnDataRecv(const esp_now_recv_info_t* info, const uint8_t* incomingData, int len) {
    const uint8_t* mac = info->src_addr;
    int rssi_val = (info->rx_ctrl != NULL) ? info->rx_ctrl->rssi : -99;
    if (!recv_ever_called) {
        recv_ever_called = true;
        Serial.println("[DEBUG] Bread received first ESP-NOW packet.");
    }
    for (int i = 0; i < N; i++) {
        if (memcmp(mac, crumb_macs[i], 6) == 0) {
            last_seen_ms[i] = millis();
            last_rssi[i] = (info->rx_ctrl != NULL) ? info->rx_ctrl->rssi : -70;
            if (last_rssi[i] > RSSI_IN_POUCH) {
                in_pouch_blink_until_ms = millis() + 400;
                if ((unsigned long)(millis() - last_in_pouch_sent_ms[i]) >= 2000) {
                    last_in_pouch_sent_ms[i] = millis();
                    bread_message_t reply;
                    reply.type = MSG_IN_POUCH;
                    reply.crumb_id = crumb_ids[i];
                    esp_now_send(crumb_macs[i], (uint8_t*)&reply, sizeof(reply));
                    Serial.printf("Crumb %c in pouch (RSSI %d > %d)\n", crumb_ids[i], last_rssi[i], RSSI_IN_POUCH);
                }
            }
            if ((unsigned long)(millis() - last_recv_debug_ms) >= 2000) {
                last_recv_debug_ms = millis();
                Serial.printf("[DEBUG] Recv from crumb %c  rssi=%d\n", crumb_ids[i], last_rssi[i]);
            }
            return;
        }
    }
    static unsigned long last_unknown_ms = 0;
    if ((unsigned long)(millis() - last_unknown_ms) >= 5000) {
        last_unknown_ms = millis();
        Serial.printf("[DEBUG] UNKNOWN MAC %02X:%02X:%02X:%02X:%02X:%02X  rssi=%d",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], rssi_val);
        if (len == 2 && incomingData[0] == 0x02) {
            char id = (char)incomingData[1];
            if (id == 'A' || id == 'B' || id == 'C' || id == 'D')
                Serial.printf("  (beacon crumb %c — update MACs.md?)", id);
        }
        Serial.println();
    }
}
void OnDataSent(const wifi_tx_info_t* info, esp_now_send_status_t status) {
    (void)info;
    (void)status;
}
#else
void OnDataRecv(const uint8_t* mac_addr, const uint8_t* data, int len) {
    (void)data;
    (void)len;
    if (!recv_ever_called) {
        recv_ever_called = true;
        Serial.println("[DEBUG] Bread received first ESP-NOW packet.");
    }
    for (int i = 0; i < N; i++) {
        if (memcmp(mac_addr, crumb_macs[i], 6) == 0) {
            last_seen_ms[i] = millis();
            last_rssi[i] = -70;
            if (last_rssi[i] > RSSI_IN_POUCH) {
                in_pouch_blink_until_ms = millis() + 400;
                if ((unsigned long)(millis() - last_in_pouch_sent_ms[i]) >= 2000) {
                    last_in_pouch_sent_ms[i] = millis();
                    bread_message_t reply;
                    reply.type = MSG_IN_POUCH;
                    reply.crumb_id = crumb_ids[i];
                    esp_now_send(crumb_macs[i], (uint8_t*)&reply, sizeof(reply));
                }
            }
            if ((unsigned long)(millis() - last_recv_debug_ms) >= 2000) {
                last_recv_debug_ms = millis();
                Serial.printf("[DEBUG] Recv from crumb %c\n", crumb_ids[i]);
            }
            return;
        }
    }
    static unsigned long last_unknown_ms = 0;
    if ((unsigned long)(millis() - last_unknown_ms) >= 5000) {
        last_unknown_ms = millis();
        Serial.printf("[DEBUG] UNKNOWN MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
                      mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    }
}
void OnDataSent(const uint8_t* mac_addr, esp_now_send_status_t status) {
    (void)mac_addr;
    (void)status;
}
#endif

const char* htmlPage =
    "<!DOCTYPE html><html><head>"
    "<meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<title>Breadcrumbs — Send message</title><style>"
    "*{box-sizing:border-box}body{font-family:system-ui,sans-serif;margin:0;padding:20px;background:#1a1a1a;color:#eee;min-height:100vh}"
    "h1{font-size:1.3rem;margin-bottom:4px}p{color:#999;font-size:.9rem;margin-top:0}"
    "form{margin-top:16px}textarea{width:100%;padding:12px;font-size:16px;border-radius:8px;border:1px solid #444;background:#2a2a2a;color:#eee;resize:vertical;min-height:100px}"
    "button{margin-top:12px;padding:12px 24px;font-size:16px;background:#0d6efd;color:#fff;border:none;border-radius:8px;cursor:pointer}"
    "button.ripple{background:#7c3aed;margin-left:12px}button:disabled{opacity:.6;cursor:not-allowed}"
    ".status{margin-top:12px;font-size:.9rem}.ripple-section{margin-top:24px;padding-top:20px;border-top:1px solid #444}"
    ".toggle-row{display:flex;align-items:center;gap:12px;margin-top:12px}"
    ".toggle-switch{position:relative;width:48px;height:26px;background:#444;border-radius:13px;cursor:pointer;transition:background .2s}"
    ".toggle-switch.on{background:#7c3aed}.toggle-switch::after{content:'';position:absolute;width:22px;height:22px;left:2px;top:2px;background:#eee;border-radius:50%;transition:transform .2s}"
    ".toggle-switch.on::after{transform:translateX(22px)}.ripple-status{font-size:.9rem;color:#999}.ok{color:#4ade80}.err{color:#f87171}"
    ".toast{position:fixed;bottom:24px;left:50%;transform:translateX(-50%) translateY(80px);min-width:260px;max-width:90%;padding:12px 20px;border-radius:10px;background:#2d2d2d;color:#eee;font-size:.95rem;text-align:center;box-shadow:0 4px 20px rgba(0,0,0,.4);z-index:9999;opacity:0;transition:transform .3s,opacity .3s}"
    ".toast.show{transform:translateX(-50%) translateY(0);opacity:1}"
    ".toast.ok{background:#166534;color:#dcfce7}.toast.err{background:#991b1b;color:#fecaca}"
    "</style></head><body><h1>Send message out</h1>"
    "<p>Type a message below. It will be sent along the trail to the gateway and then to the internet.</p>"
    "<form id=\"f\"><textarea name=\"message\" maxlength=\"64\" placeholder=\"e.g. I'm OK, at the creek\" required id=\"msgArea\"></textarea><br>"
    "<button type=\"submit\" id=\"btn\">Send</button></form><div class=\"status\" id=\"status\"></div>"
    "<div id=\"toast\" class=\"toast\" aria-live=\"polite\"></div>"
    "<div class=\"ripple-section\"><p>Ripple along the trail (A → B → C → D). Each crumb will light its LED and beep.</p>"
    "<div class=\"toggle-row\"><div class=\"toggle-switch\" id=\"rippleToggle\" role=\"switch\" aria-checked=\"false\" tabindex=\"0\"></div>"
    "<span class=\"ripple-status\" id=\"rippleStatus\">Off — turn on to send a ripple every 5s</span></div></div>"
    "<script>"
    "function showToast(msg,isOk){var t=document.getElementById('toast');t.textContent=msg;t.className='toast '+(isOk?'ok':'err');t.classList.add('show');clearTimeout(window._toastT);window._toastT=setTimeout(function(){t.classList.remove('show')},3000)}"
    "document.getElementById('f').onsubmit=function(e){e.preventDefault();var btn=document.getElementById('btn'),msgArea=document.getElementById('msgArea'),status=document.getElementById('status');var msg=(msgArea.value||'').trim();if(!msg){showToast('Message is empty',false);return}btn.disabled=true;status.textContent='Sending…';status.className='status';var fd=new FormData(document.getElementById('f'));fetch('/send',{method:'POST',body:fd}).then(function(r){if(r.ok){showToast('Message sent. It will relay along the trail.',true);msgArea.value='';msgArea.focus()}else{showToast('Send failed.',false)}}).catch(function(){showToast('Send failed.',false)}).then(function(){btn.disabled=false;status.textContent=''})};"
    "var RIPPLE_INTERVAL_MS=5000,rippleToggle=document.getElementById('rippleToggle'),rippleStatus=document.getElementById('rippleStatus'),rippleIntervalId=null;"
    "function sendRipple(){rippleStatus.textContent='Sending…';fetch('/ripple',{method:'POST'}).then(function(r){return r.text()}).then(function(){rippleStatus.textContent='On — next ripple in 5s'}).catch(function(){rippleStatus.textContent='On — send failed, retrying in 5s'})}"
    "function startRipple(){if(rippleIntervalId)return;rippleToggle.classList.add('on');rippleToggle.setAttribute('aria-checked','true');sendRipple();rippleIntervalId=setInterval(sendRipple,RIPPLE_INTERVAL_MS)}"
    "function stopRipple(){if(!rippleIntervalId)return;clearInterval(rippleIntervalId);rippleIntervalId=null;rippleToggle.classList.remove('on');rippleToggle.setAttribute('aria-checked','false');rippleStatus.textContent='Off — turn on to send a ripple every 5s'}"
    "function toggleRipple(){if(rippleIntervalId)stopRipple();else startRipple()}"
    "rippleToggle.addEventListener('click',toggleRipple);rippleToggle.addEventListener('keydown',function(e){if(e.key==' '||e.key=='Enter'){e.preventDefault();toggleRipple()}});"
    "</script></body></html>";

void handleRoot() { server.send(200, "text/html", htmlPage); }

void handleSend() {
    if (server.method() != HTTP_POST) { server.send(405, "text/plain", "Method Not Allowed"); return; }
    String message = server.hasArg("message") ? server.arg("message") : "";
    message.trim();
    if (message.length() == 0) { server.send(400, "text/plain", "Message is empty"); return; }
    if (message.length() > (unsigned)(MESSAGE_LEN - 1)) message = message.substring(0, MESSAGE_LEN - 1);
    sendToTrail(message.c_str());
    server.send(200, "text/html",
        "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Sent</title><style>body{font-family:system-ui;padding:20px;background:#1a1a1a;color:#eee;} .ok{color:#4ade80;}</style></head>"
        "<body><p class='ok'>Message sent. It will relay along the trail.</p><p><a href='/'>Send another</a></p></body></html>");
}

void handleRipple() {
    if (server.method() != HTTP_POST) { server.send(405, "text/plain", "Method Not Allowed"); return; }
    sendRippleToTrail();
    server.send(200, "text/html",
        "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Ripple sent</title><style>body{font-family:system-ui;padding:20px;background:#1a1a1a;color:#eee;} .ok{color:#4ade80;}</style></head>"
        "<body><p class='ok'>Ripple sent. It will travel A → B → C → D.</p><p><a href='/'>Back</a></p></body></html>");
}

void handleNotFound() { server.send(404, "text/plain", "Not Found"); }

void setup() {
    Serial.begin(115200);
    delay(200);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(ap_ssid, ap_password, ESP_NOW_CHANNEL);
    IPAddress ip = WiFi.softAPIP();
    Serial.print("AP IP: ");
    Serial.println(ip);
    esp_wifi_set_channel(ESP_NOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed");
        return;
    }
    esp_now_register_recv_cb(OnDataRecv);
    esp_now_register_send_cb(OnDataSent);

    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    peerInfo.channel = ESP_NOW_CHANNEL;
    peerInfo.encrypt = false;
    peerInfo.ifidx = WIFI_IF_STA;
    for (int i = 0; i < N; i++) {
        memcpy(peerInfo.peer_addr, crumb_macs[i], 6);
        if (esp_now_add_peer(&peerInfo) != ESP_OK)
            Serial.printf("Failed to add peer %c\n", crumb_ids[i]);
    }
    Serial.println("Bread: drop controller + web server. Drop order: D, C, B, A.");
    Serial.printf("Bread MAC: %s\n", WiFi.macAddress().c_str());
    Serial.println("Connect to Bread WiFi, open http://192.168.4.1");

    server.on("/", handleRoot);
    server.on("/send", HTTP_POST, handleSend);
    server.on("/ripple", HTTP_POST, handleRipple);
    server.onNotFound(handleNotFound);
    server.begin();
}

void loop() {
    switch (st) {
        case INIT:
            next_idx = 0;
            c_id = 0;
            st = WAIT_FOR_START;
            break;
        case WAIT_FOR_START:
            if (armed()) st = DROP_NEXT;
            break;
        case DROP_NEXT: {
            if (next_idx >= N) {
                pickup_k = 0;
                st = PICKUP;
                break;
            }
            if (releaseCrumb(crumb_ids[next_idx])) {
                c_id = crumb_ids[next_idx];
                next_idx++;
                last_drop_ms = millis();
                weak_start_ms = 0;
                st = TRACK_SIGNAL;
            } else {
                st = ERROR;
            }
            break;
        }
        case TRACK_SIGNAL: {
            int idx = idxFromId(c_id);
#if BREAD_USE_ESPNOW_RSSI
            int rssi = (idx >= 0) ? last_rssi[idx] : -99;
            bool nowWeak = (idx < 0 || rssi < RSSI_DROP);
#else
            unsigned long since = (idx >= 0 && last_seen_ms[idx] != 0) ? (millis() - last_seen_ms[idx]) : (unsigned long)-1;
            bool nowWeak = (idx < 0 || last_seen_ms[idx] == 0 || since > WEAK_FOR_MS);
#endif
            if (nowWeak) {
                if (weak_start_ms == 0) weak_start_ms = millis();
            } else {
                weak_start_ms = 0;
            }
            if (nowWeak)
                digitalWrite(LED_PIN, LOW);
            else
                digitalWrite(LED_PIN, HIGH);
            bool receivedOnce = (idx >= 0 && last_seen_ms[idx] != 0);
            bool weakEnough = (weak_start_ms != 0) && (millis() - weak_start_ms >= WEAK_FOR_MS);
            bool cooldownOk = (millis() - last_drop_ms >= MIN_TIME_BETWEEN_DROPS);
            if (weakEnough && cooldownOk && receivedOnce) {
                if (next_idx < N)
                    st = DROP_NEXT;
                else {
                    pickup_k = 0;
                    st = PICKUP;
                }
            }
            break;
        }
        case DONE:
            broadcastEmpty();
            break;
        case PICKUP: {
            if (pickup_k >= 4) break;
            int pidx = pickup_order[pickup_k];
            if (last_rssi[pidx] > RSSI_IN_POUCH) {
                in_pouch_blink_until_ms = millis() + 400;
                if ((unsigned long)(millis() - last_in_pouch_sent_ms[pidx]) >= 2000) {
                    last_in_pouch_sent_ms[pidx] = millis();
                    bread_message_t msg;
                    msg.type = MSG_IN_POUCH;
                    msg.crumb_id = crumb_ids[pidx];
                    esp_now_send(crumb_macs[pidx], (uint8_t*)&msg, sizeof(msg));
                    Serial.printf("Crumb %c picked up (RSSI %d > %d)\n", crumb_ids[pidx], last_rssi[pidx], RSSI_IN_POUCH);
                }
                pickup_k++;
            }
            break;
        }
        case ERROR: {
            if (last_error_ms == 0) last_error_ms = millis();
            if ((unsigned long)(millis() - last_error_ms) >= 2000) {
                Serial.println("  Retrying RELEASE...");
                last_error_ms = 0;
                st = DROP_NEXT;
            }
            break;
        }
    }

    if (millis() < in_pouch_blink_until_ms)
        digitalWrite(LED_PIN, (millis() / 100) % 2);
    else if (st != TRACK_SIGNAL && st != PICKUP)
        digitalWrite(LED_PIN, LOW);

    if ((unsigned long)(millis() - last_print_ms) >= 500) {
        last_print_ms = millis();
        int r = (c_id ? rssiForCid(c_id) : 0);
#if BREAD_USE_ESPNOW_RSSI
        const char* sig = (st == TRACK_SIGNAL && r >= RSSI_DROP) ? "good" : (st == TRACK_SIGNAL ? "bad" : "-");
        int rDisplay = r;
#else
        int tidx = idxFromId(c_id);
        unsigned long ago = (tidx >= 0 && last_seen_ms[tidx] != 0) ? (millis() - last_seen_ms[tidx]) : 999999;
        bool recent = (tidx >= 0 && last_seen_ms[tidx] != 0 && ago <= WEAK_FOR_MS);
        const char* sig = (st == TRACK_SIGNAL && recent) ? "good" : (st == TRACK_SIGNAL ? "bad" : "-");
        int rDisplay = (st == TRACK_SIGNAL && recent) ? -50 : r;
#endif
        Serial.println("----------------------------------------");
        Serial.printf("  State:        %s\n", stateName(st));
        if (st == ERROR)
            Serial.println("  (RELEASE failed — is crumb powered? Wait 2s, then retry.)");
        if (st == TRACK_SIGNAL && c_id && last_seen_ms[idxFromId(c_id)] == 0)
            Serial.println("  (Waiting for beacon from crumb.)");
        Serial.printf("  Tracking:     crumb %c\n", c_id ? c_id : '-');
        Serial.printf("  Next to drop: crumb %c (index %d of 4)\n", next_idx < N ? crumb_ids[next_idx] : '-', next_idx);
        if (st == PICKUP) {
            if (pickup_k < 4)
                Serial.printf("  Next to pick: crumb %c\n", crumb_ids[pickup_order[pickup_k]]);
            else
                Serial.println("  Next to pick: — (all picked up)");
        }
        Serial.printf("  Signal:       %s (RSSI %d dBm)\n", sig, rDisplay);
#if BREAD_USE_ESPNOW_RSSI
        // Indices: 0=D, 1=C, 2=B, 3=A — display as A,B,C,D
        Serial.printf("  RSSI per crumb: A=%d B=%d C=%d D=%d\n", last_rssi[3], last_rssi[2], last_rssi[1], last_rssi[0]);
#else
        {
            unsigned long now = millis();
            int ago[4];
            for (int i = 0; i < 4; i++)
                ago[i] = (last_seen_ms[i] != 0) ? (int)((now - last_seen_ms[i]) / 1000) : 999;
            Serial.printf("  Last beacon: A=%ds B=%ds C=%ds D=%ds (RSSI N/A)\n", ago[3], ago[2], ago[1], ago[0]);
        }
#endif
        if (st == TRACK_SIGNAL && c_id) {
            int idx = idxFromId(c_id);
            unsigned long ago = (idx >= 0 && last_seen_ms[idx] != 0) ? (millis() - last_seen_ms[idx]) / 1000 : 999;
            Serial.printf("  Last beacon from crumb %c: %lu s ago\n", c_id, (unsigned long)ago);
        }
        Serial.println("----------------------------------------");
    }

    server.handleClient();

    if (digitalRead(BUTTON_PIN) == LOW) {
        unsigned long now = millis();
        if ((unsigned long)(now - lastRipplePressMs) >= RIPPLE_DEBOUNCE_MS) {
            lastRipplePressMs = now;
            sendRippleToTrail();
        }
    }

    delay(10);
}
