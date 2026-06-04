#include <string.h>
#include "keys.h"
#include "clock.h"
#include "alarm.h"
#include "display.h"

/*=========================================================================
 * Key name strings (for protocol and events)
 *=========================================================================*/
static const char *g_key_names[NUM_KEYS] = {
    "FUNC", "SHIFT", "ADD", "SAVE", "DISP",
    "SPEED", "FORMAT", "EXT", "USER1", "USER2"
};

/*=========================================================================
 * Per-key state for debounce and long-press detection
 *=========================================================================*/
static uint8_t  g_key_raw[NUM_KEYS];       /* raw reading (0=pressed, 1=released) */
static uint8_t  g_key_state[NUM_KEYS];     /* stable debounced state */
static uint8_t  g_key_prev[NUM_KEYS];      /* previous stable state */
static uint8_t  g_key_dcnt[NUM_KEYS];      /* debounce counter */
static uint16_t g_key_press_time[NUM_KEYS];/* how long pressed (10ms ticks) */
static uint8_t  g_key_event[NUM_KEYS];     /* pending event type (0=none, 1=short, 2=long) */
static uint8_t  g_key_event_count;         /* number of pending events */

/*=========================================================================
 * Edit state globals
 *=========================================================================*/
uint8_t  g_edit_state   = EDIT_NONE;
uint8_t  g_edit_field   = 0;
uint16_t g_edit_timeout = 0;
uint8_t  g_edit_blink   = 0;

/* Edit temporary values (hold edits until saved) */
static ClockTime g_edit_temp_time;
static AlarmState g_edit_temp_alarm;

/*=========================================================================
 * Read all 10 keys: K1-K4 from GPIO, K5-K8+USER1/USER2 from TCA6424 Port0.
 * Returns the raw state in g_key_raw array.
 *=========================================================================*/
static void Keys_ReadRaw(void)
{
    uint8_t port0_val;
    uint8_t result;

    /* K1: PF0 */
    g_key_raw[KEY_ID_FUNC]  = (GPIOPinRead(KEY1_PORT, KEY1_PIN) ? 1 : 0);
    /* K2: PJ0 */
    g_key_raw[KEY_ID_SHIFT] = (GPIOPinRead(KEY2_PORT, KEY2_PIN) ? 1 : 0);
    /* K3: PJ1 */
    g_key_raw[KEY_ID_ADD]   = (GPIOPinRead(KEY3_PORT, KEY3_PIN) ? 1 : 0);
    /* K4: PN0 */
    g_key_raw[KEY_ID_SAVE]  = (GPIOPinRead(KEY4_PORT, KEY4_PIN) ? 1 : 0);

    /* K5-K8, USER1, USER2: TCA6424 Port0 */
    port0_val = I2C0_ReadByte(TCA6424_I2CADDR, TCA6424_INPUT_PORT0);
    result = port0_val; /* capture for potential error checking */

    g_key_raw[KEY_ID_DISP]   = (port0_val & (1 << KEY5_BIT))  ? 1 : 0;
    g_key_raw[KEY_ID_SPEED]  = (port0_val & (1 << KEY6_BIT))  ? 1 : 0;
    g_key_raw[KEY_ID_FORMAT] = (port0_val & (1 << KEY7_BIT))  ? 1 : 0;
    g_key_raw[KEY_ID_EXT]    = (port0_val & (1 << KEY8_BIT))  ? 1 : 0;
    g_key_raw[KEY_ID_USER1]  = (port0_val & (1 << USER1_BIT)) ? 1 : 0;
    g_key_raw[KEY_ID_USER2]  = (port0_val & (1 << USER2_BIT)) ? 1 : 0;

    (void)result;
}

/*=========================================================================
 * Initialize key scanning state.
 *=========================================================================*/
void Keys_Init(void)
{
    uint8_t i;

    for (i = 0; i < NUM_KEYS; i++) {
        g_key_raw[i]        = 1;
        g_key_state[i]      = 1;
        g_key_prev[i]       = 1;
        g_key_dcnt[i]       = 0;
        g_key_press_time[i] = 0;
        g_key_event[i]      = KEY_EVENT_NONE;
    }
    g_key_event_count = 0;

    g_edit_state   = EDIT_NONE;
    g_edit_field   = 0;
    g_edit_timeout = 0;
    g_edit_blink   = 0;
}

/*=========================================================================
 * Load current clock/alarm values into edit temp buffer
 * based on current edit state.
 *=========================================================================*/
static void Keys_LoadEditTemp(void)
{
    if (g_edit_state == EDIT_DATE) {
        g_edit_temp_time = g_clock;
    } else if (g_edit_state == EDIT_TIME) {
        g_edit_temp_time = g_clock;
    } else if (g_edit_state == EDIT_ALARM) {
        g_edit_temp_alarm = g_alarm;
    }
}

