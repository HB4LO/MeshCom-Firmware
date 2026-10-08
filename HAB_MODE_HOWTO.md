# High-Altitude Balloon (HAB) Transponder & Cutdown Mode - Integration Guide

This guide explains how to add the **HAB Transponder & Cutdown Mode** into any fresh upstream clone or pull of the **MeshCom-Firmware** project.

---

## Architecture Overview

All balloon logic is isolated into two standalone files:
* `src/hab_balloon.h` (Configuration, whitelist, prototypes)
* `src/hab_balloon.cpp` (QSL auto-reply, drop GPIO triggers, security checks, OpenLog logging)

The core MeshCom codebase only requires **minimal 1-to-4 line hooks** wrapped in `#if defined(ENABLE_HAB_MODE)` across 7 files (totaling only ~60 lines added). This makes rebasing and pulling future upstream updates quick and conflict-free.

### Supported Remote Commands & Responses

All commands are received via direct APRS text messages:

| Command | Condition | Response Payload | Action |
| :--- | :--- | :--- | :--- |
| **`QSL?`** | Direct contact (`1 hop`) | `:<CALL>:QSL <lat><lat_c> <lon><lon_c> <alt>m` | Confirms 2-way contact with current coordinates and altitude. |
| **`$P_DROP`** | Authorized callsign in whitelist | `:<CALL>:$P_DROP OK <lat><lat_c> <lon><lon_c> <alt>m` | Triggers Primary/Parachute cutdown (`GPIO_DROP_P`). |
| **`$DROP_B`** | Authorized callsign in whitelist | `:<CALL>:$DROP_B OK <lat><lat_c> <lon><lon_c> <alt>m` | Triggers Secondary/Balloon cutdown (`GPIO_DROP_B`). |
| **`$P_DROP` / `$DROP_B`** | Unauthorized sender | `:<CALL>:<CMD> REJECTED` | Cutdown is refused; security alert logged. |

*(Note: Both the incoming direct message and the outgoing confirmation response are recorded to the OpenLog blackbox SD card).*

---

## Step 1: Copy the HAB Module Files

Place these two files into your `src/` folder:
* `src/hab_balloon.h`
* `src/hab_balloon.cpp`

*(Keep backup copies of these two files in your own branch or folder so you can simply copy them over whenever you clone a fresh upstream tree).*

---

## Step 2: Add the Core Hooks (7 Files)

### 1. `src/configuration_global.h`
**Location:** around line **274** (near `POSINFO_INTERVAL` definition).

**Replace:**
```cpp
#define POSINFO_INTERVAL 30 * 60           // POSINFO interval in minutes default 30 minutes
```

**With:**
```cpp
#include "hab_balloon.h"

#if defined(ENABLE_HAB_MODE) && (ENABLE_HAB_MODE == 1)
#define POSINFO_INTERVAL 1 * 60            // HAB: POSINFO interval default 1 minute
#else
#define POSINFO_INTERVAL 30 * 60           // POSINFO interval in minutes default 30 minutes
#endif
```

---

### 2. `src/esp32/esp32_main.cpp`
**Location:** around line **2054** (at the very end of `esp32setup()`, right before `esp_task_wdt_add(NULL);`).

**Add:**
```cpp
    #if defined(ENABLE_HAB_MODE) && (ENABLE_HAB_MODE == 1)
    hab_setup();
    #endif

    esp_task_wdt_add(NULL);
}
```

---

### 3. `src/main.cpp`
**Location:** around line **101** (inside the main `loop()` function, right after `esp32loop();`).

**Add:**
```cpp
  #if defined ESP32
    esp32loop();
  #endif

  #if defined(ENABLE_HAB_MODE) && (ENABLE_HAB_MODE == 1)
    hab_loop();
  #endif
```

---

### 4. `src/lora_functions.cpp`
There are 3 hook points in this file (2 for RX, 1 for TX):

