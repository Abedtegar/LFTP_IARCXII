#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>

// ── PIN MOTOR (L298N) ──────────────────────────────────────────────────
#define ENA  3    // PWM kecepatan motor kiri
#define IN1  4
#define IN2  5
#define IN3  7
#define IN4  8
#define ENB  9    // PWM kecepatan motor kanan

// ── PIN SENSOR BFD-1000 ────────────────────────────────────────────────
// S1 = A6 (ADC-only, tidak bisa digitalRead)
// S1..S5: LOW = mendeteksi garis hitam
#define PIN_S1  A6
#define PIN_S2  A3
#define PIN_S3  A2   // tengah
#define PIN_S4  A1
#define PIN_S5  A0

// ── PIN BUTTON ─────────────────────────────────────────────────────────
// INPUT_PULLUP: LOW = ditekan
#define PIN_BTN  13

// ── OLED SSD1306 128x64 ────────────────────────────────────────────────
#define SCREEN_W   128
#define SCREEN_H   64
#define OLED_ADDR  0x3C

// ── BUTTON TIMING ──────────────────────────────────────────────────────
#define HOLD_MS       600UL
#define LONG_HOLD_MS  3000UL
#define DTAP_WINDOW   400UL

// ── EEPROM ─────────────────────────────────────────────────────────────
#define MAGIC_0  0xAB
#define MAGIC_1  0xCD

// ══════════════════════════════════════════════════════════════════════

Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);

// ── State machine ──────────────────────────────────────────────────────
enum RobotState : uint8_t {
  STATE_STOP,
  STATE_RUNNING,
  STATE_TUNING,
  STATE_INTER_FWD,   // maju lurus melewati persimpangan
  STATE_INTER_TURN   // belok kanan di persimpangan
};
RobotState robotState = STATE_STOP;

// ── Parameter tuning ───────────────────────────────────────────────────
struct Param { int value, step, minVal, maxVal; };
Param params[4] = {
  {255, 10,  50, 255},  // 0: NORMAL — kecepatan maju lurus
  {150, 10,   0, 255},  // 1: TURN   — kecepatan saat belok pelan
  {100, 10,   0, 200},  // 2: HARD   — kecepatan saat belok keras
  {200, 10,  50, 255}   // 3: LOST   — kecepatan saat garis hilang
};
const char* const PNAME[] = {"NORMAL", "TURN  ", "HARD  ", "LOST  "};

int selParam  = 0;
int tuningDir = 1;   // +1 = naik, -1 = turun

// ── Button event ───────────────────────────────────────────────────────
enum ButtonEvent : uint8_t {
  BTN_NONE, BTN_SINGLE, BTN_DOUBLE, BTN_HOLD, BTN_LONG_HOLD
};

static bool     btnPrev      = HIGH;
static uint32_t btnPressAt   = 0;
static uint32_t btnReleaseAt = 0;
static uint8_t  tapCount     = 0;
static bool     pendingTap   = false;

// ── Intersection timer ─────────────────────────────────────────────────
static uint32_t interTimer = 0;

// ══════════════════════════════════════════════════════════════════════
// EEPROM
// ══════════════════════════════════════════════════════════════════════

void loadParams() {
  if (EEPROM.read(0) != MAGIC_0 || EEPROM.read(1) != MAGIC_1) return;
  int addr = 2;
  for (int i = 0; i < 4; i++) {
    int v;
    EEPROM.get(addr, v);
    params[i].value = constrain(v, params[i].minVal, params[i].maxVal);
    addr += sizeof(int);
  }
}

void saveParams() {
  EEPROM.write(0, MAGIC_0);
  EEPROM.write(1, MAGIC_1);
  int addr = 2;
  for (int i = 0; i < 4; i++) {
    EEPROM.put(addr, params[i].value);
    addr += sizeof(int);
  }
}

// ══════════════════════════════════════════════════════════════════════
// MOTOR
// ══════════════════════════════════════════════════════════════════════

void motorKiri(int spd) {
  if (spd > 0) {
    digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
    analogWrite(ENA, constrain(spd, 0, 255));
  } else if (spd < 0) {
    digitalWrite(IN1, LOW);  digitalWrite(IN2, HIGH);
    analogWrite(ENA, constrain(-spd, 0, 255));
  } else {
    digitalWrite(IN1, LOW);  digitalWrite(IN2, LOW);
    analogWrite(ENA, 0);
  }
}

void motorKanan(int spd) {
  if (spd > 0) {
    digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
    analogWrite(ENB, constrain(spd, 0, 255));
  } else if (spd < 0) {
    digitalWrite(IN3, LOW);  digitalWrite(IN4, HIGH);
    analogWrite(ENB, constrain(-spd, 0, 255));
  } else {
    digitalWrite(IN3, LOW);  digitalWrite(IN4, LOW);
    analogWrite(ENB, 0);
  }
}

