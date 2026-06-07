#ifndef __HW_CONFIG_H__
#define __HW_CONFIG_H__

#include <stdint.h>
#include <stdbool.h>
#include "hw_memmap.h"
#include "hw_types.h"
#include "gpio.h"
#include "i2c.h"
#include "pin_map.h"
#include "sysctl.h"
#include "systick.h"
#include "interrupt.h"
#include "uart.h"
#include "hw_ints.h"

/*=========================================================================
 * System Clock Configuration
 *=========================================================================*/
#define SYSTICK_FREQUENCY       1000        /* 1000 Hz = 1ms period      */
#define SYSTEM_CLOCK_HZ         20000000    /* 20 MHz system clock       */

/*=========================================================================
 * I2C Device Addresses (from exp2.c verified values)
 *=========================================================================*/
#define TCA6424_I2CADDR         0x22
#define PCA9557_I2CADDR         0x18

/*=========================================================================
 * PCA9557 Register Definitions
 *=========================================================================*/
#define PCA9557_INPUT           0x00
#define PCA9557_OUTPUT          0x01
#define PCA9557_POLINVERT       0x02
#define PCA9557_CONFIG          0x03

/*=========================================================================
 * TCA6424 Register Definitions
 *=========================================================================*/
#define TCA6424_INPUT_PORT0     0x00
#define TCA6424_INPUT_PORT1     0x01
#define TCA6424_INPUT_PORT2     0x02
#define TCA6424_OUTPUT_PORT0    0x04
#define TCA6424_OUTPUT_PORT1    0x05
#define TCA6424_OUTPUT_PORT2    0x06
#define TCA6424_CONFIG_PORT0    0x0C
#define TCA6424_CONFIG_PORT1    0x0D
#define TCA6424_CONFIG_PORT2    0x0E

/*=========================================================================
 * UART Configuration
 *=========================================================================*/
#define UART_BAUD_RATE          115200
#define UART_CONFIG_VAL         (UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE | UART_CONFIG_PAR_NONE)

/*=========================================================================
 * GPIO Key Pin Definitions
 * USERSW1(PJ0) and USERSW2(PJ1) on main board — per exp2.c verification.
 * All K1-K8 (SW1-SW8) are on TCA6424 Port0 via expansion board.
 *=========================================================================*/
#define USER1_GPIO_PORT         GPIO_PORTJ_BASE     /* USERSW1: USER1  */
#define USER1_GPIO_PIN          GPIO_PIN_0
#define USER2_GPIO_PORT         GPIO_PORTJ_BASE     /* USERSW2: USER2  */
#define USER2_GPIO_PIN          GPIO_PIN_1

/*=========================================================================
 * Extended Key Bits (TCA6424 Port0 input, 8 bits = K1~K8)
 *=========================================================================*/
#define KEY1_BIT                0   /* SW1: K1 FUNC      */
#define KEY2_BIT                1   /* SW2: K2 SHIFT     */
#define KEY3_BIT                2   /* SW3: K3 ADD       */
#define KEY4_BIT                3   /* SW4: K4 SAVE      */
#define KEY5_BIT                4   /* SW5: K5 DISP      */
#define KEY6_BIT                5   /* SW6: K6 SPEED     */
#define KEY7_BIT                6   /* SW7: K7 FORMAT    */
#define KEY8_BIT                7   /* SW8: K8 EXT       */

/* Total number of keys */
#define NUM_KEYS                10

/* Key ID constants */
#define KEY_ID_FUNC             0
#define KEY_ID_SHIFT            1
#define KEY_ID_ADD              2
#define KEY_ID_SAVE             3
#define KEY_ID_DISP             4
#define KEY_ID_SPEED            5
#define KEY_ID_FORMAT           6
#define KEY_ID_EXT              7
#define KEY_ID_USER1            8
#define KEY_ID_USER2            9

