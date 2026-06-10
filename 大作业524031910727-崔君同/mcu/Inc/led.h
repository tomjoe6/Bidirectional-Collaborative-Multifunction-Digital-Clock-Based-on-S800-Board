#ifndef __LED_H__
#define __LED_H__

#include <stdint.h>
#include "hw_config.h"

/*=========================================================================
 * Global LED State (8 bits, 1=LED_on, 0=LED_off)
 * Exported for buzzer module to coordinate PCA9557 writes.
 *=========================================================================*/
extern uint8_t g_led_state;

/*=========================================================================
 * Function Declarations
 *=========================================================================*/
void LED_Init(void);
void LED_Set(uint8_t bit, uint8_t val);
void LED_Write(void);           /* write g_led_state to PCA9557        */
void LED_Heartbeat(void);       /* toggle D0, called every 1s          */
void LED_RXFlash(void);         /* brief flash D4 (RX activity)        */
void LED_TXFlash(void);         /* brief flash D5 (TX activity)        */
void LED_UpdateFlashTimeout(void); /* called every 10ms to time out flashes */

#endif /* __LED_H__ */
