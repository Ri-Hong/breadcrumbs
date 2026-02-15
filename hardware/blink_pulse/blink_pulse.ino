/*
 * Blink: LED on D34, buzzer on D5, built-in LED. Simple on/off.
 */

#define LED_PIN    32   // D32
#define BUZZER_PIN 5   // D5
#define LED_BUILTIN 2   // D2

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  digitalWrite(LED_PIN, HIGH);
  digitalWrite(BUZZER_PIN, HIGH);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(500);

  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_BUILTIN, LOW);
  delay(500);
}
