#ifndef __KEYS_H__
#define __KEYS_H__

#include <stdint.h>
#include "hw_config.h"

/*=========================================================================
 * Edit States
 *=========================================================================*/
#define EDIT_NONE               0
#define EDIT_DATE               1
#define EDIT_TIME               2
#define EDIT_ALARM              3

/*=========================================================================
 * Key Event Flags
 *=========================================================================*/
#define KEY_EVENT_NONE          0
#define KEY_EVENT_SHORT         1
#define KEY_EVENT_LONG          2

/*=========================================================================
 * Edit field indices per edit mode
 * DATE:  0=YEAR, 1=MONTH, 2=DAY
 * TIME:  0=HOUR, 1=MINUTE, 2=SECOND
 * ALARM: 0=HOUR, 1=MINUTE, 2=SECOND, 3=DAY
 *=========================================================================*/
#define DATE_FIELD_YEAR         0
#define DATE_FIELD_MONTH        1
#define DATE_FIELD_DAY          2

#define TIME_FIELD_HOUR         0
#define TIME_FIELD_MINUTE       1
#define TIME_FIELD_SECOND       2

#define ALARM_FIELD_HOUR        0
#define ALARM_FIELD_MINUTE      1
#define ALARM_FIELD_SECOND      2
#define ALARM_FIELD_DAY         3

/*=========================================================================
 * Key constants
 *=========================================================================*/
#define DEBOUNCE_MS             30      /* 30ms debounce = 3 samples at 10ms */
#define LONG_PRESS_MS           800     /* 80 samples at 10ms */
#define EDIT_TIMEOUT_MS         5000    /* 5 seconds without activity */

/*=========================================================================
 * Global Edit State
 *=========================================================================*/
extern uint8_t  g_edit_state;           /* EDIT_NONE/DATE/TIME/ALARM    */
extern uint8_t  g_edit_field;           /* current field index          */
extern uint16_t g_edit_timeout;         /* inactivity counter (10ms)   */
extern uint8_t  g_edit_blink;           /* blink phase (0=show,1=hide) */

/*=========================================================================
 * Function Declarations
 *=========================================================================*/
void    Keys_Init(void);
void    Keys_Scan10ms(void);
uint8_t Keys_GetEvent(uint8_t *key_id, uint8_t *is_long);
uint8_t Keys_GetEditState(void);
void    Keys_ExitEditNoSave(void);
void    Keys_Simulate(uint8_t key_id);
void    Keys_GetEditDisplay(char *buf, uint8_t bufsize);

/* Key name lookup */
const char* Keys_GetKeyName(uint8_t key_id);

#endif /* __KEYS_H__ */
