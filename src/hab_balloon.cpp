#include "hab_balloon.h"

#if defined(ENABLE_HAB_MODE) && (ENABLE_HAB_MODE == 1)

#include <configuration.h>
#include <printfdeb_functions.h>
#include <aprs_functions.h>
#include <loop_functions.h>
#include <loop_functions_extern.h>
#include <via_functions.h>
#include <msgid_counter.h>
#include <counters_store.h>
#include <mc_text.h>
#include <dedup_functions.h>

#if defined(ESP32) && defined(ENABLE_OPENLOG) && (ENABLE_OPENLOG == 1)
#include <HardwareSerial.h>
static HardwareSerial OpenLogSerial(2);
static bool s_openlog_initialized = false;
#endif

#if (HAB_DROP_PULSE_MS > 0)
static uint32_t s_drop_p_timer = 0;
static uint32_t s_drop_b_timer = 0;
#endif

/**
 * @brief Checks if a callsign is in the authorized whitelist for remote cutdown.
 */
bool hab_is_call_authorized(const char* sender_call)
{
    if (sender_call == nullptr || sender_call[0] == '\0')
        return false;

    size_t num_auth = sizeof(HAB_AUTHORIZED_CALLS) / sizeof(HAB_AUTHORIZED_CALLS[0]);
    if (num_auth == 0)
        return true; // No whitelist configured -> allow all

    // Extract base call from sender (strip -SSID if present)
    char sender_base[21];
    snprintf(sender_base, sizeof(sender_base), "%s", sender_call);
    char *dash = strchr(sender_base, '-');
    if (dash != nullptr)
        *dash = '\0';

    for (size_t i = 0; i < num_auth; i++)
    {
        const char *auth = HAB_AUTHORIZED_CALLS[i];
        if (auth == nullptr || auth[0] == '\0')
            continue;

        // Exact match with full call (e.g. "HB4LO-97" == "HB4LO-97")
        if (strcasecmp(sender_call, auth) == 0)
            return true;

        // Base call match (e.g. "HB4LO" in auth list matches "HB4LO-1")
        if (strcasecmp(sender_base, auth) == 0)
            return true;
    }

    return false;
}

/**
 * @brief Case-insensitive substring search helper.
 */
static bool hab_str_contains_nocase(const char *haystack, const char *needle)
{
    if (!haystack || !needle) return false;
    size_t nlen = strlen(needle);
    if (nlen == 0) return true;
    size_t hlen = strlen(haystack);
    if (hlen < nlen) return false;
    for (size_t i = 0; i <= hlen - nlen; i++)
    {
        if (strncasecmp(&haystack[i], needle, nlen) == 0)
            return true;
    }
    return false;
}

/**
 * @brief Transmit an APRS direct message reply to the specified station.
 */
static void hab_send_reply(const char *dest_call, const char *reply_text)
{
    struct aprsMessage replymsg;
    initAPRS(replymsg, ':');

    // Generate a new message ID
    replymsg.msg_id = ((_GW_ID & 0x3FFFFF) << 10) | (meshcom_settings.node_msgid & 0x3FF);

    mcSet(replymsg.msg_source_path, sizeof(replymsg.msg_source_path), meshcom_settings.node_call);
    mcSet(replymsg.msg_destination_path, sizeof(replymsg.msg_destination_path), dest_call);
    mcSet(replymsg.msg_destination_call, sizeof(replymsg.msg_destination_call), dest_call);
    mcSet(replymsg.msg_payload, sizeof(replymsg.msg_payload), reply_text);

    meshcom_settings.node_msgid = msgIdAdvance(meshcom_settings.node_msgid);
    if(msgIdNeedsPersist(meshcom_settings.node_msgid))
        countersSave();

    insertOwnTx(replymsg.msg_id);

    if(bGATEWAY && meshcom_settings.node_hasIPaddress)
        addLoraRxBuffer(replymsg.msg_id, true);
    else
        addLoraRxBuffer(replymsg.msg_id, false);

    checkVia(replymsg);

    uint8_t msg_buffer[MAX_MSG_LEN_PHONE];
    encodeAPRS(msg_buffer, replymsg);

    addTxRingEntryOnce(msg_buffer, (uint16_t)replymsg.msg_len, "hab_reply");
}

/**
 * @brief Initialize HAB hardware, pins, OpenLog, and defaults.
 */