/*=========================================================================
 * PCA9557 LED Bit Assignments (active low: 0=LED_on, 1=LED_off)
 *=========================================================================*/
#define LED_HEARTBEAT           0   /* D0: 1Hz toggle heartbeat        */
#define LED_ALARM_EN            1   /* D1: Alarm enabled               */
#define LED_ALARM_RING          2   /* D2: Alarm ringing               */
#define LED_EDIT_ACTIVE         3   /* D3: Edit mode active            */
#define LED_RX_ACTIVE           4   /* D4: UART RX activity flash      */
#define LED_TX_ACTIVE           5   /* D5: UART TX activity flash      */
#define LED_NTP_STATUS          6   /* D6: NTP sync status             */
#define LED_DAYNIGHT            7   /* D7: Day/Night (1=DAY,0=NIGHT)  */

/*=========================================================================
 * Buzzer — PF3 GPIO toggle (verified PWM7 from schematic)
 * We drive PF3 as a plain GPIO, toggled at audio rate by SysTick.
 *=========================================================================*/
#define BUZZER_PORT             GPIO_PORTF_BASE
#define BUZZER_PIN              GPIO_PIN_3

/*=========================================================================
 * Display Modes
 *=========================================================================*/
#define DISP_MODE_TIME          0   /* HH.MM.SS                       */
#define DISP_MODE_DATE          1   /* YY.MM.DD                       */
#define DISP_MODE_YEAR          2   /* YYYYMMDD                       */
#define DISP_MODE_FULL          3   /* Message buffer with flow       */

/*=========================================================================
 * Format Directions
 *=========================================================================*/
#define FORMAT_LEFT             0
#define FORMAT_RIGHT            1

/*=========================================================================
 * Edit States
 *=========================================================================*/
#define EDIT_NONE               0
#define EDIT_DATE               1
#define EDIT_TIME               2
#define EDIT_ALARM              3

/*=========================================================================
 * Day/Night Mode
 *=========================================================================*/
#define MODE_DAY                0
#define MODE_NIGHT              1

/*=========================================================================
 * Flow Speed Levels
 *=========================================================================*/
#define FLOW_SPEED_SLOW         0
#define FLOW_SPEED_FAST         1
#define FLOW_DELAY_SLOW         50      /* 50 * 10ms = 500ms per step  */
#define FLOW_DELAY_FAST         10      /* 10 * 10ms = 100ms per step  */

/*=========================================================================
 * Protocol Buffer Sizes
 *=========================================================================*/
#define PROTO_RING_SIZE         256
#define PROTO_LINE_SIZE         64

/*=========================================================================
 * Global Variables (declared extern, defined in main.c)
 *=========================================================================*/
extern uint32_t g_ui32SysClock;

/* Systick timing flags and counters */
extern volatile uint8_t  g_flag_1ms;
extern volatile uint8_t  g_flag_10ms;
extern volatile uint16_t g_cnt_10ms;
extern volatile uint8_t  g_flag_100ms;
extern volatile uint8_t  g_cnt_100ms;
extern volatile uint8_t  g_flag_1000ms;
extern volatile uint8_t  g_cnt_1000ms;
extern volatile uint32_t g_uptime_seconds;
extern volatile uint16_t g_beep_timeout;
extern volatile uint8_t  g_msg_timeout;   /* weather msg auto-revert (seconds) */

/*=========================================================================
 * Shared Utility Function Declarations (defined in main.c)
 *=========================================================================*/
void     Delay(uint32_t value);
uint8_t  I2C0_WriteByte(uint8_t DevAddr, uint8_t RegAddr, uint8_t WriteData);
uint8_t  I2C0_ReadByte(uint8_t DevAddr, uint8_t RegAddr);
void     UART0_SendChar(char c);
void     UART0_SendString(const char *str);
void     UART0_SendBuf(const char *buf, uint16_t len);

#endif /* __HW_CONFIG_H__ */
