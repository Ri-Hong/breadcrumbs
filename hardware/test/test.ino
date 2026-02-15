/*
 * Blink: LED on D14, buzzer on D25. Simple on/off.
 */

#define LED_PIN    14   // D14
#define BUZZER_PIN 25   // D25

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
}

void loop() {
  digitalWrite(LED_PIN, HIGH);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(500);

  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  delay(500);
}