void majuLurus(int spd) { motorKiri(spd); motorKanan(spd); }
void stopMotor()         { motorKiri(0);  motorKanan(0); }

// ══════════════════════════════════════════════════════════════════════
// SENSOR
// ══════════════════════════════════════════════════════════════════════

// s[0]=S1(kiri), s[2]=S3(tengah), s[4]=S5(kanan)
// true = mendeteksi garis
void readSensors(bool s[5]) {
  s[0] = (analogRead(PIN_S1) < 512);   // A6: ADC-only, LOW(<512) = garis
  s[1] = !digitalRead(PIN_S2);
  s[2] = !digitalRead(PIN_S3);
  s[3] = !digitalRead(PIN_S4);
  s[4] = !digitalRead(PIN_S5);
}

// ══════════════════════════════════════════════════════════════════════
// LINE FOLLOWING
// ══════════════════════════════════════════════════════════════════════

void followLine(bool s[5]) {
  int sN = params[0].value;  // NORMAL
  int sT = params[1].value;  // TURN
  int sH = params[2].value;  // HARD
  int sL = params[3].value;  // LOST

  if (!s[0] && !s[1] && !s[2] && !s[3] && !s[4]) {
    majuLurus(sL);                              // garis hilang
  } else if (s[2] && s[3] && s[4]) {
    motorKiri(sN); motorKanan(sH);              // T-junction kanan → ambil kanan
  } else if (s[4]) {
    motorKiri(sN); motorKanan(sH);              // jauh kanan → keras kanan
  } else if (s[3] && !s[4]) {
    motorKiri(sN); motorKanan(sT);              // kanan → belok kanan pelan
  } else if (s[2] && !s[1] && !s[3]) {
    majuLurus(sN);                              // tengah → lurus
  } else if (s[1] && !s[0]) {
    motorKiri(sT); motorKanan(sN);              // kiri → belok kiri pelan
  } else if (s[0]) {
    motorKiri(sH); motorKanan(sN);              // jauh kiri → keras kiri
  } else {
    majuLurus(sN);
  }
}

// ══════════════════════════════════════════════════════════════════════
// BUTTON TIMING
// ══════════════════════════════════════════════════════════════════════
//
// SINGLE TAP  (<600ms, tidak diikuti tap ke-2 dalam 400ms) → start/stop | ubah nilai
// DOUBLE TAP  (2 tap dalam 400ms)                          → toggle dir ±
// HOLD        (600ms – 3000ms)                             → masuk tuning | ganti param
// LONG HOLD   (≥3000ms)                                    → keluar tuning + simpan EEPROM

ButtonEvent readButton() {
  bool btnNow = digitalRead(PIN_BTN);  // LOW = ditekan (INPUT_PULLUP)
  ButtonEvent ev = BTN_NONE;

  if (btnPrev == HIGH && btnNow == LOW) {
    btnPressAt = millis();
  }

  if (btnPrev == LOW && btnNow == HIGH) {
    uint32_t held = millis() - btnPressAt;
    if (held >= LONG_HOLD_MS) {
      ev = BTN_LONG_HOLD;
      tapCount = 0; pendingTap = false;
    } else if (held >= HOLD_MS) {
      ev = BTN_HOLD;
      tapCount = 0; pendingTap = false;
    } else {
      tapCount++;
      btnReleaseAt = millis();
      pendingTap = true;
    }
  }

  if (pendingTap && (millis() - btnReleaseAt > DTAP_WINDOW)) {
    ev = (tapCount >= 2) ? BTN_DOUBLE : BTN_SINGLE;
    tapCount = 0; pendingTap = false;
  }

  btnPrev = btnNow;
  return ev;
}

// ══════════════════════════════════════════════════════════════════════
// DISPLAY
// ══════════════════════════════════════════════════════════════════════
//
// Layout 128x64:
//   y= 0 h= 8  — header state
//   y=12 h=12  — 5 sensor rectangles (20x12px) di x=4,28,52,76,100
//   y=28..55   — 4 baris parameter (spacing 9px)