void hab_setup()
{
    printfdeb("\n[HAB] Initializing High Altitude Balloon Mode...\n");

    // 1. Initialize Cutdown / Drop GPIO pins
#if defined(GPIO_DROP_P)
    pinMode(GPIO_DROP_P, OUTPUT);
    digitalWrite(GPIO_DROP_P, HAB_DROP_IDLE_LEVEL);
    printfdeb("[HAB] GPIO_DROP_P (pin %d) initialized to IDLE (%s)\n", GPIO_DROP_P,
              HAB_DROP_IDLE_LEVEL == HIGH ? "HIGH" : "LOW");
#endif

#if defined(GPIO_DROP_B)
    pinMode(GPIO_DROP_B, OUTPUT);
    digitalWrite(GPIO_DROP_B, HAB_DROP_IDLE_LEVEL);
    printfdeb("[HAB] GPIO_DROP_B (pin %d) initialized to IDLE (%s)\n", GPIO_DROP_B,
              HAB_DROP_IDLE_LEVEL == HIGH ? "HIGH" : "LOW");
#endif

    // 2. Adopt default HAB callsign if unconfigured
    if (isUnconfiguredCall(meshcom_settings.node_call))
    {
        mcSet(meshcom_settings.node_call, sizeof(meshcom_settings.node_call), HAB_DEFAULT_CALLSIGN);
        printfdeb("[HAB] Node callsign set to default: %s\n", meshcom_settings.node_call);
    }

    // 3. Enforce maximum postime interval (must not exceed HAB_MAX_POSTIME_SEC)
    if (meshcom_settings.node_postime == 0 || meshcom_settings.node_postime > HAB_MAX_POSTIME_SEC)
    {
        meshcom_settings.node_postime = HAB_MAX_POSTIME_SEC;
        posinfo_interval = HAB_MAX_POSTIME_SEC;
        printfdeb("[HAB] Fast position reporting interval enforced: %d seconds\n", posinfo_interval);
    }

    // 4. Initialize OpenLog serial interface
#if defined(ESP32) && defined(ENABLE_OPENLOG) && (ENABLE_OPENLOG == 1)
    OpenLogSerial.begin(OPENLOG_BAUD, SERIAL_8N1, OPENLOG_RX_PIN, OPENLOG_TX_PIN);
    delay(100);
    OpenLogSerial.println("\n--- MeshCom OpenLog (HAB Mode) Started ---");
    s_openlog_initialized = true;
    printfdeb("[HAB] OpenLog Serial initialized on TX:%d RX:%d @ %d baud\n",
              OPENLOG_TX_PIN, OPENLOG_RX_PIN, OPENLOG_BAUD);
#endif

    printfdeb("[HAB] Initialization complete.\n");
}

/**
 * @brief Periodic loop handler (handles auto-reset of timed drop pulses).
 */
void hab_loop()
{
#if (HAB_DROP_PULSE_MS > 0)
    if (s_drop_p_timer > 0 && millis() >= s_drop_p_timer)
    {
        digitalWrite(GPIO_DROP_P, HAB_DROP_IDLE_LEVEL);
        s_drop_p_timer = 0;
        printfdeb("[HAB] Drop P pulse ended -> reset GPIO %d to IDLE\n", GPIO_DROP_P);
    }

    if (s_drop_b_timer > 0 && millis() >= s_drop_b_timer)
    {
        digitalWrite(GPIO_DROP_B, HAB_DROP_IDLE_LEVEL);
        s_drop_b_timer = 0;
        printfdeb("[HAB] Drop B pulse ended -> reset GPIO %d to IDLE\n", GPIO_DROP_B);
    }
#endif
}

/**
 * @brief Check incoming direct message for HAB commands (QSL?, $P_DROP, $DROP_B).
 * @return true if a HAB command was matched and handled, false otherwise.
 */
