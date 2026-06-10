#ifndef __ALARM_H__
#define __ALARM_H__

#include <stdint.h>
#include "clock.h"

/*=========================================================================
 * Alarm State Structure
 * hour, minute, second: alarm trigger time (0-23, 0-59, 0-59)
 * day: 0=every day, 1-31=specific day of month
 * enabled: 0=disabled, 1=enabled
 * ringing: 0=not ringing, 1=ringing now
 *=========================================================================*/
typedef struct {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t day;
    uint8_t enabled;
    uint8_t ringing;
} AlarmState;

/*=========================================================================
 * Global Alarm Instance
 *=========================================================================*/
extern AlarmState g_alarm;

/*=========================================================================
 * Function Declarations
 *=========================================================================*/
void    Alarm_Init(void);
void    Alarm_Check(ClockTime *now);
void    Alarm_Stop(void);
void    Alarm_Set(uint8_t h, uint8_t m, uint8_t s, uint8_t d);
uint8_t Alarm_IsRinging(void);

#endif /* __ALARM_H__ */
