#include "alarm.h"

/*=========================================================================
 * Global alarm instance. Default: disabled, 07:00:00, every day.
 *=========================================================================*/
AlarmState g_alarm;

/*=========================================================================
 * Initialize alarm to defaults (disabled, 07:00:00, every day).
 *=========================================================================*/
void Alarm_Init(void)
{
    g_alarm.hour    = 7;
    g_alarm.minute  = 0;
    g_alarm.second  = 0;
    g_alarm.day     = 0;    /* 0 = every day */
    g_alarm.enabled = 0;
    g_alarm.ringing = 0;
}

/*=========================================================================
 * Check if the current time matches the alarm trigger.
 * Called once per second. If alarm is enabled and time matches,
 * start ringing.
 *=========================================================================*/
void Alarm_Check(ClockTime *now)
{
    uint8_t match;

    if (!g_alarm.enabled) {
        return;
    }

    /* Already ringing: do not re-trigger */
    if (g_alarm.ringing) {
        return;
    }

    match = 0;
    if (now->hour   == g_alarm.hour   &&
        now->minute == g_alarm.minute &&
        now->second == g_alarm.second) {

        if (g_alarm.day == 0) {
            /* Every day */
            match = 1;
        } else if (g_alarm.day == now->day) {
            /* Specific day of month */
            match = 1;
        }
    }

    if (match) {
        g_alarm.ringing = 1;
    }
}

/*=========================================================================
 * Stop the alarm (turn off ringing, but do not disable the alarm).
 *=========================================================================*/
void Alarm_Stop(void)
{
    g_alarm.ringing = 0;
}

/*=========================================================================
 * Set the alarm time and day.
 * d=0 means every day; d=1-31 means specific day of month.
 * Setting the alarm automatically enables it.
 *=========================================================================*/
void Alarm_Set(uint8_t h, uint8_t m, uint8_t s, uint8_t d)
{
    g_alarm.hour    = h;
    g_alarm.minute  = m;
    g_alarm.second  = s;
    g_alarm.day     = d;
    g_alarm.enabled = 1;
    g_alarm.ringing = 0;
}

/*=========================================================================
 * Return 1 if the alarm is currently ringing.
 *=========================================================================*/
uint8_t Alarm_IsRinging(void)
{
    return g_alarm.ringing;
}
