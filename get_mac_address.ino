#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  delay(200);

  WiFi.mode(WIFI_STA);   // put ESP32 into Station mode
  WiFi.begin();          // start WiFi (no network connection needed for MAC)

  Serial.print("ESP32 MAC Address: ");
  Serial.println(WiFi.macAddress());
}

void loop() {}
