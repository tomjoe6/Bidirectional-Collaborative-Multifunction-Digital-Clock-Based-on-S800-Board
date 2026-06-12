#ifndef __CLOCK_H__
#define __CLOCK_H__

#include <stdint.h>

/*=========================================================================
 * Clock Time Structure
 * year:   0-99  (0 = 2000, 24 = 2024, etc.)
 * month:  1-12
 * day:    1-31
 * hour:   0-23
 * minute: 0-59
 * second: 0-59
 * day_of_week: 0-6 (0=Sunday)
 *=========================================================================*/
typedef struct {
    uint8_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t day_of_week;
} ClockTime;

/*=========================================================================
 * Global Clock Instance
 *=========================================================================*/
extern ClockTime g_clock;

/*=========================================================================
 * Function Declarations
 *=========================================================================*/
void    Clock_Init(uint8_t y, uint8_t m, uint8_t d,
                   uint8_t h, uint8_t min, uint8_t s);
void    Clock_Tick(void);
uint8_t Clock_IsLeapYear(uint8_t year);
uint8_t Clock_DaysInMonth(uint8_t year, uint8_t month);

#endif /* __CLOCK_H__ */
