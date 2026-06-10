#include "clock.h"

/*=========================================================================
 * Day-of-week computation: Zeller-like algorithm for years 2000-2099.
 * month: 1=Jan..12=Dec, day: 1..31, year: 0..99 (2000+year).
 * Returns 0=Sunday .. 6=Saturday.
 *=========================================================================*/
static uint8_t Clock_ComputeDOW(uint8_t year, uint8_t month, uint8_t day)
{
    int16_t m_adj, y_adj;
    int16_t dow;
    uint8_t result;

    /* Jan and Feb treated as months 13 and 14 of previous year */
    if (month <= 2) {
        m_adj = (int16_t)month + 12;
        y_adj = (int16_t)year - 1;
    } else {
        m_adj = (int16_t)month;
        y_adj = (int16_t)year;
    }

    /* Zeller's congruence for Gregorian calendar */
    dow = (int16_t)day;
    dow = dow + (13 * (m_adj + 1)) / 5;
    dow = dow + y_adj;
    dow = dow + y_adj / 4;
    dow = dow + (20 / 4);       /* century = 20 */
    dow = dow - 2 * 20;
    dow = dow % 7;
    if (dow < 0) dow = dow + 7;

    result = (uint8_t)dow;
    return result;
}

/*=========================================================================
 * Check if the given year (0..99, 2000+year) is a leap year.
 *=========================================================================*/
uint8_t Clock_IsLeapYear(uint8_t year)
{
    uint16_t full_year;
    full_year = 2000 + (uint16_t)year;
    if ((full_year % 400) == 0) return 1;
    if ((full_year % 100) == 0) return 0;
    if ((full_year % 4) == 0)   return 1;
    return 0;
}

/*=========================================================================
 * Return number of days in the given month (1..12) for year (0..99).
 *=========================================================================*/
uint8_t Clock_DaysInMonth(uint8_t year, uint8_t month)
{
    uint8_t days;
    static const uint8_t month_days[12] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };

    if (month < 1 || month > 12) return 30;
    days = month_days[month - 1];

    /* February in leap year */
    if (month == 2 && Clock_IsLeapYear(year)) {
        days = 29;
    }
    return days;
}

/*=========================================================================
 * Global clock instance, default to 2024-01-01 00:00:00 Monday
 *=========================================================================*/
ClockTime g_clock;

/*=========================================================================
 * Initialize clock with given date and time.
 *=========================================================================*/
void Clock_Init(uint8_t y, uint8_t m, uint8_t d,
                uint8_t h, uint8_t min, uint8_t s)
{
    g_clock.year   = y;
    g_clock.month  = m;
    g_clock.day    = d;
    g_clock.hour   = h;
    g_clock.minute = min;
    g_clock.second = s;
    g_clock.day_of_week = Clock_ComputeDOW(y, m, d);
}

/*=========================================================================
 * Advance the clock by one second with full carry handling.
 * Handles minute, hour, day, month, year rollover with leap-year support.
 *=========================================================================*/
void Clock_Tick(void)
{
    g_clock.second++;
    if (g_clock.second < 60) {
        return;
    }
    g_clock.second = 0;

    g_clock.minute++;
    if (g_clock.minute < 60) {
        return;
    }
    g_clock.minute = 0;

    g_clock.hour++;
    if (g_clock.hour < 24) {
        return;
    }
    g_clock.hour = 0;

    /* Day rollover */
    g_clock.day_of_week++;
    if (g_clock.day_of_week > 6) {
        g_clock.day_of_week = 0;
    }

    g_clock.day++;
    if (g_clock.day <= Clock_DaysInMonth(g_clock.year, g_clock.month)) {
        return;
    }
    g_clock.day = 1;

    /* Month rollover */
    g_clock.month++;
    if (g_clock.month <= 12) {
        return;
    }
    g_clock.month = 1;

    /* Year rollover */
    g_clock.year++;
    /* Recompute DOW for Jan 1 of new year */
    g_clock.day_of_week = Clock_ComputeDOW(g_clock.year, 1, 1);
}