#### Hook 4A: Incoming Direct Messages WITH Enquiry / ACK request (RX)
When phones/apps send direct messages, they include an enquiry ID `{nnn}` to request an ACK frame.
**Location:** around line **1779** (inside `OnRxDone()` inside the `else if(iEnqPos > 0)` branch after ACK is sent and payload stripped).

**Find:**
```cpp
queueDisplayText(aprsmsg, rssi, snr);

if(bDisplayVia)
    printfdeb("[MESHx]...SRC-PATH:%s ... DST-PATH:%s TEXT:%s\n", aprsmsg.msg_source_path, aprsmsg.msg_destination_path, aprsmsg.msg_payload);
```

**Add `hab_log_rx` and `hab_handle_rx_message` right after `queueDisplayText`:**
```cpp
queueDisplayText(aprsmsg, rssi, snr);

#if defined(ENABLE_HAB_MODE) && (ENABLE_HAB_MODE == 1)
hab_log_rx(aprsmsg);
hab_handle_rx_message(aprsmsg);
#endif

if(bDisplayVia)
    printfdeb("[MESHx]...SRC-PATH:%s ... DST-PATH:%s TEXT:%s\n", aprsmsg.msg_source_path, aprsmsg.msg_destination_path, aprsmsg.msg_payload);
```

#### Hook 4B: Incoming Direct Messages WITHOUT Enquiry (RX)
When a message arrives without an ACK enquiry number.
**Location:** around line **1800** (inside `OnRxDone()` in the trailing `else` branch).

**Find:**
```cpp
queueDisplayText(aprsmsg, rssi, snr);

if(bDisplayVia)
    printfdeb("[MESHx]...SRC-PATH:%s ... DST-PATH:%s TEXT:%s\n", aprsmsg.msg_source_path, aprsmsg.msg_destination_path, aprsmsg.msg_payload);
```

**Add `hab_log_rx` and `hab_handle_rx_message` right after `queueDisplayText`:**
```cpp
queueDisplayText(aprsmsg, rssi, snr);

#if defined(ENABLE_HAB_MODE) && (ENABLE_HAB_MODE == 1)
hab_log_rx(aprsmsg);
hab_handle_rx_message(aprsmsg);
#endif

if(bDisplayVia)
    printfdeb("[MESHx]...SRC-PATH:%s ... DST-PATH:%s TEXT:%s\n", aprsmsg.msg_source_path, aprsmsg.msg_destination_path, aprsmsg.msg_payload);
```

#### Hook 4C: Direct Message Response Logger (TX)
**Location:** around line **2890** (inside `doTX()` where outgoing packets are transmitted).
*(Note: `hab_log_tx` automatically filters to log ONLY direct messages sent to a specific callsign, e.g. QSL confirmations and `$P_DROP OK`, ignoring beacons and broadcasts).*

**Find:**
```cpp
if(msg_type_b_lora != 0x00) // 0x41 ACK
{
    tx_is_active = true;

    // you can transmit C-string or Arduino string up to
```

**Add `hab_log_tx` after `tx_is_active = true;`:**
```cpp
if(msg_type_b_lora != 0x00) // 0x41 ACK
{
    tx_is_active = true;

#if defined(ENABLE_HAB_MODE) && (ENABLE_HAB_MODE == 1)
    hab_log_tx(msg_type_b_lora, aprsmsg);
#endif

    // you can transmit C-string or Arduino string up to
```

---

### 5. `src/command_functions.cpp`
**Location:** around line **823** (inside `commandAction()` for `--postime`).

**Replace:**
```cpp
    if(commandCheck(msg_text+2, (char*)"postime ") == 0)    // sec
    {
        sscanf(msg_text+10, "%d", &meshcom_settings.node_postime);

        // minimum 3 Minuten
        if(meshcom_settings.node_postime < (5 * 60))
            meshcom_settings.node_postime = (5 * 60);
```

