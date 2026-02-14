#define BUZZER_PIN 25
#define LED_PIN 2

void setup() {
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
}

void loop() {
  digitalWrite(BUZZER_PIN, HIGH);  // turn buzzer ON
  digitalWrite(LED_PIN, HIGH);  // turn buzzer ON

  delay(300);

  digitalWrite(BUZZER_PIN, LOW);   // OFF
  digitalWrite(LED_PIN, LOW);   // OFF
  delay(300);
}
