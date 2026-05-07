#include <Arduino.h>

// === PIN MOTOR (L298N) ===
#define ENA 5   // PWM - Kecepatan Motor Kiri
#define IN1 4   // Arah Motor Kiri
#define IN2 3   // Arah Motor Kiri
#define ENB 6   // PWM - Kecepatan Motor Kanan
#define IN3 7   // Arah Motor Kanan
#define IN4 8   // Arah Motor Kanan

// === PIN SENSOR (BFD-1000) ===
#define S1 A0   // Paling Kiri
#define S2 A1
#define S3 A2   // Tengah
#define S4 A3
#define S5 A4   // Paling Kanan

// === KONFIGURASI ===
// BFD-1000: LOW = mendeteksi garis (aktif LOW)
// Jika robot berperilaku terbalik, ganti LOW -> HIGH
#define LINE_DETECTED LOW

#define SPEED_NORMAL  160
#define SPEED_TURN    100
#define SPEED_HARD    50
#define SPEED_LOST    80

// === FUNGSI MOTOR ===
void motorKiri(int speed) {
  if (speed > 0) {
    digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
    analogWrite(ENA, constrain(speed, 0, 255));
  } else if (speed < 0) {
    digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH);
    analogWrite(ENA, constrain(-speed, 0, 255));
  } else {
    digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
    analogWrite(ENA, 0);
  }
}

void motorKanan(int speed) {
  if (speed > 0) {
    digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
    analogWrite(ENB, constrain(speed, 0, 255));
  } else if (speed < 0) {
    digitalWrite(IN3, LOW); digitalWrite(IN4, HIGH);
    analogWrite(ENB, constrain(-speed, 0, 255));
  } else {
    digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
    analogWrite(ENB, 0);
  }
}

void majuLurus(int speed) {
  motorKiri(speed);
  motorKanan(speed);
}

void stopMotor() {
  motorKiri(0);
  motorKanan(0);
}

// === SETUP ===
void setup() {
  pinMode(ENA, OUTPUT); pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(ENB, OUTPUT); pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  pinMode(S1, INPUT); pinMode(S2, INPUT); pinMode(S3, INPUT);
  pinMode(S4, INPUT); pinMode(S5, INPUT);

  stopMotor();
  delay(2000); // Jeda 2 detik sebelum mulai
}

// === LOOP UTAMA ===
void loop() {
  bool s1 = (digitalRead(S1) == LINE_DETECTED); // Paling Kiri
  bool s2 = (digitalRead(S2) == LINE_DETECTED);
  bool s3 = (digitalRead(S3) == LINE_DETECTED); // Tengah
  bool s4 = (digitalRead(S4) == LINE_DETECTED);
  bool s5 = (digitalRead(S5) == LINE_DETECTED); // Paling Kanan

  if (!s1 && !s2 && !s3 && !s4 && !s5) {
    // Garis hilang -> lanjut maju pelan
    majuLurus(SPEED_LOST);
  } else if (s1 && s2 && s3 && s4 && s5) {
    // Persimpangan / semua sensor di garis -> maju lurus
    majuLurus(SPEED_NORMAL);
  } else if (s3 && !s2 && !s4) {
    // Garis tepat di tengah -> maju lurus
    majuLurus(SPEED_NORMAL);
  } else if (s4 && !s5) {
    // Garis sedikit ke kanan -> belok kanan pelan
    motorKiri(SPEED_NORMAL);
    motorKanan(SPEED_TURN);
  } else if (s5 || (s4 && s5)) {
    // Garis jauh ke kanan -> belok kanan keras
    motorKiri(SPEED_NORMAL);
    motorKanan(SPEED_HARD);
  } else if (s2 && !s1) {
    // Garis sedikit ke kiri -> belok kiri pelan
    motorKiri(SPEED_TURN);
    motorKanan(SPEED_NORMAL);
  } else if (s1 || (s1 && s2)) {
    // Garis jauh ke kiri -> belok kiri keras
    motorKiri(SPEED_HARD);
    motorKanan(SPEED_NORMAL);
  } else {
    // Default -> maju lurus
    majuLurus(SPEED_NORMAL);
  }
}
