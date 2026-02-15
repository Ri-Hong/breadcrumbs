/*
 * Bread (Pouch) — Controller carried by the hiker.
 * See README.md and MACs.md.
 *
 * -----------------------------------------------------------------------------
 * LED MEANINGS (Bread / Pouch)
 * -----------------------------------------------------------------------------
 *   SOLID ON   — Tracked crumb's signal is good (reachable). RSSI from received ESP-NOW beacons.
 *   SOLID OFF  — Tracked crumb's signal is bad or lost (you've walked away).
 *                After a short time + cooldown, Bread will trigger "drop next" crumb.
 *   BLINKING   — A crumb is in pouch (RSSI > -30). Brief blink.
 *
 * -----------------------------------------------------------------------------
 * WHAT'S SUPPOSED TO HAPPEN
 * -----------------------------------------------------------------------------
 *   Start: Crumb A is in the pouch. Bread does NOT get dropped — the pouch stays with you.
 *   First drop is always crumb A.
 *
 *   1. You start with crumb A (and B,C,D) in the pouch. Bread goes INIT → WAIT_FOR_START → DROP_NEXT.
 *   2. DROP_NEXT: Bread sends RELEASE to the next crumb (first time = crumb A). That crumb's LED
 *      turns ON so you know which one to take out and drop. (Uses sender.ino-style ESP-NOW.)
 *   3. TRACK_SIGNAL: Bread watches RSSI. Past -60 dBm (weaker) → drop next. Within -30 dBm (stronger) → in pouch.
 *      LED ON = good, LED OFF = bad. When bad long enough + cooldown → DROP_NEXT.
 *   4. Repeat until all four dropped → DONE. In pouch = by RSSI: when crumb's signal > -30 dBm,
 *      Bread sends MSG_IN_POUCH to that crumb (LED off) and blinks. No Hall sensor.
 */

#include <WiFi.h>
#include <esp_now.h>

// ---------------------------------------------------------------------------
// LED: use built-in (blue) LED. On most ESP32 boards this is GPIO 2.
// ---------------------------------------------------------------------------
#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif
#define LED_PIN LED_BUILTIN

// Servo disabled for now
// #define SERVO_PIN 13

static int idxFromId(char id);

// ---------------------------------------------------------------------------
// Crumb IDs and MACs — MUST match hardware/MACs.md
// Drop order: D first, then C, B, A (index 0 = first to drop).
// ---------------------------------------------------------------------------
const int N = 4;
const char crumb_ids[N] = {'D', 'C', 'B', 'A'};

// From MACs.md: A=24:0A:C4:AF:63:A4  B=24:0A:C4:AE:97:A8  C=98:F4:AB:6F:FC:80  D=E4:65:B8:80:08:C4
const uint8_t crumb_macs[][6] = {
    {0xE4, 0x65, 0xB8, 0x80, 0x08, 0xC4},  // D  E4:65:B8:80:08:C4  (first to drop)
    {0x98, 0xF4, 0xAB, 0x6F, 0xFC, 0x80},  // C  98:F4:AB:6F:FC:80
    {0x24, 0x0A, 0xC4, 0xAE, 0x97, 0xA8},  // B  24:0A:C4:AE:97:A8
    {0x24, 0x0A, 0xC4, 0xAF, 0x63, 0xA4},  // A  24:0A:C4:AF:63:A4
};

// ---------------------------------------------------------------------------
// Message types (Bread ↔ Crumbs)
// ---------------------------------------------------------------------------
#define MSG_EMPTY 0x00     // trail done / idle
#define MSG_RELEASE 0x01   // Bread → crumb: you're the one to drop (LED on)
#define MSG_PING 0x02      // optional / beacon
#define MSG_IN_POUCH 0x03  // Bread → crumb: you're in pouch (RSSI > -30), LED off

typedef struct __attribute__((packed)) bread_message {
    uint8_t type;
    uint8_t crumb_id;  // 'A','B','C','D' for RELEASE
} bread_message_t;

// ---------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------
enum State { INIT,
             WAIT_FOR_START,
             DROP_NEXT,
             TRACK_SIGNAL,
             DONE,
             ERROR };
State st = INIT;

int next_idx = 0;  // next crumb to drop (0..N-1)
char c_id = 0;     // ID of the crumb we're currently tracking (last dropped)

unsigned long last_drop_ms = 0;
unsigned long weak_start_ms = 0;

// Thresholds (signal strength in dBm; no Hall sensor — all by RSSI)
const int RSSI_DROP = -60;      // below this = far → drop next crumb
const int RSSI_IN_POUCH = -40;  // above this = close → crumb is in pouch (tell crumb to turn LED off)
const unsigned long WEAK_FOR_MS = 500;
const unsigned long MIN_TIME_BETWEEN_DROPS = 5000;

