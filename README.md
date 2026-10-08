# MeshCom-Firmware (HAB Edition)

> **Upstream Project Notice**: This project is a specialized fork of the official [MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) created by the Institute of Citizen Science for Space & Wireless communication ([www.icssw.org](https://icssw.org/en/meshcom/)). All core MeshCom LoRa/APRS mesh networking capabilities are preserved.

This edition adds an integrated **High Altitude Balloon (HAB) Transponder, Cutdown Controller & Blackbox Logger** mode for stratospheric amateur radio missions (e.g., HB4LO-97).

---

## Table of Contents
- [High Altitude Balloon (HAB) Mode](#high-altitude-balloon-hab-mode)
  - [Key HAB Features](#key-hab-features)
  - [HAB Over-The-Air Commands](#hab-over-the-air-commands)
  - [HAB Configuration (`src/hab_balloon.h`)](#hab-configuration-srchab_balloonh)
  - [OpenLog Serial Logger](#openlog-serial-logger)
  - [Upstream Maintenance & Porting](#upstream-maintenance--porting)
- [About MeshCom](#about-meshcom)
  - [Basic Functions](#basic-functions)
  - [Frequencies by Region](#frequencies-by-region)
  - [LoRa Modulation Parameters](#lora-modulation-parameters)
  - [APRS Protocol & Message Format](#aprs-protocol--message-format)
  - [Supported Hardware](#supported-hardware)
- [Building and Flashing](#building-and-flashing)
  - [PlatformIO Setup](#platformio-setup)
  - [Flashing ESP32 via CLI](#flashing-esp32-via-cli)
  - [OTA Updates](#ota-updates)
  - [Flashing RAK4631 (nRF52)](#flashing-rak4631-nrf52)
- [Icon Licensing](#icon-licensing)

---

# High Altitude Balloon (HAB) Mode

The HAB module provides an automated, on-air interactive transponder service for high-altitude balloons, enabling ground stations to verify RF contact while giving the flight team secure remote payload cutdown and descent release controls.

### Key HAB Features

1. **Automated QSL & Position Confirmation**:
   - Responds to incoming direct messages containing `QSL` or `QSL?`.
   - Replies with the balloon's real-time GPS coordinates and altitude in standard APRS format:  
     `<CALL>:QSL 46.1234N 006.1234E 1500m`
2. **Direct-Hop Protection (`HAB_QSL_DIRECT_ONLY`)**:
   - Ensures that only stations hearing the balloon directly (1 RF hop) receive a QSL reply.
   - Prevents balloon responses from flooding multi-hop mesh relays across several digipeaters.
3. **Dual Remote Cutdown / Drop Outputs**:
   - **Parachute / Primary Drop (`$P_DROP`)**: Controls `GPIO_DROP_P` (default GPIO 15).
   - **Balloon / Secondary Drop (`$DROP_B`)**: Controls `GPIO_DROP_B` (default GPIO 2).
   - Replies with execution confirmation and instant GPS position & altitude:  
     `<CALL>:$P_DROP OK 46.1234N 006.1234E 1500m`
4. **Whitelist Security for Cutdown**:
   - Cutdown commands are strictly validated against a callsign whitelist (`HAB_AUTHORIZED_CALLS`).
   - Supports both full callsigns (e.g., `HB9HIZ-1`) and base callsigns without SSID (`HB9HIZ`).
   - Unauthorized attempts are immediately rejected (`<CALL>:$P_DROP REJECTED`) and logged.
5. **Timed Pulse Safety Circuit (`HAB_DROP_PULSE_MS`)**:
   - When a cutdown command is triggered, the GPIO pin stays active for a specified duration (default **5 seconds**) and then automatically reverts to the idle state.
   - Prevents thermal burnout of nicrome hotwires, release relays, and onboard batteries.
6. **OpenLog MicroSD Blackbox Logging**:
   - Logs timestamped text activity (`[RX]`, `[TX]`, QSLs, and drop operations) to a hardware serial OpenLog recorder on dedicated pins without bogging down system tasks.
7. **Fast Position Reporting Enforcement**:
   - Automatically clamps position beacon intervals to a high cadence (e.g. maximum 60 seconds) regardless of default node or operator settings.
8. **Default Mission Callsign**:
   - Sets a mission callsign (e.g. `HB4LO-8`) automatically if the node is unconfigured.

---

### HAB Over-The-Air Commands

Send a direct APRS text message addressed to the balloon's callsign:

| Command | Permission | Description | Response Example |
| :--- | :--- | :--- | :--- |
| `QSL?` or `QSL` | Public | Requests contact confirmation & position | `<DEST>:QSL 46.1234N 006.1234E 1500m` |
| `$P_DROP` | Whitelist Only | Triggers primary / parachute drop pin | `<DEST>:$P_DROP OK 46.1234N 006.1234E 1500m` |
| `$DROP_B` | Whitelist Only | Triggers secondary / balloon drop pin | `<DEST>:$DROP_B OK 46.1234N 006.1234E 1500m` |

*(If an unauthorized station sends `$P_DROP` or `$DROP_B`, the node replies with `<DEST>:$P_DROP REJECTED` and no GPIO is activated).*

---

### HAB Configuration (`src/hab_balloon.h`)

All HAB parameters are configured in [src/hab_balloon.h](file:///c:/Users/Zappvion/Documents/PROJETS_ELO/MeshCom-Firmware/src/hab_balloon.h):

```c
// Master toggle (1 = enabled, 0 = disabled)
#define ENABLE_HAB_MODE 1

// Cutdown GPIO pins
#define GPIO_DROP_P 15       // Primary / Parachute Drop pin (e.g. GPIO 15 on T-Beam)
#define GPIO_DROP_B 2        // Secondary / Balloon Drop pin (e.g. GPIO 2 on T-Beam)

// Logic levels
#define HAB_DROP_ACTIVE_LEVEL LOW   // Active trigger level (LOW or HIGH)
#define HAB_DROP_IDLE_LEVEL   HIGH  // Normal flight idle level

// Pulse duration in ms (5000 = 5 seconds auto-shutoff; 0 = latch permanently)
#define HAB_DROP_PULSE_MS 5000

// Position interval clamp
#define HAB_MAX_POSTIME_SEC 60

// Direct-only filter for QSL confirmations (1 = direct 1 hop only, 0 = allow relays)
#define HAB_QSL_DIRECT_ONLY 1

// Whitelist of authorized command operators
static const char* const HAB_AUTHORIZED_CALLS[] = {
    "HB9HIZ-1",
    "HB9FOU-62"
};
```

---

### OpenLog Serial Logger

When `ENABLE_OPENLOG` is enabled (default on ESP32 targets):
- **Baud Rate**: 9600 baud (configurable via `OPENLOG_BAUD`)
- **ESP32 Pins**:
  - `OPENLOG_TX_PIN`: **GPIO 13** (connect to OpenLog RXI)
  - `OPENLOG_RX_PIN`: **GPIO 25** (connect to OpenLog TXO)
- **Output Format**:
  ```text
  --- MeshCom OpenLog (HAB Mode) Started ---
  [2026-10-06 15:32:34] [RX] HB9HIZ-1 -> HB4LO-97: QSL?
  [2026-10-06 15:32:35] [TX] HB4LO-97 -> HB9HIZ-1: HB9HIZ-1 :QSL 46.1234N 006.1234E 1500m
  [2026-10-06 15:34:02] [RX] HB9HIZ-1 -> HB4LO-97: $P_DROP
  [2026-10-06 15:34:03] [TX] HB4LO-97 -> HB9HIZ-1: HB9HIZ-1 :$P_DROP OK 46.1234N 006.1234E 1500m
  ```

---

### Upstream Maintenance & Porting

To maintain clean separation from upstream [MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) updates:
- All HAB logic lives inside dedicated files: [src/hab_balloon.h](file:///c:/Users/Zappvion/Documents/PROJETS_ELO/MeshCom-Firmware/src/hab_balloon.h) and [src/hab_balloon.cpp](file:///c:/Users/Zappvion/Documents/PROJETS_ELO/MeshCom-Firmware/src/hab_balloon.cpp).
- Core firmware integrations are isolated into lightweight hooks.
- See [HAB_MODE_HOWTO.md](file:///c:/Users/Zappvion/Documents/PROJETS_ELO/MeshCom-Firmware/HAB_MODE_HOWTO.md) for step-by-step instructions and [hab_hooks.patch](file:///c:/Users/Zappvion/Documents/PROJETS_ELO/MeshCom-Firmware/hab_hooks.patch) to re-apply hooks onto clean upstream releases with a single command:
  ```bash
  git apply hab_hooks.patch
  ```

---

# About MeshCom

MeshCom is an open Citizen Science project of the **Institute of Citizen Science for Space & Wireless communication** ([www.icssw.org](https://icssw.org/en/meshcom/)) designed to create a resilient, text-based communication tool for amateur radio operators. It utilizes LoRa™ modulation technology and the APRS protocol to establish a mesh network in the 70cm amateur band.

### Basic Functions
- Each node is identified by an Amateur Radio Callsign (with optional SSID).
- Short text messages can be broadcast to `*` (ALL) with gateway acknowledgements.
- Direct text messages can be sent to individual stations with end-to-end ACK.
- Nodes can act as gateways to HAMNET or the Internet via Wi-Fi.
- Self-building and self-healing mesh repeating over the air.
- Automatic status, position, and telemetry/weather reports.
- Support for onboard displays (OLED/E-Paper), Bluetooth LE smartphone apps, and USB serial CLI.

### Frequencies by Region
- **EU / Region 1**: 433.175 MHz
- **UK**: 439.9125 MHz (BW: 125 kHz, SF: 10)
- **Norway**: 433.925 MHz (BW: 125 kHz, SF: 10)
- **USA / Region 2**: 433.175 MHz
- **Africa**: 433.175 MHz

### LoRa Modulation Parameters
- **Spreading Factor (SF)**: 11
- **Bandwidth**: 250 kHz
- **Coding Rate (CR)**: 4/6

### APRS Protocol & Message Format
MeshCom frames follow the APRS Protocol Reference (AX.25-based):
- Text messages: `:|!MMMMMMMM|!HH|OE0XXX-99|>*|:|Text message|!00|!HW|!MOD|FCS#`
- Position reports: `!|!MMMMMMMM|!HH|OE0XXX-99|>*|!|4800.00|N|/|01600.00|E|#| BBB /A=HHHH|!00|!HW|!MOD|FCS#`

### Supported Hardware

| ID | Model / Hardware | MCU | LoRa Transceiver |
| :- | :--- | :--- | :--- |
| 4 | TTGO T-Beam 1.1 | ESP32 | SX1278 |
| 5 | TTGO T-Beam 1268 | ESP32 | SX1268 |
| 6 | TTGO T-Beam 0.7 | ESP32 | SX1262 |
| 7 | LilyGO T-Echo | nRF52840 | SX1262 |
| 8 | LilyGO T-Deck | ESP32-S3 | SX1262 |
| 9 | RAK Wireless WisBlock RAK4631 | nRF52840 | SX1262 |
| 10 | Heltec WiFi LoRa 32 v2 | ESP32 | SX1262 |
| 12 | TTGO T-Beam AXP2101 | ESP32 | SX1278 |
| 39 | Ebyte E22 + ESP32 DevKitC | ESP32 | SX1268 / SX1262 |
| 43 | Heltec WiFi LoRa 32 v3 | ESP32-S3 | SX1262 |
| 48 | Ebyte E22 + ESP32-S3 | ESP32-S3 | SX1268 / SX1262 |
| 51 | LilyGo T-Beam 1W | ESP32 | SX1262 |
| 54 | Heltec T114 | nRF52840 | SX1262 |

---

# Building and Flashing

### PlatformIO Setup
1. Install [Visual Studio Code](https://code.visualstudio.com/) and the [PlatformIO IDE](https://platformio.org/) extension.
2. Clone this repository and open the project directory in VS Code.
3. Select your target environment (e.g. `ttgo_tbeam` or `E22_1262-DevKitC`) in the PlatformIO status bar.
4. Click **Build** (`Ctrl+Alt+B`) or **Upload** (`Ctrl+Alt+U`).

### Flashing ESP32 via CLI
Using `esptool.py`:
```bash
# Erase flash (optional clean install):
esptool.py --port <PORT> erase_flash

# Flash standard ESP32 (e.g. T-Beam v1.1):
esptool.py -p <PORT> write_flash 0x1000 bootloader.bin 0xE000 otadata.bin 0x8000 partitions.bin 0x10000 safeboot.bin 0xC0000 firmware.bin

# Flash ESP32-S3 (e.g. Heltec v3 / T-Deck):
esptool.py -p <PORT> write_flash 0x0000 bootloader-s3.bin 0xE000 otadata.bin 0x8000 partitions.bin 0x10000 safeboot-s3.bin 0xC0000 firmware.bin
```

Alternatively, use the web flasher at [https://esptool.oevsv.at/](https://esptool.oevsv.at/).

### OTA Updates
- Send command `--ota-update` in the serial console or trigger via the web interface.
- Node reboots into OTA recovery mode.
- Access via `<CALLSIGN>.local` or connect to the `MeshCom-OTA` Wi-Fi AP at `192.168.4.1`.

### Flashing RAK4631 (nRF52)
1. **Via CLI (`adafruit-nrfutil`)**:
   ```bash
   pip3 install adafruit-nrfutil
   adafruit-nrfutil --verbose dfu serial --package wiscore_rak4631.zip -p <PORT> --singlebank --touch 1200
   ```
2. **Via UF2**:
   - Double-click the reset button on the RAK module to mount as a USB flash drive.
   - Drag and drop `firmware.uf2` into the mounted drive.

---

## Icon Licensing
MeshCom utilizes GPS and menu icons from [WpZoom](https://www.wpzoom.com), provided under the [Creative Commons Attribution-Share Alike 3.0 License](https://creativecommons.org/licenses/by-sa/3.0/).
