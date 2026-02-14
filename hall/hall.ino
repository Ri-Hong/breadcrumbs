#define HALL_ADC 34

void setup() {
  Serial.begin(115200);

  // ESP32 ADC setup (Arduino core)
  analogReadResolution(12);                  // 0..4095
  analogSetPinAttenuation(HALL_ADC, ADC_11db); // lets you read up to ~3.3V
}

float adcToVolts(int adc) {
  // Approx conversion. ESP32 ADC isn't super linear; good enough for testing.
  return (adc / 4095.0f) * 3.3f;
}

void loop() {
  // average a few samples to reduce noise
  long sum = 0;
  const int N = 32;
  for (int i = 0; i < N; i++) {
    sum += analogRead(HALL_ADC);
    delayMicroseconds(200);
  }
  int adc = sum / N;
  float v = adcToVolts(adc);

  // midpoint is around VCC/2 ≈ 1.65V when powered at 3.3V
  float delta = v - 1.65f;

  Serial.print("ADC=");
  Serial.print(adc);
  Serial.print("  V=");
  Serial.print(v, 3);
  Serial.print("  delta=");
  Serial.println(delta, 3);

  // simple threshold detect (tune this)
  if (fabs(delta) > 0.10f) {  // 100mV threshold
    Serial.println(">>> Magnet nearby");
  }

  delay(100);
}
