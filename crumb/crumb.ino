// Crumb: Hall sensor → LED flash + buzzer when magnet detected

#define HALL_ADC 34
#define LED_PIN 14
#define BUZZER_PIN 25

// Magnet detection threshold (volts from midpoint ~1.65V)
#define MAGNET_THRESHOLD_V 0.10f

#define FLASH_MS 80
#define PAUSE_MS 80

float adcToVolts(int adc) {
  return (adc / 4095.0f) * 3.3f;
}

// Returns true if magnet is still detected (above threshold)
bool readHallAboveThreshold() {
  long sum = 0;
  const int N = 16;
  for (int i = 0; i < N; i++) {
    sum += analogRead(HALL_ADC);
    delayMicroseconds(200);
  }
  float v = adcToVolts(sum / N);
  float delta = v - 1.65f;
  return fabs(delta) > MAGNET_THRESHOLD_V;
}

void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  analogReadResolution(12);
  analogSetPinAttenuation(HALL_ADC, ADC_11db);
}

void loop() {
  // Average samples to reduce noise
  long sum = 0;
  const int N = 32;
  for (int i = 0; i < N; i++) {
    sum += analogRead(HALL_ADC);
    delayMicroseconds(200);
  }
  int adc = sum / N;
  float v = adcToVolts(adc);
  float delta = v - 1.65f;

  if (fabs(delta) > MAGNET_THRESHOLD_V) {
    Serial.println("Magnet detected — flash + beep until magnet removed");
    while (readHallAboveThreshold()) {
      digitalWrite(LED_PIN, HIGH);
      digitalWrite(BUZZER_PIN, HIGH);
      delay(FLASH_MS);
      digitalWrite(LED_PIN, LOW);
      digitalWrite(BUZZER_PIN, LOW);
      delay(PAUSE_MS);
    }
    Serial.println("Magnet removed — stopping");
  }

  delay(50);
}