bool hab_handle_rx_message(const struct aprsMessage &aprsmsg)
{
    printfdeb("[HAB] RX DM from %s: \"%s\" (hops=%d)\n",
              aprsmsg.msg_source_call, aprsmsg.msg_payload, (int)aprsmsg.msg_last_path_cnt);

    // 1. QSL confirmation request (case-insensitive check for "QSL")
    if (hab_str_contains_nocase(aprsmsg.msg_payload, "QSL"))
    {
#if (HAB_QSL_DIRECT_ONLY == 1)
        if (aprsmsg.msg_last_path_cnt > 1)
        {
            printfdeb("[HAB] QSL request from %s ignored (hops=%d > 1, direct only configured)\n",
                      aprsmsg.msg_source_call, (int)aprsmsg.msg_last_path_cnt);
            return false;
        }
#endif
        char reply_text[80];
        snprintf(reply_text, sizeof(reply_text), "%-9.9s:QSL %.4lf%c %.4lf%c %im",
                 aprsmsg.msg_source_call,
                 meshcom_settings.node_lat,
                 meshcom_settings.node_lat_c,
                 meshcom_settings.node_lon,
                 meshcom_settings.node_lon_c,
                 meshcom_settings.node_alt);

        printfdeb("[HAB] QSL from %s -> sending reply: %s\n", aprsmsg.msg_source_call, reply_text);
        hab_send_reply(aprsmsg.msg_source_call, reply_text);
        return true;
    }
    // 2. Parachute / Primary Drop command
    else if (hab_str_contains_nocase(aprsmsg.msg_payload, "$P_DROP"))
    {
        if (!hab_is_call_authorized(aprsmsg.msg_source_call))
        {
            printfdeb("[HAB-SECURITY] UNAUTHORIZED $P_DROP attempt rejected from call: %s!\n", aprsmsg.msg_source_call);
            char reply_text[80];
            snprintf(reply_text, sizeof(reply_text), "%-9.9s:$P_DROP REJECTED", aprsmsg.msg_source_call);
            hab_send_reply(aprsmsg.msg_source_call, reply_text);
            return true;
        }

#if defined(GPIO_DROP_P)
        pinMode(GPIO_DROP_P, OUTPUT);
        digitalWrite(GPIO_DROP_P, HAB_DROP_ACTIVE_LEVEL);
#if (HAB_DROP_PULSE_MS > 0)
        s_drop_p_timer = millis() + HAB_DROP_PULSE_MS;
#endif
        printfdeb("[HAB-DROP] $P_DROP triggered by %s -> GPIO %d set to ACTIVE (%s)\n",
                  aprsmsg.msg_source_call, GPIO_DROP_P, (HAB_DROP_ACTIVE_LEVEL == LOW ? "LOW" : "HIGH"));
#endif

        char reply_text[90];
        snprintf(reply_text, sizeof(reply_text), "%-9.9s:$P_DROP OK %.4lf%c %.4lf%c %im",
                 aprsmsg.msg_source_call,
                 meshcom_settings.node_lat,
                 meshcom_settings.node_lat_c,
                 meshcom_settings.node_lon,
                 meshcom_settings.node_lon_c,
                 meshcom_settings.node_alt);
        hab_send_reply(aprsmsg.msg_source_call, reply_text);
        return true;
    }
    // 3. Balloon / Secondary Drop command
    else if (hab_str_contains_nocase(aprsmsg.msg_payload, "$DROP_B"))
    {
        if (!hab_is_call_authorized(aprsmsg.msg_source_call))
        {
            printfdeb("[HAB-SECURITY] UNAUTHORIZED $DROP_B attempt rejected from call: %s!\n", aprsmsg.msg_source_call);
            char reply_text[80];
            snprintf(reply_text, sizeof(reply_text), "%-9.9s:$DROP_B REJECTED", aprsmsg.msg_source_call);
            hab_send_reply(aprsmsg.msg_source_call, reply_text);
            return true;
        }

#if defined(GPIO_DROP_B)
        pinMode(GPIO_DROP_B, OUTPUT);
        digitalWrite(GPIO_DROP_B, HAB_DROP_ACTIVE_LEVEL);
#if (HAB_DROP_PULSE_MS > 0)
        s_drop_b_timer = millis() + HAB_DROP_PULSE_MS;
#endif
        printfdeb("[HAB-DROP] $DROP_B triggered by %s -> GPIO %d set to ACTIVE (%s)\n",
                  aprsmsg.msg_source_call, GPIO_DROP_B, (HAB_DROP_ACTIVE_LEVEL == LOW ? "LOW" : "HIGH"));
#endif

        char reply_text[90];
        snprintf(reply_text, sizeof(reply_text), "%-9.9s:$DROP_B OK %.4lf%c %.4lf%c %im",
                 aprsmsg.msg_source_call,
                 meshcom_settings.node_lat,
                 meshcom_settings.node_lat_c,
                 meshcom_settings.node_lon,
                 meshcom_settings.node_lon_c,
                 meshcom_settings.node_alt);
        hab_send_reply(aprsmsg.msg_source_call, reply_text);
        return true;
    }

    return false;
}

/**
 * @brief Log received text packet to OpenLog serial.
 */
void hab_log_rx(const struct aprsMessage &aprsmsg)
{
#if defined(ESP32) && defined(ENABLE_OPENLOG) && (ENABLE_OPENLOG == 1)
    if (!s_openlog_initialized)
        return;

    OpenLogSerial.printf("[%s %s] [RX] %s -> %s: %s\n",
                         getDateString().c_str(),
                         getTimeString().c_str(),
                         aprsmsg.msg_source_call,
                         aprsmsg.msg_destination_call,
                         aprsmsg.msg_payload);
#else
    (void)aprsmsg;
#endif
}

/**
 * @brief Log transmitted text packet to OpenLog serial.
 */
void hab_log_tx(uint16_t msg_type, const struct aprsMessage &aprsmsg)
{
#if defined(ESP32) && defined(ENABLE_OPENLOG) && (ENABLE_OPENLOG == 1)
    if (!s_openlog_initialized)
        return;

    // Only log text messages addressed directly to a specific station (not broadcast '*' and not group)
    // E.g. direct responses like QSL confirmations, $P_DROP OK, etc.
    if (msg_type == MSG_TYPE_TEXT)
    {
        if (strcmp(aprsmsg.msg_destination_call, "*") != 0 && CheckGroup(aprsmsg.msg_destination_call) == 0)
        {
            OpenLogSerial.printf("[%s %s] [TX] %s -> %s: %s\n",
                                 getDateString().c_str(),
                                 getTimeString().c_str(),
                                 aprsmsg.msg_source_call,
                                 aprsmsg.msg_destination_call,
                                 aprsmsg.msg_payload);
        }
    }
#else
    (void)msg_type;
    (void)aprsmsg;
#endif
}

#endif // ENABLE_HAB_MODE