/*=========================================================================
 * Save edit temp values to actual clock or alarm.
 *=========================================================================*/
static void Keys_SaveEdit(void)
{
    if (g_edit_state == EDIT_DATE || g_edit_state == EDIT_TIME) {
        g_clock = g_edit_temp_time;
        /* Recompute day of week */
        g_clock.day_of_week = 0; /* Will be recomputed; use a fixed approach */
    } else if (g_edit_state == EDIT_ALARM) {
        g_alarm = g_edit_temp_alarm;
        g_alarm.enabled = 1;
    }
}

/*=========================================================================
 * Get max value for the given edit field.
 *=========================================================================*/
static uint8_t Keys_GetFieldMax(void)
{
    if (g_edit_state == EDIT_DATE) {
        if (g_edit_field == DATE_FIELD_YEAR)  return 99;
        if (g_edit_field == DATE_FIELD_MONTH) return 12;
        if (g_edit_field == DATE_FIELD_DAY)
            return Clock_DaysInMonth(g_edit_temp_time.year, g_edit_temp_time.month);
    } else if (g_edit_state == EDIT_TIME) {
        if (g_edit_field == TIME_FIELD_HOUR)   return 23;
        if (g_edit_field == TIME_FIELD_MINUTE) return 59;
        if (g_edit_field == TIME_FIELD_SECOND) return 59;
    } else if (g_edit_state == EDIT_ALARM) {
        if (g_edit_field == ALARM_FIELD_HOUR)   return 23;
        if (g_edit_field == ALARM_FIELD_MINUTE) return 59;
        if (g_edit_field == ALARM_FIELD_SECOND) return 59;
        if (g_edit_field == ALARM_FIELD_DAY)    return 31;
    }
    return 59;
}

/*=========================================================================
 * Get min value for the given edit field.
 *=========================================================================*/
static uint8_t Keys_GetFieldMin(void)
{
    if (g_edit_state == EDIT_DATE) {
        if (g_edit_field == DATE_FIELD_MONTH) return 1;
        if (g_edit_field == DATE_FIELD_DAY)   return 1;
    }
    if (g_edit_state == EDIT_ALARM) {
        if (g_edit_field == ALARM_FIELD_DAY) return 0;
    }
    return 0;
}

/*=========================================================================
 * Increment the current edit field value with wrap-around.
 *=========================================================================*/
static void Keys_IncrementField(void)
{
    uint8_t max_val, min_val;
    uint8_t *pval;
    uint8_t i;

    max_val = Keys_GetFieldMax();
    min_val = Keys_GetFieldMin();

    pval = NULL;

    if (g_edit_state == EDIT_DATE || g_edit_state == EDIT_TIME) {
        if (g_edit_state == EDIT_DATE) {
            if (g_edit_field == DATE_FIELD_YEAR)  pval = &g_edit_temp_time.year;
            if (g_edit_field == DATE_FIELD_MONTH) pval = &g_edit_temp_time.month;
            if (g_edit_field == DATE_FIELD_DAY)   pval = &g_edit_temp_time.day;
        } else {
            if (g_edit_field == TIME_FIELD_HOUR)   pval = &g_edit_temp_time.hour;
            if (g_edit_field == TIME_FIELD_MINUTE) pval = &g_edit_temp_time.minute;
            if (g_edit_field == TIME_FIELD_SECOND) pval = &g_edit_temp_time.second;
        }
    } else if (g_edit_state == EDIT_ALARM) {
        if (g_edit_field == ALARM_FIELD_HOUR)   pval = &g_edit_temp_alarm.hour;
        if (g_edit_field == ALARM_FIELD_MINUTE) pval = &g_edit_temp_alarm.minute;
        if (g_edit_field == ALARM_FIELD_SECOND) pval = &g_edit_temp_alarm.second;
        if (g_edit_field == ALARM_FIELD_DAY)    pval = &g_edit_temp_alarm.day;
    }

    if (pval != NULL) {
        (*pval)++;
        if (*pval > max_val) {
            *pval = min_val;
        }
    }

    /* Update display buffer with current edit values */
    /* The display will be updated in the main loop during edit mode */
    (void)i; /* suppress warning */
}

/*=========================================================================
 * Move to the next field in the current edit mode.
 *=========================================================================*/
static void Keys_NextField(void)
{
    uint8_t max_fields;

    if (g_edit_state == EDIT_DATE) {
        max_fields = 3;
    } else if (g_edit_state == EDIT_TIME) {
        max_fields = 3;
    } else if (g_edit_state == EDIT_ALARM) {
        max_fields = 4;
    } else {
        return;
    }

    g_edit_field++;
    if (g_edit_field >= max_fields) {
        g_edit_field = 0;
    }
}

