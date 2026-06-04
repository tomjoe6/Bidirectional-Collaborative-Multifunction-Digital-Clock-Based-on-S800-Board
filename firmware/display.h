#ifndef __DISPLAY_H__
#define __DISPLAY_H__

#include <stdint.h>
#include "clock.h"
#include "hw_config.h"

/*=========================================================================
 * Display Buffer Constants
 *=========================================================================*/
#define DISP_BUF_SIZE           32
#define DISP_DIGITS             8

/*=========================================================================
 * Global Display State
 *=========================================================================*/
extern char     g_disp_buffer[DISP_BUF_SIZE];   /* user-settable text   */
extern uint8_t  g_disp_mode;                     /* TIME/DATE/YEAR/FULL  */
extern uint8_t  g_disp_format;                   /* LEFT or RIGHT        */
extern uint8_t  g_disp_on;                       /* 1=display on,0=off   */
extern uint8_t  g_disp_night;                    /* 1=night mode active  */
extern uint8_t  g_dp_mask;                       /* 8-bit DP mask        */
extern char     g_disp_chars[DISP_DIGITS];       /* chars on display now */

/* Flow control */
extern int16_t  g_flow_position;
extern int8_t   g_flow_direction;
extern uint8_t  g_flow_speed;                    /* 0=slow, 1=fast       */
extern uint16_t g_flow_delay;                    /* 10ms ticks per step  */
extern uint16_t g_flow_counter;

/*=========================================================================
 * Segment Code Table (global, shared with events module)
 *=========================================================================*/
extern const uint8_t g_seg_table_num[10];
extern const uint8_t g_seg_table_alpha[26];

/*=========================================================================
 * Function Declarations
 *=========================================================================*/
void    Display_Init(void);
void    Display_Scan(uint8_t digit_idx);
void    Display_SetMode(uint8_t mode);
void    Display_SetFormat(uint8_t format);
void    Display_SetBuffer(const char *str);
void    Display_UpdateFromClock(ClockTime *now);
void    Display_FlowAdvance(void);
void    Display_SetNight(uint8_t night);
uint8_t Display_GetSegCode(char c);

#endif /* __DISPLAY_H__ */