// Last RSSI per crumb (from ESP-NOW recv; crumbs send periodic beacons).
int last_rssi[4] = {-100, -100, -100, -100};
unsigned long last_seen_ms[4] = {0, 0, 0, 0};

// Throttle Serial and in-pouch blink
unsigned long last_print_ms = 0;
unsigned long in_pouch_blink_until_ms = 0;
unsigned long last_error_ms = 0;
unsigned long last_recv_debug_ms = 0;
unsigned long last_in_pouch_sent_ms[4] = {0, 0, 0, 0};  // throttle "in pouch" tell crumb

// Pouch has no Hall sensor; start is automatic. (Hall + magnet is on crumbs for pickup detection.)
// Optional: add a button and make armed() return (digitalRead(BTN_PIN) == HIGH) for manual start.
bool armed() {
    return true;  // start drop sequence immediately; replace with button if desired
}

// Get crumb index from id character.
static int idxFromId(char id) {
    for (int i = 0; i < N; i++)
        if (crumb_ids[i] == id) return i;
    return -1;
}

// Send RELEASE (beep/blink) to the given crumb. Returns true if send queued OK.
bool releaseCrumb(char crumb_id) {
    int idx = idxFromId(crumb_id);
    if (idx < 0) return false;

    bread_message_t msg;
    msg.type = MSG_RELEASE;
    msg.crumb_id = (uint8_t)crumb_id;

    esp_err_t err = esp_now_send(crumb_macs[idx], (uint8_t *)&msg, sizeof(msg));
    if (err != ESP_OK) {
        Serial.printf("  >>> RELEASE send to crumb %c failed: 0x%x (is crumb powered on?)\n", crumb_id, (unsigned)err);
        return false;
    }
    return true;
}

// RSSI from last ESP-NOW packet from that crumb (real measurement from rx_ctrl->rssi).
// Never received = -95 (bad). Not seen for WEAK_FOR_MS = -90 (lost). Otherwise use last_rssi.
int rssiForCid(char id) {
    int idx = idxFromId(id);
    if (idx < 0) return -100;
    unsigned long t = last_seen_ms[idx];
    if (t == 0) return -95;                      // never received a packet from this crumb = treat as bad
    if (millis() - t > WEAK_FOR_MS) return -90;  // no packet for a while = signal lost
    return last_rssi[idx];                       // real RSSI from last received packet
}

// Return state name for status output
static const char *stateName(State s) {
    switch (s) {
        case INIT:
            return "INIT";
        case WAIT_FOR_START:
            return "WAIT_FOR_START";
        case DROP_NEXT:
            return "DROP_NEXT";
        case TRACK_SIGNAL:
            return "TRACK_SIGNAL";
        case DONE:
            return "DONE";
        case ERROR:
            return "ERROR";
        default:
            return "?";
    }
}

// Broadcast empty/idle so gateway or other nodes know trail is done.
void broadcastEmpty() {
    bread_message_t msg;
    msg.type = MSG_EMPTY;
    msg.crumb_id = 0;
    // Broadcast to first peer (A) as a simple default; or loop all crumbs.
    esp_now_send(crumb_macs[0], (uint8_t *)&msg, sizeof(msg));
}

// ---------------------------------------------------------------------------
// ESP-NOW callbacks
// ---------------------------------------------------------------------------
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
    (void)info;
    (void)status;
}

void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
    const uint8_t *mac = info->src_addr;
    int rssi = (info->rx_ctrl != NULL) ? info->rx_ctrl->rssi : -99;

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
                    esp_now_send(crumb_macs[i], (uint8_t *)&reply, sizeof(reply));
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
    // Unknown MAC — print once so we know ESP-NOW is working but sender isn't a known crumb
    static unsigned long last_unknown_ms = 0;
    if ((unsigned long)(millis() - last_unknown_ms) >= 5000) {
        last_unknown_ms = millis();
        Serial.printf("[DEBUG] Recv from UNKNOWN MAC %02X:%02X:%02X:%02X:%02X:%02X  rssi=%d\n",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], rssi);
    }
}

// ---------------------------------------------------------------------------
// Setup (sender.ino-style: WiFi STA, ESP-NOW init, register callbacks, add peers)
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(200);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    WiFi.mode(WIFI_STA);
    delay(300);
    // Use a fixed channel so Bread and crumbs can hear each other (no "pairing" needed, just same channel)
    WiFi.setChannel(1);
    delay(100);

    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataRecv);

    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, crumb_macs[0], 6);  // temp for init
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
#if defined(WIFI_IF_STA)
    peerInfo.ifidx = WIFI_IF_STA;