void updateDisplay(bool s[5]) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Header
  display.setCursor(0, 0);
  switch (robotState) {
    case STATE_STOP:
      display.print(F("  [ ROBOT  STOP ]   "));
      break;
    case STATE_RUNNING:
    case STATE_INTER_FWD:
    case STATE_INTER_TURN:
      display.print(F("   >>  RUNNING >>   "));
      break;
    case STATE_TUNING:
      display.print(tuningDir > 0
        ? F("  [ TUNING ] [+]    ")
        : F("  [ TUNING ] [-]    "));
      break;
  }

  // Sensor rectangles
  static const uint8_t rx[] = {4, 28, 52, 76, 100};
  for (int i = 0; i < 5; i++) {
    if (s[i]) display.fillRect(rx[i], 12, 20, 12, SSD1306_WHITE);
    else       display.drawRect(rx[i], 12, 20, 12, SSD1306_WHITE);
  }

  // Parameter list
  for (int i = 0; i < 4; i++) {
    display.setCursor(0, 28 + i * 9);
    display.print((robotState == STATE_TUNING && i == selParam) ? '>' : ' ');
    display.print(PNAME[i]);
    display.print(':');
    int v = params[i].value;
    if (v < 100) display.print(' ');
    if (v <  10) display.print(' ');
    display.print(v);
  }

  display.display();
}

// ══════════════════════════════════════════════════════════════════════
// SETUP
// ══════════════════════════════════════════════════════════════════════

void setup() {
  pinMode(ENA, OUTPUT); pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(ENB, OUTPUT); pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  pinMode(PIN_S2, INPUT); pinMode(PIN_S3, INPUT);
  pinMode(PIN_S4, INPUT); pinMode(PIN_S5, INPUT);
  pinMode(PIN_BTN, INPUT_PULLUP);

  Wire.begin();
  Wire.setClock(400000);  // fast mode agar display tidak lambat

  stopMotor();
  loadParams();

  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(14, 22);
  display.print(F("IARC XII - LFTP"));
  display.setCursor(22, 34);
  display.print(F("--- READY ---"));
  display.display();
  delay(1500);
}

// ══════════════════════════════════════════════════════════════════════
// LOOP UTAMA
// ══════════════════════════════════════════════════════════════════════

void loop() {
  bool s[5];
  readSensors(s);

  ButtonEvent btn = readButton();

  // ── Transisi state ──────────────────────────────────────────────────
  switch (robotState) {
    case STATE_STOP:
      if      (btn == BTN_SINGLE) { robotState = STATE_RUNNING; }
      else if (btn == BTN_HOLD)   { robotState = STATE_TUNING; selParam = 0; tuningDir = 1; }
      break;

    case STATE_RUNNING:
    case STATE_INTER_FWD:
    case STATE_INTER_TURN:
      if      (btn == BTN_SINGLE) { stopMotor(); robotState = STATE_STOP; }
      else if (btn == BTN_HOLD)   { stopMotor(); robotState = STATE_TUNING; selParam = 0; tuningDir = 1; }
      break;

    case STATE_TUNING:
      if (btn == BTN_SINGLE) {
        // Ubah nilai parameter ke arah tuningDir, clamp ke min/max
        int& v = params[selParam].value;
        v = constrain(v + params[selParam].step * tuningDir,
                      params[selParam].minVal, params[selParam].maxVal);
      } else if (btn == BTN_DOUBLE) {
        tuningDir = -tuningDir;           // toggle arah + / -
      } else if (btn == BTN_HOLD) {
        selParam = (selParam + 1) % 4;   // ganti parameter
      } else if (btn == BTN_LONG_HOLD) {
        saveParams();                     // simpan ke EEPROM
        robotState = STATE_STOP;
      }
      break;
  }

  // ── Aksi motor ─────────────────────────────────────────────────────
  switch (robotState) {
    case STATE_STOP:
    case STATE_TUNING:
      stopMotor();
      break;

    case STATE_RUNNING:
      if (s[0] && s[1] && s[2] && s[3] && s[4]) {
        // Persimpangan → telusur kanan
        robotState = STATE_INTER_FWD;
        interTimer = millis();
        majuLurus(params[0].value);
      } else {
        followLine(s);
      }
      break;

    case STATE_INTER_FWD:
      // Maju lurus 150ms agar robot terpusat di persimpangan
      majuLurus(params[0].value);
      if (millis() - interTimer >= 150) {
        robotState = STATE_INTER_TURN;
        interTimer = millis();  // reset untuk timeout belok
      }
      break;

    case STATE_INTER_TURN:
      // Belok kanan: motor kiri maju, motor kanan berhenti
      motorKiri(params[0].value);
      motorKanan(0);
      // Selesai saat sensor tengah menemukan garis, atau timeout 2 detik
      if (s[2] || millis() - interTimer >= 2000) {
        robotState = STATE_RUNNING;
      }
      break;
  }

  // ── Update display (throttle 80ms ≈ 12fps) ─────────────────────────
  static uint32_t lastDisplay = 0;
  if (millis() - lastDisplay >= 80) {
    updateDisplay(s);
    lastDisplay = millis();
  }
}
