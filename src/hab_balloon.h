#ifndef _HAB_BALLOON_H_
#define _HAB_BALLOON_H_

#include <Arduino.h>
#include <aprs_structures.h>

// ===========================================================================
// High Altitude Balloon (HAB) Transponder & Cutdown Configuration
// ===========================================================================

// Master toggle for HAB mode (1 = enabled, 0 = disabled)
#ifndef ENABLE_HAB_MODE
#define ENABLE_HAB_MODE 1
#endif

#if defined(ENABLE_HAB_MODE) && (ENABLE_HAB_MODE == 1)

// Drop / Cutdown GPIO Pin Definitions
#ifndef GPIO_DROP_P
#define GPIO_DROP_P 15       // Parachute / Primary Drop pin (default GPIO 15)
#endif

#ifndef GPIO_DROP_B
#define GPIO_DROP_B 2        // Secondary / Balloon Drop pin (default GPIO 2)
#endif

// Logic level when cutdown is triggered
#ifndef HAB_DROP_ACTIVE_LEVEL
#define HAB_DROP_ACTIVE_LEVEL LOW
#endif

// Logic level when idle / normal flight
#ifndef HAB_DROP_IDLE_LEVEL
#define HAB_DROP_IDLE_LEVEL HIGH
#endif

// Duration of cutdown trigger pulse in milliseconds (e.g. 5000ms = 5 sec).
// Set to 0 to permanently latch active level.
#ifndef HAB_DROP_PULSE_MS
#define HAB_DROP_PULSE_MS 5000
#endif

// Default Callsign if node is unconfigured / factory default
#ifndef HAB_DEFAULT_CALLSIGN
#define HAB_DEFAULT_CALLSIGN "HB4LO-8"
#endif

// Maximum allowable postime in seconds (enforces fast position reports)
#ifndef HAB_MAX_POSTIME_SEC
#define HAB_MAX_POSTIME_SEC 60
#endif

// OpenLog Serial Blackbox Configuration (ESP32)
#ifndef ENABLE_OPENLOG
#define ENABLE_OPENLOG 1
#endif

#ifndef OPENLOG_TX_PIN
#define OPENLOG_TX_PIN 13    // ESP32 TX pin connected to OpenLog RX
#endif

#ifndef OPENLOG_RX_PIN
#define OPENLOG_RX_PIN 25    // ESP32 RX pin connected to OpenLog TX
#endif

#ifndef OPENLOG_BAUD
#define OPENLOG_BAUD 9600
#endif

// Only answer "QSL?" if packet was heard directly (1 hop)
#ifndef HAB_QSL_DIRECT_ONLY
#define HAB_QSL_DIRECT_ONLY 1
#endif

// List of callsigns authorized to trigger remote cutdown ($P_DROP, $DROP_B).
// Matches exact full callsign (e.g. "HB4LO-97") or base callsign without SSID ("HB4LO").
// If list is empty (0 elements), all senders are accepted.
static const char* const HAB_AUTHORIZED_CALLS[] = {
    "HB9HIZ-1",
    "HB9FOU-62"
    // Add additional callsigns here
};

// ===========================================================================
// Public HAB Functions
// ===========================================================================

void hab_setup();
void hab_loop();
bool hab_handle_rx_message(const struct aprsMessage &aprsmsg);
void hab_log_rx(const struct aprsMessage &aprsmsg);
void hab_log_tx(uint16_t msg_type, const struct aprsMessage &aprsmsg);
bool hab_is_call_authorized(const char* call);

#endif // ENABLE_HAB_MODE

#endif // _HAB_BALLOON_H_
