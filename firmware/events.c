#include <string.h>
#include <stdio.h>
#include "events.h"
#include "hw_config.h"
#include "keys.h"

/*=========================================================================
 * Event queue entry
 *=========================================================================*/
typedef struct {
    char    msg[EVENT_MSG_MAX];
    uint8_t len;
    uint8_t valid;
} EventEntry;

/*=========================================================================
 * Event queue
 *=========================================================================*/
static EventEntry g_event_queue[EVENT_QUEUE_SIZE];
static uint8_t    g_event_head;       /* write position */
static uint8_t    g_event_tail;       /* read position  */
static uint8_t    g_event_count;

/* Rate limiting: max 1 event per 50ms */
static uint16_t   g_event_rate_timer;  /* 10ms ticks since last send */

/* External references for key names */
const char* Keys_GetKeyName(uint8_t key_id);

/*=========================================================================
 * Initialize the event queue.
 *=========================================================================*/
void Events_Init(void)
{
    uint8_t i;

    for (i = 0; i < EVENT_QUEUE_SIZE; i++) {
        g_event_queue[i].msg[0] = '\0';
        g_event_queue[i].len    = 0;
        g_event_queue[i].valid  = 0;
    }
    g_event_head        = 0;
    g_event_tail        = 0;
    g_event_count       = 0;
    g_event_rate_timer  = 0;
}

/*=========================================================================
 * Enqueue an event message. Returns 0 if queue is full.
 *=========================================================================*/
static uint8_t Events_Enqueue(const char *msg)
{
    uint8_t len;

    if (g_event_count >= EVENT_QUEUE_SIZE) {
        return 0; /* queue full */
    }

    len = (uint8_t)strlen(msg);
    if (len >= EVENT_MSG_MAX) {
        len = EVENT_MSG_MAX - 1;
    }

    {
        uint8_t i;
        for (i = 0; i < len; i++) {
            g_event_queue[g_event_head].msg[i] = msg[i];
        }
        g_event_queue[g_event_head].msg[len] = '\0';
    }
    g_event_queue[g_event_head].len   = len;
    g_event_queue[g_event_head].valid = 1;

    g_event_head++;
    if (g_event_head >= EVENT_QUEUE_SIZE) {
        g_event_head = 0;
    }
    g_event_count++;

    return 1;
}

/*=========================================================================
 * Report a key press event: *EVT:KEY <NAME>\r\n
 *=========================================================================*/
void Events_ReportKey(uint8_t key_id)
{
    const char *name;
    char msg[EVENT_MSG_MAX];

    name = Keys_GetKeyName(key_id);
    sprintf(msg, "*EVT:KEY %s\r\n", name);
    Events_Enqueue(msg);
}

/*=========================================================================
 * Report alarm event: *EVT:ALARM\r\n or *EVT:ALARM OFF\r\n
 *=========================================================================*/
void Events_ReportAlarm(uint8_t on)
{
    if (on) {
        Events_Enqueue("*EVT:ALARM\r\n");
    } else {
        Events_Enqueue("*EVT:ALARM OFF\r\n");
    }
}

/*=========================================================================
 * Report edit event: *EVT:EDIT <TYPE> <VALUE>\r\n
 * type: 1=DATE, 2=TIME, 3=ALARM
 *=========================================================================*/
void Events_ReportEdit(uint8_t type, const char *value)
{
    char msg[EVENT_MSG_MAX];
    const char *type_str;

    type_str = "UNKNOWN";
    if (type == EDIT_DATE)  type_str = "DATE";
    if (type == EDIT_TIME)  type_str = "TIME";
    if (type == EDIT_ALARM) type_str = "ALARM";

    sprintf(msg, "*EVT:EDIT %s %s\r\n", type_str, value);
    Events_Enqueue(msg);
}

/*=========================================================================
 * Report display event: *EVT:DISP <8char> <dpHex>\r\n
 *=========================================================================*/
void Events_ReportDisp(const char *seg_str, uint8_t dp_hex)
{
    char msg[EVENT_MSG_MAX];
    char disp9[9];
    uint8_t i;

    /* Ensure exactly 8 chars for the display string */
    for (i = 0; i < 8; i++) {
        if (seg_str && seg_str[i]) {
            disp9[i] = seg_str[i];
        } else {
            disp9[i] = ' ';
        }
    }
    disp9[8] = '\0';

    sprintf(msg, "*EVT:DISP %s %02X\r\n", disp9, dp_hex);
    Events_Enqueue(msg);
}

/*=========================================================================
 * Report LED event: *EVT:LED <hex2>\r\n
 *=========================================================================*/
void Events_ReportLED(uint8_t led_hex)
{
    char msg[EVENT_MSG_MAX];

    sprintf(msg, "*EVT:LED %02X\r\n", led_hex);
    Events_Enqueue(msg);
}

/*=========================================================================
 * Report mode event: *EVT:MODE <STATE>\r\n
 * mode: 0=DAY, 1=NIGHT
 *=========================================================================*/
void Events_ReportMode(uint8_t mode)
{
    if (mode == MODE_NIGHT) {
        Events_Enqueue("*EVT:MODE NIGHT\r\n");
    } else {
        Events_Enqueue("*EVT:MODE DAY\r\n");
    }
}

/*=========================================================================
 * 1Hz heartbeat handler: sends DISP and LED events every second.
 * Called from the 1000ms handler in main.
 *=========================================================================*/
void Events_1HzHandler(ClockTime *now, uint8_t led_state,
                       const char *disp_str, uint8_t dp)
{
    char time_str[16];

    /* Send DISP heartbeat */
    Events_ReportDisp(disp_str, dp);

    /* Send LED heartbeat */
    Events_ReportLED(led_state);

    /* Suppress unused parameter warning */
    sprintf(time_str, "%02d:%02d:%02d",
            now->hour, now->minute, now->second);
    (void)time_str;
}

/*=========================================================================
 * Check if there are pending events to send.
 *=========================================================================*/
uint8_t Events_HasPending(void)
{
    return (g_event_count > 0) ? 1 : 0;
}

/*=========================================================================
 * Send the next pending event over UART.
 * Rate-limited to max 1 event per 50ms to avoid flooding.
 * Called from the 100ms handler in main.
 *=========================================================================*/
void Events_SendNext(void)
{
    uint8_t i;
    char *pmsg;

    /* Rate limiting: 20ms between sends (50 events/sec max).
     * At 115200 baud this uses <10% bandwidth, enough for flow. */
    g_event_rate_timer++;
    if (g_event_rate_timer < 2) {
        return;
    }
    g_event_rate_timer = 0;

    if (g_event_count == 0) {
        return;
    }

    /* Get the next event from the queue */
    if (!g_event_queue[g_event_tail].valid) {
        /* Queue corrupted; reset */
        Events_Init();
        return;
    }

    pmsg = g_event_queue[g_event_tail].msg;

    /* Send the event over UART */
    for (i = 0; i < g_event_queue[g_event_tail].len; i++) {
        UART0_SendChar(pmsg[i]);
    }

    /* Apply FORMAT RIGHT reversal if needed */
    /* NOTE: FORMAT RIGHT reversal for responses is handled in protocol.c.
     * Event messages are NOT reversed — only command responses are. */

    /* Mark as sent and advance tail */
    g_event_queue[g_event_tail].valid = 0;
    g_event_tail++;
    if (g_event_tail >= EVENT_QUEUE_SIZE) {
        g_event_tail = 0;
    }
    g_event_count--;
}
