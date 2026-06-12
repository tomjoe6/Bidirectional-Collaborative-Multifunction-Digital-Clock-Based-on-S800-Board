#ifndef __EVENTS_H__
#define __EVENTS_H__

#include <stdint.h>
#include "clock.h"

/*=========================================================================
 * Event Queue Constants
 *=========================================================================*/
#define EVENT_QUEUE_SIZE    16
#define EVENT_MSG_MAX       64

/*=========================================================================
 * Function Declarations
 *=========================================================================*/
void    Events_Init(void);
void    Events_ReportKey(uint8_t key_id);
void    Events_ReportAlarm(uint8_t on);
void    Events_ReportEdit(uint8_t type, const char *value);
void    Events_ReportDisp(const char *seg_str, uint8_t dp_hex);
void    Events_ReportLED(uint8_t led_hex);
void    Events_ReportMode(uint8_t mode);
void    Events_1HzHandler(ClockTime *now, uint8_t led_state,
                          const char *disp_str, uint8_t dp);
uint8_t Events_HasPending(void);
void    Events_SendNext(void);

#endif /* __EVENTS_H__ */