**With:**
```cpp
    if(commandCheck(msg_text+2, (char*)"postime ") == 0)    // sec
    {
        sscanf(msg_text+10, "%d", &meshcom_settings.node_postime);

#if defined(ENABLE_HAB_MODE) && (ENABLE_HAB_MODE == 1)
        // HAB Mode: fast beaconing (clamp to max 60s instead of min 5 min)
        if(meshcom_settings.node_postime > HAB_MAX_POSTIME_SEC)
            meshcom_settings.node_postime = HAB_MAX_POSTIME_SEC;
        else if(meshcom_settings.node_postime < 0)
            meshcom_settings.node_postime = 0;
#else
        // minimum 3 Minuten
        if(meshcom_settings.node_postime < (5 * 60))
            meshcom_settings.node_postime = (5 * 60);
#endif
```

---

### 6. `src/aprs_functions.cpp`
There are 2 hook points in this file to append `/A=00xxxx` (altitude in feet) to APRS comments:

#### Hook 6A: Standard APRS Beacon (`encodeLoRaAPRS`)
**Location:** around line **1208**.

**Replace:**
```cpp
    char catxt[sizeof(meshcom_settings.node_atxt)];
    snprintf(catxt, sizeof(catxt), "%s", meshcom_settings.node_atxt);
    size_t iatxt = charset_filter_apply(catxt, strlen(catxt), CHARSET_FILTER_STRIP_SEPARATORS);
    iatxt = charset_utf8_safe_truncate(catxt, iatxt, 25);
    catxt[iatxt] = 0x00;
```

**With:**
```cpp
    char catxt[64];
    snprintf(catxt, sizeof(catxt), "%s", meshcom_settings.node_atxt);
    size_t iatxt = charset_filter_apply(catxt, strlen(catxt), CHARSET_FILTER_STRIP_SEPARATORS);
    iatxt = charset_utf8_safe_truncate(catxt, iatxt, 25);
    catxt[iatxt] = 0x00;

#if defined(ENABLE_HAB_MODE) && (ENABLE_HAB_MODE == 1)
    if (alt != 0)
    {
        char calt[16];
        snprintf(calt, sizeof(calt), "/A=%06i", conv_fuss(alt));
        strncat(catxt, calt, sizeof(catxt) - strlen(catxt) - 1);
    }
#endif
```

#### Hook 6B: Compressed APRS Beacon (`encodeLoRaAPRScompressed`)
**Location:** around line **1322**.

**Replace:**
```cpp
    char catxt[sizeof(meshcom_settings.node_atxt)];
    snprintf(catxt, sizeof(catxt), "%s", strtmp.c_str());
    size_t iatxt = charset_filter_apply(catxt, strlen(catxt), CHARSET_FILTER_STRIP_SEPARATORS);
    iatxt = charset_utf8_safe_truncate(catxt, iatxt, 16);
    catxt[iatxt] = 0x00;
```

**With:**
```cpp
    char catxt[64];
    snprintf(catxt, sizeof(catxt), "%s", strtmp.c_str());
    size_t iatxt = charset_filter_apply(catxt, strlen(catxt), CHARSET_FILTER_STRIP_SEPARATORS);
    iatxt = charset_utf8_safe_truncate(catxt, iatxt, 16);
    catxt[iatxt] = 0x00;

#if defined(ENABLE_HAB_MODE) && (ENABLE_HAB_MODE == 1)
    if (alt != 0)
    {
        char calt[16];
        snprintf(calt, sizeof(calt), "/A=%06i", conv_fuss(alt));
        strncat(catxt, calt, sizeof(catxt) - strlen(catxt) - 1);
    }
#endif
```

---

### 7. `src/loop_functions.cpp`
**Location:** around lines **5907**, **5957**, and **6025** inside `setSMartBeaconing()`.

In these three spots, replace the hardcoded constant `POSINFO_INTERVAL` with the dynamic variable `posinfo_interval`:

