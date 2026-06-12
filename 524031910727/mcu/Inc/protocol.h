#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__

#include <stdint.h>

/*=========================================================================
 * Function Declarations
 *=========================================================================*/
void Protocol_Init(void);
void Protocol_CharReceived(uint8_t c);  /* called from UART ISR         */
void Protocol_Process(void);            /* called in main loop when ready */

#endif /* __PROTOCOL_H__ */
