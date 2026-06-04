#ifndef __BUZZER_H__
#define __BUZZER_H__

#include <stdint.h>

/*=========================================================================
 * Function Declarations
 *=========================================================================*/
void    Buzzer_Init(void);
void    Buzzer_On(void);
void    Buzzer_Off(void);
void    Buzzer_Toggle(void);
void    Buzzer_RhythmHandler(void);     /* called every 100ms           */
void    Buzzer_StartRing(void);
void    Buzzer_StopRing(void);
uint8_t Buzzer_IsRinging(void);
void    Buzzer_WriteOutput(void);       /* combined LED+buzzer PCA9557 write */

#endif /* __BUZZER_H__ */