1. At line **5907**:
```diff
-   printfdeb("%s [POSINFO]...Stationary -> Suppressing drift (Rate: %is)\n", getTimeString().c_str(), POSINFO_INTERVAL);
-   return POSINFO_INTERVAL;
+   printfdeb("%s [POSINFO]...Stationary -> Suppressing drift (Rate: %is)\n", getTimeString().c_str(), (int)posinfo_interval);
+   return posinfo_interval;
```

2. At line **5957**:
```diff
-   posinfo_last_rate = POSINFO_INTERVAL;
+   posinfo_last_rate = posinfo_interval;
```

3. At line **6025**:
```diff
-   gps_send_rate = POSINFO_INTERVAL;
+   gps_send_rate = posinfo_interval;
...
-   if(iGPSDEBUG > 0) printfdeb("%s [POSINFO]...WiFi connected & Stationary -> Suppressing drift (Rate: %i)\n", getTimeString().c_str(), POSINFO_INTERVAL);
-   return POSINFO_INTERVAL;
+   if(iGPSDEBUG > 0) printfdeb("%s [POSINFO]...WiFi connected & Stationary -> Suppressing drift (Rate: %i)\n", getTimeString().c_str(), (int)posinfo_interval);
+   return posinfo_interval;
```

---

## Step 3: Fast Integration via Git Patch (Recommended)

Instead of editing by hand, you can save the hook diff as a patch file `hab_hooks.patch` and apply it in 1 second:

```bash
git apply hab_hooks.patch
```

---

## Step 4: Configuration Reference (`src/hab_balloon.h`)

All parameters are configured in [`src/hab_balloon.h`](file:///c:/Users/Zappvion/Documents/PROJETS_ELO/MeshCom-Firmware/src/hab_balloon.h):

| Setting | Default | Description |
| :--- | :--- | :--- |
| `ENABLE_HAB_MODE` | `1` | Master switch (`1` = enabled, `0` = disabled) |
| `GPIO_DROP_P` | `15` | ESP32 GPIO for Primary/Parachute cutdown (`$P_DROP`) |
| `GPIO_DROP_B` | `2` | ESP32 GPIO for Secondary/Balloon cutdown (`$DROP_B`) |
| `HAB_DROP_ACTIVE_LEVEL` | `LOW` | Pin output level when triggered (`LOW` or `HIGH`) |
| `HAB_DROP_IDLE_LEVEL` | `HIGH` | Pin output level when idle |
| `HAB_DROP_PULSE_MS` | `5000` | Pulse duration in milliseconds before returning to idle. Set to `0` to latch permanently. |
| `HAB_AUTHORIZED_CALLS` | `{"HB4LO", "HB4LO-97", ...}` | Whitelist of calls allowed to trigger cutdown. Matches full call or base call (without SSID). If empty `{}`: allows all. |
| `HAB_QSL_DIRECT_ONLY` | `1` | Only reply to `QSL?` if heard directly (`msg_last_path_cnt == 1`) without digipeaters |
| `HAB_MAX_POSTIME_SEC` | `60` | Maximum beacon interval clamp for balloons (seconds) |
| `HAB_DEFAULT_CALLSIGN` | `"HB4LO-97"` | Default callsign assigned if node is unconfigured at boot |
| `ENABLE_OPENLOG` | `1` | Enable OpenLog serial blackbox logger |
| `OPENLOG_TX_PIN` | `13` | ESP32 TX pin connected to OpenLog RX |
| `OPENLOG_RX_PIN` | `25` | ESP32 RX pin connected to OpenLog TX |
| `OPENLOG_BAUD` | `9600` | OpenLog baud rate |

---

## Step 5: Build and Flash

Compile for TTGO T-Beam:
```bash
pio run -e ttgo_tbeam
```

Compile for ESP32 + EBYTE E22:
```bash
pio run -e E22_1262-DevKitC
```
