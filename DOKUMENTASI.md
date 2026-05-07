# Dokumentasi Robot Line Follower — IARC XII ITS

## Daftar Isi
- [Gambaran Umum](#gambaran-umum)
- [Komponen Hardware](#komponen-hardware)
- [Wiring & Pin Mapping](#wiring--pin-mapping)
- [Struktur Program](#struktur-program)
- [Logika Line Following](#logika-line-following)
- [Konfigurasi & Tuning](#konfigurasi--tuning)
- [Troubleshooting](#troubleshooting)
- [Cara Build & Upload](#cara-build--upload)

---

## Gambaran Umum

Robot line follower ini dirancang untuk kompetisi **IARC XII ITS (Indonesian Autonomous Robot Competition)**. Robot membaca garis hitam di atas permukaan putih menggunakan sensor inframerah, lalu menggerakkan dua motor DC secara independen untuk mengikuti garis tersebut.

**Microcontroller:** Arduino Nano (ATmega328P, 16 MHz)  
**Framework:** Arduino via PlatformIO  
**Bahasa:** C++

---

## Komponen Hardware

| Komponen | Spesifikasi | Fungsi |
|---|---|---|
| Arduino Nano | ATmega328P, 16 MHz | Microcontroller utama |
| L298N | Dual H-Bridge, max 2A/channel | Driver motor DC |
| BFD-1000 | 5 sensor IR digital | Deteksi garis hitam |
| Motor DC | 2 buah (kiri & kanan) | Penggerak roda |

---

## Wiring & Pin Mapping

### Motor Driver L298N → Arduino Nano

```
L298N          Arduino Nano
------         ------------
ENA      →     D5  (PWM) — kecepatan motor kiri
IN1      →     D4        — arah motor kiri (bit A)
IN2      →     D3        — arah motor kiri (bit B)
ENB      →     D6  (PWM) — kecepatan motor kanan
IN3      →     D7        — arah motor kanan (bit A)
IN4      →     D8        — arah motor kanan (bit B)
GND      →     GND
12V      →     sumber baterai
5V       →     5V Arduino (opsional, jika pakai jumper)
```

> **Catatan:** Jika menggunakan jumper 5V di L298N, tegangan 5V dari regulator internal L298N dapat digunakan untuk power Arduino Nano.

### Sensor BFD-1000 → Arduino Nano

```
BFD-1000         Arduino Nano    Posisi Fisik
--------         ------------    ------------
IR Sensor 1  →   A0             Paling Kiri
IR Sensor 2  →   A1             Kiri
IR Sensor 3  →   A2             Tengah
IR Sensor 4  →   A3             Kanan
IR Sensor 5  →   A4             Paling Kanan
GND          →   GND
VCC          →   5V
```

> **Orientasi sensor:** Sensor 1 (A0) berada di sisi paling kiri robot saat robot menghadap maju.

---

## Struktur Program

```
src/main.cpp
│
├── Definisi Pin
│   ├── Motor: ENA, IN1, IN2, ENB, IN3, IN4
│   └── Sensor: S1 (A0) ... S5 (A4)
│
├── Konfigurasi
│   ├── LINE_DETECTED  — logika aktif sensor (LOW/HIGH)
│   ├── SPEED_NORMAL   — kecepatan maju lurus
│   ├── SPEED_TURN     — kecepatan saat belok pelan
│   ├── SPEED_HARD     — kecepatan saat belok keras
│   └── SPEED_LOST     — kecepatan saat garis hilang
│
├── Fungsi Motor
│   ├── motorKiri(int speed)   — kontrol motor kiri (-255 s/d 255)
│   ├── motorKanan(int speed)  — kontrol motor kanan (-255 s/d 255)
│   ├── majuLurus(int speed)   — gerak maju kedua motor
│   └── stopMotor()            — berhenti
│
├── setup()
│   ├── Inisialisasi semua pin
│   └── Delay 2 detik sebelum mulai
│
└── loop()
    ├── Baca 5 sensor digital
    └── Tentukan aksi berdasarkan pola sensor
```

### Fungsi `motorKiri(int speed)` dan `motorKanan(int speed)`

Kedua fungsi bekerja dengan parameter `speed` bertanda:
- `speed > 0` → motor maju
- `speed < 0` → motor mundur
- `speed = 0` → motor berhenti (brake)

Nilai speed di-constrain otomatis ke range `0–255` untuk `analogWrite`.

---

## Logika Line Following

### Cara Kerja Sensor

Sensor BFD-1000 mengeluarkan sinyal digital:
- `LOW (0)` → sensor di atas **garis hitam** (mendeteksi garis)
- `HIGH (1)` → sensor di atas **permukaan putih** (tidak ada garis)

Program membaca kelima sensor dan mengkonversinya menjadi boolean `true` (garis terdeteksi) / `false` (tidak ada garis).

### Tabel Keputusan

Notasi: `■` = garis terdeteksi, `□` = tidak terdeteksi

```
S1  S2  S3  S4  S5   Kondisi                  Aksi
□   □   □   □   □    Semua sensor off          Maju pelan (SPEED_LOST)
■   ■   ■   ■   ■    Persimpangan / lebar      Maju lurus (SPEED_NORMAL)
□   □   ■   □   □    Garis di tengah           Maju lurus (SPEED_NORMAL)
□   □   ■   ■   □    Garis sedikit ke kanan    Belok kanan pelan
□   □   □   □   ■    Garis jauh ke kanan       Belok kanan keras
□   ■   □   □   □    Garis sedikit ke kiri     Belok kiri pelan
■   □   □   □   □    Garis jauh ke kiri        Belok kiri keras
```

### Cara Belok

```
Belok KANAN  →  Motor kiri CEPAT, Motor kanan LAMBAT
Belok KIRI   →  Motor kiri LAMBAT, Motor kanan CEPAT
```

---

## Konfigurasi & Tuning

Semua parameter yang perlu disesuaikan ada di bagian atas `main.cpp`:

```cpp
#define LINE_DETECTED LOW    // Ganti ke HIGH jika sensor aktif HIGH

#define SPEED_NORMAL  160    // Kecepatan maju normal (0-255)
#define SPEED_TURN    100    // Kecepatan saat belok pelan
#define SPEED_HARD    50     // Kecepatan saat belok keras
#define SPEED_LOST    80     // Kecepatan saat garis hilang
```

### Panduan Tuning

| Masalah | Solusi |
|---|---|
| Robot terlalu cepat, susah belok | Turunkan `SPEED_NORMAL` |
| Robot terlalu lambat di jalur lurus | Naikkan `SPEED_NORMAL` |
| Robot overshooting saat belok | Turunkan `SPEED_HARD`, naikkan `SPEED_TURN` |
| Robot lambat koreksi arah | Turunkan `SPEED_TURN` |
| Robot berhenti saat garis hilang | Naikkan `SPEED_LOST` |

---

## Troubleshooting

**Motor berputar terbalik (mundur saat harusnya maju)**
→ Tukar kabel motor pada terminal OUT L298N, atau swap definisi pin:
```cpp
// Tukar IN1 dan IN2
#define IN1 3
#define IN2 4
```

**Robot belok ke arah yang salah**
→ Tukar kabel motor kiri dan kanan pada L298N.

**Sensor tidak mendeteksi garis**
→ Cek LED indikator di BFD-1000. Putar trimpot di sensor untuk kalibrasi sensitivitas.

**Sensor aktif HIGH (terdeteksi = HIGH, bukan LOW)**
→ Ubah konfigurasi:
```cpp
#define LINE_DETECTED HIGH
```

**Robot tidak bergerak sama sekali**
→ Pastikan:
1. Power supply cukup (baterai ≥ 7.4V untuk motor)
2. Jumper ENA dan ENB terpasang di L298N (atau sambungkan ke pin PWM)
3. Koneksi GND antara Arduino dan L298N tersambung

---

## Cara Build & Upload

Project ini menggunakan **PlatformIO** di VS Code.

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

### Konfigurasi Board (`platformio.ini`)
```ini
[env:nanoatmega328]
platform = atmelavr
board = nanoatmega328
framework = arduino
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