#endif
    for (int i = 0; i < N; i++) {
        memcpy(peerInfo.peer_addr, crumb_macs[i], 6);
        esp_err_t addErr = esp_now_add_peer(&peerInfo);
        if (addErr != ESP_OK)
            Serial.printf("Failed to add peer %c: 0x%x\n", crumb_ids[i], (unsigned)addErr);
        else
            Serial.printf("Peer %c added OK\n", crumb_ids[i]);
    }
    delay(200);
    Serial.println("Bread ready. Drop order: D, C, B, A.");
    Serial.printf("[DEBUG] Bread MAC (crumbs must send here): %s\n", WiFi.macAddress().c_str());
    Serial.println("[DEBUG] If you never see 'Recv from crumb' — check: crumb powered? same channel? crumb has this Bread MAC?");
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
                st = DONE;
                break;
            }
            // Servo disabled for now
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
            int rssi = rssiForCid(c_id);
            bool nowWeak = (rssi < RSSI_DROP);

            if (nowWeak) {
                if (weak_start_ms == 0) weak_start_ms = millis();
            } else {
                weak_start_ms = 0;
            }

            // LED: on = signal good (reachable), off = bad
            if (nowWeak)
                digitalWrite(LED_PIN, LOW);
            else
                digitalWrite(LED_PIN, HIGH);

            bool receivedOnce = (idx >= 0 && last_seen_ms[idx] != 0);  // got at least one beacon from this crumb
            bool weakEnough = (weak_start_ms != 0) && (millis() - weak_start_ms >= WEAK_FOR_MS);
            bool cooldownOk = (millis() - last_drop_ms >= MIN_TIME_BETWEEN_DROPS);

            // Only advance when we've actually received from this crumb (so we're measuring real signal)
            if (weakEnough && cooldownOk && receivedOnce) {
                st = (next_idx < N) ? DROP_NEXT : DONE;
            }
            break;
        }

        case DONE:
            broadcastEmpty();
            break;

        case ERROR: {
            // Cooldown 2s before retry so we don't flood; print once
            if (last_error_ms == 0) last_error_ms = millis();
            if ((unsigned long)(millis() - last_error_ms) >= 2000) {
                Serial.println("  Retrying RELEASE to crumb...");
                last_error_ms = 0;
                st = DROP_NEXT;
            }
            break;
        }
    }

    if (millis() < in_pouch_blink_until_ms)
        digitalWrite(LED_PIN, (millis() / 100) % 2);
    else if (st != TRACK_SIGNAL)
        digitalWrite(LED_PIN, LOW);

    if ((unsigned long)(millis() - last_print_ms) >= 500) {
        last_print_ms = millis();
        int r = (c_id ? rssiForCid(c_id) : 0);
        const char *sig = (st == TRACK_SIGNAL && r >= RSSI_DROP) ? "good" : (st == TRACK_SIGNAL ? "bad" : "-");
        Serial.println("----------------------------------------");
        Serial.printf("  State:        %s\n", stateName(st));
        if (st == ERROR)
            Serial.println("  (RELEASE send failed — is crumb powered on? Wait 2s, then retry.)");
        if (st == TRACK_SIGNAL && c_id && last_seen_ms[idxFromId(c_id)] == 0)
            Serial.println("  (Waiting for beacon from crumb — is it powered and in range?)");
        Serial.printf("  Tracking:     crumb %c (last dropped)\n", c_id ? c_id : '-');
        Serial.printf("  Next to drop: crumb %c (index %d of 4)\n", next_idx < N ? crumb_ids[next_idx] : '-', next_idx);
        Serial.printf("  Signal:       %s (RSSI %d dBm)\n", sig, r);
        // Indices: 0=D, 1=C, 2=B, 3=A — display as A,B,C,D
        Serial.printf("  RSSI per crumb:  A=%d  B=%d  C=%d  D=%d dBm\n", last_rssi[3], last_rssi[2], last_rssi[1], last_rssi[0]);
        if (st == TRACK_SIGNAL && c_id) {
            int idx = idxFromId(c_id);
            unsigned long ago = (idx >= 0 && last_seen_ms[idx] != 0) ? (millis() - last_seen_ms[idx]) / 1000 : 999;
            Serial.printf("  Last beacon from crumb %c: %lu s ago\n", c_id, (unsigned long)ago);
        }
        Serial.println("----------------------------------------");
    }

    delay(50);
}
