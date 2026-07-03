# 📡 RuralReach

> **An embedded LoRa‑to‑GSM bridge that brings SMS and push‑to‑talk voice to cellular dead zones in rural Rwanda — the person you contact just uses an ordinary phone on the normal mobile network.**

**Author:** Ramadhani Shafii Wanjenja
**Programme:** BSc. Software Engineering (Low‑Level Programming) — African Leadership University
**Supervisor:** Thadee Gatera
**Stack:** C (ESP‑IDF firmware) · Dart / Flutter (companion app)

- 🔗 **GitHub:** https://github.com/ramadhaniwanjenja/RuralREACH
- 🎥 **Video demo:** https://drive.google.com/file/d/1rypfK1TotT0imnOMQn0IE0Nynq8grNhq/view?usp=sharing

---

## 1. What it does

Many rural areas sit only a few kilometres from a cell tower but are blocked from coverage by ridges and hills. RuralReach bridges that gap with a low‑power **LoRa** radio link:

- A user in the **dead zone** pairs their phone (over Bluetooth) to a **Node**, and either types an SMS or holds a push‑to‑talk button to speak.
- The Node sends this over **LoRa (433 MHz)** to a **Gateway** placed where there *is* cell coverage.
- The Gateway pushes it into the **real mobile network** through a **SIM800L** GSM modem, so the recipient receives a **normal SMS** or a **normal phone call**.

Voice is **push‑to‑talk** (walkie‑talkie style, ~1–2 s latency) because LoRa is half‑duplex, and is compressed with **Codec2** (1300 bps) to fit LoRa's tiny bandwidth.

---

## 2. System architecture

```
 ┌──────────────┐   Bluetooth LE    ┌────────────────────────┐      LoRa 433 MHz      ┌───────────────────────────┐    GSM       ┌───────────────┐
 │  Phone + app │ ───────────────▶ │  NODE (ESP32‑S3+SX1262)│ ───────────────────▶ │ GATEWAY (ESP32‑S3+SX1262) │ ──────────▶ │ Recipient's   │
 │ (Flutter)    │ ◀─────────────── │  OLED · mic · speaker  │ ◀─────────────────── │ + SIM800L GSM modem       │ ◀────────── │ ordinary phone│
 └──────────────┘   replies/notify  └────────────────────────┘   voice / SMS / call  └───────────────────────────┘   SMS + call └───────────────┘
        (dead zone — no cell signal)                                              (has cell coverage)
```