/*=========================================================================
 * Enter edit mode for the specified state.
 *=========================================================================*/
static void Keys_EnterEdit(uint8_t state)
{
    g_edit_state = state;
    g_edit_field = 0;
    g_edit_timeout = EDIT_TIMEOUT_MS / 10; /* convert ms to 10ms ticks */
    g_edit_blink = 0;
    Keys_LoadEditTemp();
}

/*=========================================================================
 * Exit edit mode without saving.
 *=========================================================================*/
void Keys_ExitEditNoSave(void)
{
    g_edit_state = EDIT_NONE;
    g_edit_field = 0;
    g_edit_timeout = 0;
    g_edit_blink = 0;
}

/*=========================================================================
 * Cycle edit state: NONE->DATE->TIME->ALARM->NONE
 * If alarm is ringing, stop alarm first.
 *=========================================================================*/
static void Keys_CycleFunc(void)
{
    /* If alarm is ringing, FUNC stops it (priority) */
    if (Alarm_IsRinging()) {
        Alarm_Stop();
        return;
    }

    if (g_edit_state == EDIT_NONE) {
        Keys_EnterEdit(EDIT_DATE);
    } else if (g_edit_state == EDIT_DATE) {
        Keys_EnterEdit(EDIT_TIME);
    } else if (g_edit_state == EDIT_TIME) {
        Keys_EnterEdit(EDIT_ALARM);
    } else {
        /* Exit edit mode */
        Keys_ExitEditNoSave();
    }
}

/*=========================================================================
 * Dispatch a key press event.
 *=========================================================================*/
static void Keys_Dispatch(uint8_t key_id, uint8_t is_long)
{
    /* Always reset timeout on any key press during edit */
    if (g_edit_state != EDIT_NONE) {
        g_edit_timeout = EDIT_TIMEOUT_MS / 10;
    }

    switch (key_id) {
    case KEY_ID_FUNC:
        if (is_long) {
            /* Long FUNC = SAVE + exit (like SAVE) */
            if (g_edit_state != EDIT_NONE) {
                Keys_SaveEdit();
                Keys_ExitEditNoSave();
            }
        } else {
            /* Short FUNC: cycle edit modes / stop alarm */
            Keys_CycleFunc();
        }
        break;

    case KEY_ID_SHIFT:
        if (g_edit_state != EDIT_NONE) {
            Keys_NextField();
        }
        break;

    case KEY_ID_ADD:
        if (g_edit_state != EDIT_NONE) {
            Keys_IncrementField();
            /* If long press, keep auto-repeating (handled by caller) */
        }
        break;

    case KEY_ID_SAVE:
        if (g_edit_state != EDIT_NONE) {
            Keys_SaveEdit();
            Keys_ExitEditNoSave();
        }
        break;

    case KEY_ID_DISP:
        /* Cycle display mode: TIME -> DATE -> YEAR -> TIME */
        {
            uint8_t next_mode;
            next_mode = g_disp_mode + 1;
            if (next_mode > DISP_MODE_YEAR) {
                next_mode = DISP_MODE_TIME;
            }
            Display_SetMode(next_mode);
        }
        break;

    case KEY_ID_SPEED:
        /* Toggle flow speed */
        if (g_flow_speed == FLOW_SPEED_SLOW) {
            g_flow_speed = FLOW_SPEED_FAST;
            g_flow_delay = FLOW_DELAY_FAST;
        } else {
            g_flow_speed = FLOW_SPEED_SLOW;
            g_flow_delay = FLOW_DELAY_SLOW;
        }
        break;

    case KEY_ID_FORMAT:
        /* Toggle format direction */
        if (g_disp_format == FORMAT_LEFT) {
            Display_SetFormat(FORMAT_RIGHT);
        } else {
            Display_SetFormat(FORMAT_LEFT);
        }
        break;

    case KEY_ID_EXT:
    case KEY_ID_USER1:
    case KEY_ID_USER2:
        /* These generate events only, no local action (handled by PC) */
        break;

    default:
        break;
    }
}

/*=========================================================================
 * Scan keys every 10ms. Handles debounce, long-press detection,
 * edit timeout, and queues key events.
 *=========================================================================*/
