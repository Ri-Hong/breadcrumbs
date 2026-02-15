/*
 * Blink / pulse script: LED on D14, buzzer on D25.
 * LED pulses (fade in/out); active buzzer beeps in sync.
 */

#define LED_PIN    14   // D14
#define BUZZER_PIN 25   // D25

// PWM for LED pulse (ESP32 uses ledc)
#define LEDC_CHANNEL   0
#define LEDC_RESOLUTION 8   // 8-bit = 0..255
#define LEDC_FREQ      5000

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  ledcSetup(LEDC_CHANNEL, LEDC_FREQ, LEDC_RESOLUTION);
  ledcAttachPin(LED_PIN, LEDC_CHANNEL);
}

void loop() {
  // Pulse up (fade in) — buzzer ticks in sync
  for (int d = 0; d <= 255; d += 8) {
    ledcWrite(LEDC_CHANNEL, d);
    if (d == 128) {
      digitalWrite(BUZZER_PIN, HIGH);
      delay(30);
      digitalWrite(BUZZER_PIN, LOW);
    }
    delay(12);
  }
  digitalWrite(BUZZER_PIN, HIGH);
  delay(60);
  digitalWrite(BUZZER_PIN, LOW);
  delay(40);

  // Pulse down (fade out)
  for (int d = 255; d >= 0; d -= 8) {
    ledcWrite(LEDC_CHANNEL, d);
    delay(12);
  }

  delay(400);  // pause between pulses
}
