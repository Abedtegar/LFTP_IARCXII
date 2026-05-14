# Dokumentasi Robot Line Follower — IARC XII ITS

## Daftar Isi
- [Gambaran Umum](#gambaran-umum)
- [Komponen Hardware](#komponen-hardware)
- [Wiring & Pin Mapping](#wiring--pin-mapping)
- [State Machine](#state-machine)
- [Sistem Button Timing](#sistem-button-timing)
- [Struktur Program](#struktur-program)
- [Logika Line Following](#logika-line-following)
- [Tampilan OLED](#tampilan-oled)
- [Parameter Tuning (EEPROM)](#parameter-tuning-eeprom)
- [Troubleshooting](#troubleshooting)
- [Cara Build & Upload](#cara-build--upload)

---

## Gambaran Umum

Robot line follower ini dirancang untuk kompetisi **IARC XII ITS (Indonesian Autonomous Robot Competition)**. Robot membaca garis hitam di atas permukaan putih menggunakan sensor inframerah, lalu menggerakkan dua motor DC secara independen untuk mengikuti garis tersebut.

Fitur utama:
- Line following 5 sensor dengan prioritas belok kanan di persimpangan (**telusur kanan**)
- Tuning parameter kecepatan secara langsung via tombol fisik, tanpa perlu upload ulang
- Tampilan OLED real-time: status robot, visualisasi sensor, dan nilai parameter
- Parameter tersimpan otomatis ke **EEPROM** (tidak hilang saat power off)

**Microcontroller:** Arduino Nano (ATmega328P, 16 MHz)  
**Framework:** Arduino via PlatformIO  
**Bahasa:** C++

---

## Komponen Hardware

| Komponen | Spesifikasi | Fungsi |
|---|---|---|
| Arduino Nano | ATmega328P, 16 MHz | Microcontroller utama |
| L298N | Dual H-Bridge, max 2A/channel | Driver motor DC |
| BFD-1000 | 5 sensor IR + bump + proximity | Deteksi garis hitam |
| OLED SSD1306 | 128×64 px, I2C (0x3C) | Display parameter & sensor |
| Push Button | Normally Open, ke GND | Kontrol robot & tuning |
| Motor DC | 2 buah (kiri & kanan) | Penggerak roda |

---

## Wiring & Pin Mapping

### Motor Driver L298N → Arduino Nano

```
L298N          Arduino Nano
------         ------------
ENA      →     D3  (PWM) — kecepatan motor kiri
IN1      →     D4        — arah motor kiri (bit A)
IN2      →     D5        — arah motor kiri (bit B)
IN3      →     D7        — arah motor kanan (bit A)
IN4      →     D8        — arah motor kanan (bit B)
ENB      →     D9  (PWM) — kecepatan motor kanan
GND      →     GND
12V      →     sumber baterai
5V       →     5V Arduino (opsional, jika pakai jumper)
```

> Jika jumper 5V di L298N terpasang, regulator internal L298N dapat memberi daya ke Arduino Nano.

### Sensor BFD-1000 → Arduino Nano

```
BFD-1000       Arduino Nano    Posisi Fisik
--------        ------------    ------------
IR Sensor 1  →  A6  (ADC only) Kiri
IR Sensor 2  →  A3             Kiri-tengah
IR Sensor 3  →  A2             Tengah
IR Sensor 4  →  A1             Kanan-tengah
IR Sensor 5  →  A0             Kanan
GND          →  GND
VCC          →  5V
```

> **Penting — Pin A6:** Arduino Nano pin A6 adalah **ADC-only** (tidak bisa `digitalRead`).
> Program membaca A6 menggunakan `analogRead(A6) < 512` untuk mendeteksi LOW (garis).

### OLED SSD1306 → Arduino Nano (I2C)

```
OLED     Arduino Nano
----     ------------
VCC  →   5V
GND  →   GND
SDA  →   A4
SCL  →   A5
```

### Push Button → Arduino Nano

```
Button   Arduino Nano
------   ------------
Pin 1 →  D13 (INPUT_PULLUP)
Pin 2 →  GND
```

> Button ditekan = D13 LOW. Tombol **tidak perlu resistor eksternal** karena menggunakan internal pull-up.

---

## State Machine

Robot beroperasi dalam 5 state:

```
                 ┌─────────────────────────────────────┐
                 │                                     │
         TAP     ▼     TAP              HOLD           │
  ┌──────────► STOP ◄────────── RUNNING ──────► TUNING ┘
  │             │                  ▲               │
  │             │ TAP              │               │ LONG HOLD
  │             ▼                  │               │ (save EEPROM)
  │          RUNNING               │               ▼
  │             │                  │             STOP
  │        semua sensor            │
  │           aktif                │
  │             │                  │
  │             ▼                  │
  │         INTER_FWD              │
  │        (maju 150ms)            │
  │             │                  │
  │             ▼                  │
  └─────── INTER_TURN ────────────┘
        (belok kanan sampai
         sensor tengah aktif)
```

| State | Deskripsi |
|---|---|
| `STATE_STOP` | Robot diam, menunggu perintah |
| `STATE_RUNNING` | Robot mengikuti garis |
| `STATE_TUNING` | Mode tuning parameter, robot diam |
| `STATE_INTER_FWD` | Persimpangan terdeteksi, maju 150ms untuk posisioning |
| `STATE_INTER_TURN` | Belok kanan di persimpangan sampai sensor tengah menemukan garis |

---

## Sistem Button Timing

Satu tombol fisik (D13) menghasilkan 4 jenis event berdasarkan durasi tekan:

| Event | Cara | Durasi |
|---|---|---|
| **SINGLE TAP** | Tekan-lepas, tidak ada tap kedua dalam 400ms | < 600ms |
| **DOUBLE TAP** | Dua kali tekan-lepas masing-masing dalam 400ms | < 600ms × 2 |
| **HOLD** | Tahan lalu lepas | 600ms – 3000ms |
| **LONG HOLD** | Tahan sangat lama lalu lepas | ≥ 3000ms |

> Single Tap memiliki jeda deteksi 400ms untuk menunggu kemungkinan Double Tap.

### Aksi per State

| State | SINGLE TAP | DOUBLE TAP | HOLD | LONG HOLD |
|---|---|---|---|---|
| **STOP** | → RUNNING | — | → TUNING | — |
| **RUNNING** | → STOP | — | → TUNING | — |
| **TUNING** | Ubah nilai param (±) | Toggle arah `[+]`/`[-]` | Ganti ke param berikutnya | Simpan EEPROM → STOP |

### Cara Tuning Parameter

1. Dari state STOP atau RUNNING → **HOLD** tombol (~1 detik) → masuk TUNING
2. OLED menampilkan `[TUNING] [+]` dan cursor `>` pada parameter aktif
3. **SINGLE TAP** → nilai parameter naik/turun sesuai arah yang aktif
4. **DOUBLE TAP** → toggle arah: `[+]` (naik) ↔ `[-]` (turun)
5. **HOLD** → pindah ke parameter berikutnya (NORMAL → TURN → HARD → LOST → loop)
6. **LONG HOLD** (~3 detik) → simpan semua nilai ke EEPROM dan kembali ke STOP

---

## Struktur Program

```
src/main.cpp
│
├── Defines & Includes
│   ├── Arduino.h, Wire.h, Adafruit_GFX.h, Adafruit_SSD1306.h, EEPROM.h
│   ├── Pin motor  : ENA(D3), IN1(D4), IN2(D5), IN3(D7), IN4(D8), ENB(D9)
│   ├── Pin sensor : PIN_S1(A6), PIN_S2(A3), PIN_S3(A2), PIN_S4(A1), PIN_S5(A0)
│   └── Pin button : PIN_BTN(D13)
│
├── Enum RobotState
│   └── STOP, RUNNING, TUNING, INTER_FWD, INTER_TURN
│
├── Struct Param { value, step, minVal, maxVal }
│   └── params[4] = NORMAL, TURN, HARD, LOST
│
├── Enum ButtonEvent
│   └── NONE, SINGLE, DOUBLE, HOLD, LONG_HOLD
│
├── EEPROM
│   ├── loadParams()  — baca params saat boot, validasi magic bytes (0xAB 0xCD)
│   └── saveParams()  — tulis params saat exit TUNING dengan LONG HOLD
│
├── Motor
│   ├── motorKiri(int spd)   — spd: -255 s/d 255 (negatif = mundur)
│   ├── motorKanan(int spd)
│   ├── majuLurus(int spd)
│   └── stopMotor()
│
├── Sensor
│   └── readSensors(bool s[5])  — s[0]=kiri, s[2]=tengah, s[4]=kanan
│                                  A6 dibaca via analogRead + threshold 512
│
├── followLine(bool s[5])     — logika belok berdasarkan pola sensor
│
├── readButton()              — deteksi durasi tekan, return ButtonEvent
│
├── updateDisplay(bool s[5])  — render OLED (throttle 80ms)
│
├── setup()  — init pin, Wire 400kHz, OLED, loadParams, splash screen
│
└── loop()
    ├── readSensors()
    ├── readButton() → transisi state
    ├── aksi motor sesuai state
    └── updateDisplay() (setiap 80ms)
```

---

## Logika Line Following

### Cara Kerja Sensor

Sensor BFD-1000 mengeluarkan sinyal digital aktif LOW:
- `LOW` → sensor di atas **garis hitam** → `true` di program
- `HIGH` → sensor di atas **permukaan putih** → `false` di program

### Tabel Keputusan

Notasi: `■` = garis terdeteksi, `□` = tidak terdeteksi  
S1=kiri, S3=tengah, S5=kanan

```
S1  S2  S3  S4  S5   Kondisi                   Aksi
□   □   □   □   □    Garis hilang              Maju pelan (LOST)
■   ■   ■   ■   ■    Persimpangan              → STATE_INTER_FWD (telusur kanan)
□   □   ■   ■   ■    T-junction kanan          Belok kanan keras
□   □   □   □   ■    Jauh ke kanan             Belok kanan keras (HARD)
□   □   □   ■   □    Sedikit ke kanan          Belok kanan pelan (TURN)
□   □   ■   □   □    Tengah                    Maju lurus (NORMAL)
□   ■   □   □   □    Sedikit ke kiri           Belok kiri pelan (TURN)
■   □   □   □   □    Jauh ke kiri              Belok kiri keras (HARD)
```

### Telusur Kanan di Persimpangan (Right-Hand Rule)

Saat semua sensor aktif (persimpangan terdeteksi), robot menjalankan 2 tahap:

1. **INTER_FWD** — maju lurus selama 150ms agar robot terpusat di simpang
2. **INTER_TURN** — motor kiri maju penuh, motor kanan berhenti → belok kanan
   - Selesai saat sensor tengah (S3) kembali mendeteksi garis
   - Timeout otomatis 2 detik sebagai keamanan jika tidak ada garis

---

## Tampilan OLED

Layout 128×64 pixel, font size 1 (6×8px per karakter):

```
+──────────────────────────────+
│   [ ROBOT  STOP ]            │  ← y=0  header state
│ [██] [  ] [██] [  ] [  ]    │  ← y=12 visualisasi 5 sensor (20×12px)
│ >NORMAL: 255                 │  ← y=28 parameter 1 ("> " = sedang dipilih)
│  TURN  : 150                 │  ← y=37 parameter 2
│  HARD  : 100                 │  ← y=46 parameter 3
│  LOST  : 200                 │  ← y=55 parameter 4
+──────────────────────────────+
```

| Kotak sensor | Isi `fillRect` | Kosong `drawRect` |
|---|---|---|
| Tampilan | ████ (putih solid) | □ (outline saja) |
| Arti | Garis terdeteksi | Tidak ada garis |

Header berubah sesuai state:
- **STOP** → `[ ROBOT  STOP ]`
- **RUNNING** → `>>  RUNNING >>`
- **TUNING naik** → `[ TUNING ] [+]`
- **TUNING turun** → `[ TUNING ] [-]`

---

## Parameter Tuning (EEPROM)

| Index | Nama | Default | Step | Min | Max | Fungsi |
|---|---|---|---|---|---|---|
| 0 | NORMAL | 255 | 10 | 50 | 255 | Kecepatan maju lurus |
| 1 | TURN | 150 | 10 | 0 | 255 | Kecepatan roda lambat saat belok pelan |
| 2 | HARD | 100 | 10 | 0 | 200 | Kecepatan roda lambat saat belok keras |
| 3 | LOST | 200 | 10 | 50 | 255 | Kecepatan saat garis hilang |

### Panduan Tuning

| Gejala | Solusi |
|---|---|
| Robot terlalu cepat, susah belok | Turunkan NORMAL |
| Robot terlalu lambat | Naikkan NORMAL |
| Robot overshooting di tikungan | Turunkan HARD, naikkan TURN |
| Robot lambat koreksi arah | Turunkan TURN |
| Robot berhenti saat garis hilang | Naikkan LOST |
| Robot melaju terlalu jauh saat garis hilang | Turunkan LOST |

Parameter disimpan otomatis ke **EEPROM** saat keluar TUNING dengan LONG HOLD.
Nilai akan tetap ada meski robot dimatikan dan dinyalakan kembali.

---

## Troubleshooting

**Motor berputar terbalik (mundur saat harusnya maju)**
→ Tukar kabel motor di terminal OUT L298N, atau swap pin:
```cpp
#define IN1 5   // tukar IN1 dan IN2
#define IN2 4
```

**Robot belok ke arah yang salah**
→ Tukar kabel motor kiri dan kanan di L298N (OUT1/OUT2 ↔ OUT3/OUT4).

**Sensor tidak mendeteksi garis**
→ Cek LED indikator BFD-1000. Putar trimpot untuk kalibrasi sensitivitas.

**OLED tidak menyala**
→ Cek alamat I2C: scan dengan I2C Scanner sketch. Ubah `OLED_ADDR 0x3C` ke `0x3D` jika perlu.

**Robot tidak bergerak sama sekali**
→ Pastikan:
1. Baterai ≥ 7.4V untuk motor
2. GND antara Arduino dan L298N tersambung
3. Tombol D13 ditekan (single tap) untuk start dari state STOP

**EEPROM tidak menyimpan**
→ Pastikan keluar TUNING dengan **LONG HOLD** (tahan ≥ 3 detik). HOLD biasa hanya ganti parameter.

**Robot terus berputar di persimpangan**
→ Timeout 2 detik akan paksa keluar dari STATE_INTER_TURN. Cek sensor S3 (tengah) dan kalibrasi BFD-1000.

---

## Cara Build & Upload

### Konfigurasi Board (`platformio.ini`)
```ini
[env:nanoatmega328]
platform = atmelavr
board = nanoatmega328
framework = arduino
lib_deps =
  adafruit/Adafruit SSD1306@^2.5.7
  adafruit/Adafruit GFX Library@^1.11.5
```

Library akan didownload otomatis oleh PlatformIO saat build pertama kali.

### Build
```
Ctrl+Shift+B  atau klik tombol ✓ di toolbar PlatformIO
```

### Upload ke Arduino Nano
```
Klik tombol → (Upload) di toolbar PlatformIO
```
Atau via terminal:
```bash
pio run --target upload
```

---

## Update & Version Control

Repository: [github.com/Abedtegar/LFTP_IARCXII](https://github.com/Abedtegar/LFTP_IARCXII)

Untuk push perubahan ke GitHub:
```bash
git add .
git commit -m "deskripsi perubahan"
git push
```
