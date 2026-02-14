#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#include "secrets.h"

// WiFi credentials from secrets.h (copy secrets.h.example → secrets.h)

const char* serverURL =
  "https://breadcrumbs-phi.vercel.app/api/message";

void setup() {
  Serial.begin(115200);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting");

  const unsigned long timeoutMs = 20000;  // 20 s
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - start > timeoutMs) {
      Serial.println("\nWiFi timeout. Check SSID, password, and that the hotspot is on.");
      for (;;) delay(1000);  // stop here
    }
  }

  Serial.println("\nWiFi connected!");
}

void sendMessage(String id, String msg, int hops) {
  WiFiClientSecure client;
  client.setInsecure(); // skip cert check for demo

  HTTPClient https;

  if (https.begin(client, serverURL)) {
    https.addHeader("Content-Type", "application/json");

    String json =
      "{\"id\":\"" + id + "\","
      "\"crumb_id\":\"C7\","
      "\"type\":\"SOS\","
      "\"message\":\"" + msg + "\","
      "\"hop_count\":" + String(hops) + "}";

    int code = https.POST(json);

    Serial.print("Response code: ");
    Serial.println(code);

    Serial.println(https.getString());

    https.end();
  }
}

void loop() {
  sendMessage("C7", "SOS: Help needed!", 5);
  delay(5000);
}
