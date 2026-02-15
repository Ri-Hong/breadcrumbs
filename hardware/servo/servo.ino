// Standalone servo driver (e.g. for crumb drop). Uses ESP32 LEDC (no external library).
// Two servos; each has its own start (hold) and end (drop) position for finetuning.

#define SERVO1_PIN 25          // GPIO for first servo signal
#define SERVO2_PIN 26          // GPIO for second servo signal

// Finetune start/end position for each servo (0–180).
#define SERVO1_ANGLE_HOLD 0    // Servo 1 start position (e.g. holding)
#define SERVO1_ANGLE_DROP 90   // Servo 1 end position (e.g. drop)
#define SERVO2_ANGLE_HOLD 180  // Servo 2 start position (opposite of servo 1)
#define SERVO2_ANGLE_DROP 90   // Servo 2 end position (opposite of servo 1)

#define SERVO_DROP_MS 400      // Time at drop angle before returning to hold

#define SERVO_LEDC_RES_BITS 16
#define SERVO_FREQ_HZ 50
#define SERVO_PULSE_MIN_US 1000   // 0 deg (tweak if your servo differs)
#define SERVO_PULSE_MAX_US 2000   // 180 deg

static void servoAttach() {
    ledcAttach(SERVO1_PIN, SERVO_FREQ_HZ, SERVO_LEDC_RES_BITS);
    ledcAttach(SERVO2_PIN, SERVO_FREQ_HZ, SERVO_LEDC_RES_BITS);
}

static uint32_t angleToDuty(int angle) {
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;
    uint32_t pulseUs = SERVO_PULSE_MIN_US + (unsigned long)angle * (SERVO_PULSE_MAX_US - SERVO_PULSE_MIN_US) / 180;
    return (unsigned long)pulseUs * (1UL << SERVO_LEDC_RES_BITS) / 20000;
}

// Set one servo (0–180). Use for finetuning or custom moves.
void setServo1Angle(int angle) {
    ledcWrite(SERVO1_PIN, angleToDuty(angle));
}
void setServo2Angle(int angle) {
    ledcWrite(SERVO2_PIN, angleToDuty(angle));
}

// Set both servos to the same logical angle (servo2 = 180 - angle). Optional; dropCrumb uses per-servo hold/drop.
void setServoAngle(int angle) {
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;
    ledcWrite(SERVO1_PIN, angleToDuty(angle));
    ledcWrite(SERVO2_PIN, angleToDuty(180 - angle));
}

// One-shot: move both to their DROP positions, hold, then return to their HOLD positions.
void dropCrumb() {
    setServo1Angle(SERVO1_ANGLE_DROP);
    setServo2Angle(SERVO2_ANGLE_DROP);
    delay(SERVO_DROP_MS);
    setServo1Angle(SERVO1_ANGLE_HOLD);
    setServo2Angle(SERVO2_ANGLE_HOLD);
}

void setup() {
    Serial.begin(115200);
    servoAttach();
    setServo1Angle(SERVO1_ANGLE_HOLD);
    setServo2Angle(SERVO2_ANGLE_HOLD);
    Serial.println("Servo ready. Finetune SERVO1_/SERVO2_ANGLE_HOLD and _DROP (0-180). dropCrumb() or setServo1Angle/setServo2Angle.");
}

void loop() {
    // Example: drop every 5 s. Replace with your own trigger (e.g. button, FSM, message).
    dropCrumb();
    delay(5000);
}