void Keys_Scan10ms(void)
{
    uint8_t i;
    uint8_t raw;
    uint8_t stable;

    Keys_ReadRaw();

    for (i = 0; i < NUM_KEYS; i++) {
        raw = g_key_raw[i];
        stable = g_key_state[i];

        if (raw != stable) {
            /* State change: increment or decrement debounce counter */
            if (g_key_dcnt[i] < DEBOUNCE_MS / 10) {
                g_key_dcnt[i]++;
            } else {
                /* Debounced: state has changed */
                g_key_state[i] = raw;
                g_key_dcnt[i] = 0;

                if (raw == 0) {
                    /* Key pressed */
                    g_key_press_time[i] = 0;
                } else {
                    /* Key released */
                    if (g_key_press_time[i] < LONG_PRESS_MS / 10) {
                        /* Short press */
                        if (g_key_event[i] == KEY_EVENT_NONE &&
                            g_key_event_count < 10) {
                            g_key_event[i] = KEY_EVENT_SHORT;
                            g_key_event_count++;
                        }
                    }
                    g_key_press_time[i] = 0;
                }
            }
        } else if (raw == 0) {
            /* Key held down */
            g_key_press_time[i]++;

            /* Detect long press */
            if (g_key_press_time[i] == LONG_PRESS_MS / 10) {
                if (g_key_event[i] == KEY_EVENT_NONE &&
                    g_key_event_count < 10) {
                    g_key_event[i] = KEY_EVENT_LONG;
                    g_key_event_count++;
                }
            }

            /* Auto-repeat for ADD key while held (every 100ms after initial) */
            if (i == KEY_ID_ADD && g_key_press_time[i] > LONG_PRESS_MS / 10) {
                if ((g_key_press_time[i] % 10) == 0) {
                    /* Queue repeat ADD events while in edit mode */
                    if (g_edit_state != EDIT_NONE) {
                        Keys_IncrementField();
                        g_edit_timeout = EDIT_TIMEOUT_MS / 10;
                    }
                }
            }

            /* Reset debounce counter while held */
            g_key_dcnt[i] = 0;
        } else {
            /* Key released and stable: reset press time */
            g_key_press_time[i] = 0;
        }
    }

    /* Edit timeout: decrement and auto-exit if timeout reached */
    if (g_edit_state != EDIT_NONE) {
        if (g_edit_timeout > 0) {
            g_edit_timeout--;
            if (g_edit_timeout == 0) {
                Keys_ExitEditNoSave();
            }
        }

        /* Blink timer: toggle every 500ms */
        {
            static uint8_t blink_timer = 0;
            blink_timer++;
            if (blink_timer >= 50) { /* 50 * 10ms = 500ms */
                blink_timer = 0;
                g_edit_blink = (uint8_t)(!g_edit_blink);
            }
        }
    }
}

/*=========================================================================
 * Get the next pending key event. Returns 1 if event available.
 * The event is consumed (cleared) when retrieved.
 *=========================================================================*/
uint8_t Keys_GetEvent(uint8_t *key_id, uint8_t *is_long)
{
    uint8_t i;

    for (i = 0; i < NUM_KEYS; i++) {
        if (g_key_event[i] != KEY_EVENT_NONE) {
            *key_id  = i;
            *is_long = (g_key_event[i] == KEY_EVENT_LONG) ? 1 : 0;

            /* Dispatch the key action locally */
            Keys_Dispatch(i, *is_long);

            /* Clear the event */
            g_key_event[i] = KEY_EVENT_NONE;
            if (g_key_event_count > 0) {
                g_key_event_count--;
            }
            return 1;
        }
    }
    return 0;
}

/*=========================================================================
 * Get current edit state (for LED and event reporting).
 *=========================================================================*/
uint8_t Keys_GetEditState(void)
{
    return g_edit_state;
}

/*=========================================================================
 * Look up the protocol name for a key ID.
 *=========================================================================*/
const char* Keys_GetKeyName(uint8_t key_id)
{
    if (key_id < NUM_KEYS) {
        return g_key_names[key_id];
    }
    return "UNKNOWN";
}

/*=========================================================================
 * Simulate a key press (from *SET:KEY command).
 * Directly dispatches the key action without physical debounce.
 *=========================================================================*/
void Keys_Simulate(uint8_t key_id)
{
    if (key_id < NUM_KEYS) {
        Keys_Dispatch(key_id, 0); /* 0 = short press */
    }
}

/*=========================================================================
 * Provide access to edit temp values for display update during editing.
 *=========================================================================*/
void Keys_GetEditDisplay(char *buf, uint8_t bufsize)
{
    if (g_edit_state == EDIT_DATE) {
        sprintf(buf, "%02d.%02d.%02d",
                 g_edit_temp_time.year % 100,
                 g_edit_temp_time.month,
                 g_edit_temp_time.day);
    } else if (g_edit_state == EDIT_TIME) {
        sprintf(buf, "%02d.%02d.%02d",
                 g_edit_temp_time.hour,
                 g_edit_temp_time.minute,
                 g_edit_temp_time.second);
    } else if (g_edit_state == EDIT_ALARM) {
        sprintf(buf, "%02d.%02d.%02d",
                 g_edit_temp_alarm.hour,
                 g_edit_temp_alarm.minute,
                 g_edit_temp_alarm.second);
    } else {
        buf[0] = '\0';
    }
}