- **Node** and **Gateway** are the *same* board type — a **Heltec WiFi LoRa 32 V3‑class** board (**ESP32‑S3 + SX1262 + 0.96" OLED**).
- The **app** talks to the Node over **Bluetooth LE** (Nordic UART Service), *not* Classic Bluetooth (the ESP32‑S3 has BLE only).

Design assets live in [`designs/`](designs/):

| File | Shows |
|---|---|
| `designs/architecture/RuralReach system architecture overview.png` | Overall system diagram |
| `designs/hardware/LoRa_node/LoRa_node.png` | Node circuit schematic |
| `designs/hardware/Gateway_node/Gatewaynode.png` | Gateway circuit schematic |
| `designs/figma/mobile_design.png` | App UI mock‑ups |

---

## 3. Current status

| Capability | Status |
|---|---|
| App ⇄ Node over Bluetooth LE | ✅ Working |
| SMS: app → Node → LoRa → Gateway → recipient's phone | ✅ Working (delivered) |
| SMS reply: recipient → Gateway → LoRa → Node OLED + app | ✅ Working |
| Voice over LoRa (Codec2 1300, push‑to‑talk) | ✅ Working |
| GSM voice **call setup** (Gateway dials the recipient) | ✅ Working (phone rings) |
| GSM voice **audio bridge** (voice into/out of the live call) | 🔧 In progress (analog `MIC`/`SPK` wiring) |
| AES‑128 payload encryption · CRC‑16 packet framing | ⏳ Planned |

---

## 4. Repository layout (related files)

```
RuralREACH/
├── README.md                     ← this file
│
├── ruralreach_app/               📱 Flutter companion app (Dart)
│   ├── lib/
│   │   ├── main.dart                     app entry point
│   │   ├── bluetooth/bt_service.dart     BLE link to the Node (flutter_blue_plus)
│   │   └── screens/
│   │       ├── home_screen.dart          connect + shows incoming replies
│   │       ├── sms_screen.dart           send an SMS
│   │       └── voice_screen.dart         place a call + push‑to‑talk
│   └── pubspec.yaml                       app dependencies
│
├── ruralreach_node/              🔌 Node firmware (ESP‑IDF, ESP32‑S3)
│   ├── main/main.c                       BLE · OLED · mic/speaker · LoRa · Codec2 voice
│   ├── components/
│   │   ├── lora_driver/                   SX1262 driver (TX + RX)
│   │   ├── codec2/                        vendored Codec2 speech codec
│   │   ├── packet/ · aes_crypto/          packet framing + AES (planned use)
│   │   └── audio_codec/
│   ├── sdkconfig.defaults                 target = esp32s3, BLE, 8 MB flash
│   └── CMakeLists.txt
│
├── ruralreach_gateway/           🛰️ Gateway firmware (ESP‑IDF, ESP32‑S3)
│   ├── main/main.c                       LoRa RX · SMS via SIM800L · voice decode · call control
│   ├── components/
│   │   ├── lora_driver/                   SX1262 driver (TX + RX)
│   │   └── codec2/                        vendored Codec2 speech codec
│   ├── sdkconfig.defaults                 target = esp32s3, 8 MB flash
│   └── CMakeLists.txt
│
└── designs/                      🎨 Schematics, architecture diagram, UI mock‑ups
```

---

## 5. Hardware you need

| Part | Used by |
|---|---|
| 2 × Heltec WiFi LoRa 32 **V3** (ESP32‑S3 + SX1262 + OLED), 433 MHz + antennas | Node **and** Gateway |
| INMP441 I²S microphone | Node (mic) |
| MAX98357A I²S amplifier + small speaker | Node (speaker) |
| SIM800L GSM module + SMS/voice‑capable SIM + **its own 3.7–4.2 V ≥2 A supply** | Gateway |
| Push button (optional external push‑to‑talk) | Node |
| An Android phone | runs the app |

**Node pin map** (Heltec V3): OLED `SDA17/SCL18/RST21/Vext36` · mic `SCK1/WS2/SD3` · speaker `BCLK4/LRC5/DIN6` · LoRa `NSS8/SCK9/MOSI10/MISO11/RST12/BUSY13/DIO1‑14` · PTT button `GPIO7` (or onboard PRG `GPIO0`).

**Gateway pin map:** OLED + LoRa same as above · SIM800L `TXD→GPIO48`, `RXD→GPIO46`, **common ground**, 9600 baud.

---

## 6. Prerequisites (toolchain)

| Tool | Version | For |
|---|---|---|
| [ESP‑IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/) | v5.1+ | Node & Gateway firmware |
| [Flutter SDK](https://docs.flutter.dev/get-started/install) | 3.x | Companion app |
| Android Studio / Android SDK | latest | Build & install the app on Android |

---

## 7. ▶️ Install and run the **app** (step by step)

The companion app is a Flutter project in [`ruralreach_app/`](ruralreach_app/).

1. **Install Flutter** (if you haven't): follow https://docs.flutter.dev/get-started/install, then verify:
   ```bash
   flutter doctor
   ```
   Fix anything it flags (Android toolchain, a connected device/emulator).

2. **Get the code and enter the app folder:**
   ```bash
   git clone https://github.com/ramadhaniwanjenja/RuralREACH.git
   cd RuralREACH/ruralreach_app
   ```

3. **Install the app's dependencies:**
   ```bash
   flutter pub get
   ```

4. **Connect an Android phone** (USB debugging on) or start an emulator, then confirm it's seen:
   ```bash
   flutter devices
   ```
   > Bluetooth LE needs a **real phone** — an emulator can't scan for the Node.

5. **Run the app:**
   ```bash
   flutter run
   ```
   To instead build an installable APK:
   ```bash
   flutter build apk --release
   # output: build/app/outputs/flutter-apk/app-release.apk
   ```

6. **On first launch, allow the permissions** the app requests (Nearby devices / Bluetooth, and Location on older Android).

7. **Use it:**
   - Power on the **Node** (it advertises as **`RuralReach`**).
   - On the app home screen, tap **“tap to connect”** → it finds and connects to the Node.
   - **Send SMS:** open *Send SMS*, enter number + message, tap *Send*.
   - **Voice call:** open *Voice Call*, enter the recipient's number, tap *Call*, then **hold** the big button to talk.
   - **Replies** from the recipient appear as a card on the home screen and on the Node's OLED.

---

## 8. Build and flash the **Node** firmware

```bash
cd RuralREACH/ruralreach_node

# one-time environment setup
. $IDF_PATH/export.sh            # or run install.sh first if IDF isn't set up

# first build only (sets the chip target from sdkconfig.defaults)
rm -f sdkconfig
idf.py set-target esp32s3
idf.py build

# flash + watch logs (use your Node board's serial port)
idf.py -p /dev/ttyUSB0 flash monitor
```

---

## 9. Build and flash the **Gateway** firmware

```bash
cd RuralREACH/ruralreach_gateway

. $IDF_PATH/export.sh
rm -f sdkconfig
idf.py set-target esp32s3
idf.py build

# flash the GATEWAY board (a different serial port from the Node)
idf.py -p /dev/ttyUSB1 flash monitor
```

> ⚡ **Power the SIM800L from its own 3.7–4.2 V, ≥2 A supply** (not the ESP32's pins) with a **common ground** — under‑powering it is the #1 cause of a SIM800L that won't answer AT commands.

---

## 10. Radio / link settings (must match on both boards)

| Setting | Value |
|---|---|
| Frequency | 433 MHz |
| Spreading factor | SF7 |
| Bandwidth | 125 kHz |
| Coding rate | 4/5 |
| Sync word | `0x1424` (SX1262) ≡ `0x12` (SX127x) |
| Voice codec | Codec2 **1300 bps** |

---

## 11. Security (design)

- **Encryption:** every SMS payload / voice frame to be AES‑128‑CBC encrypted before LoRa (`ruralreach_node/components/aes_crypto/`).
- **Integrity:** CRC‑16 checksum + sequence‑number replay cache per packet (`ruralreach_node/components/packet/`).
- **Privacy:** destination numbers SHA‑256 hashed in logs; message bodies and audio never stored.

---

## 12. Roadmap

- 🔧 Finish the analog **audio bridge** into the SIM800L call (`MIC`/`SPK` ↔ ESP32 via PDM + ADC).
- 🔒 Turn on AES‑128 + CRC‑16 on the wire.
- 🔁 Two‑way live voice tuning (jitter buffer, gain).

---

## 13. License & credits

Academic capstone project — African Leadership University.
Uses the open‑source **[Codec2](https://github.com/drowe67/codec2)** speech codec and the **ESP‑IDF** / **Flutter** frameworks.

**© Ramadhani Shafii Wanjenja**
