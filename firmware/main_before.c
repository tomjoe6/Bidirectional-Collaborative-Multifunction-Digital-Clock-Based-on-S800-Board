/*=========================================================================*/
/*  S800 Smart Clock — All custom code in single main.c                     */
/*=========================================================================*/

#include "alarm.h"
#include "buzzer.h"
#include "clock.h"
#include "debug.h"
#include "display.h"
#include "events.h"
#include "gpio.h"
#include "hw_config.h"
#include "hw_gpio.h"
#include "hw_i2c.h"
#include "hw_ints.h"
#include "hw_memmap.h"
#include "hw_types.h"
#include "i2c.h"
#include "interrupt.h"
#include "keys.h"
#include "led.h"
#include "pin_map.h"
#include "protocol.h"
#include "sysctl.h"
#include "systick.h"
#include "timer.h"
#include "uart.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*=========================================================================
 * Global Variable Definitions
 *=========================================================================*/
uint32_t g_ui32SysClock;

/* Systick timing */
volatile uint8_t  g_flag_1ms;
volatile uint8_t  g_flag_10ms;
volatile uint16_t g_cnt_10ms;
volatile uint8_t  g_flag_100ms;
volatile uint8_t  g_cnt_100ms;
volatile uint8_t  g_flag_1000ms;
volatile uint8_t  g_cnt_1000ms;
volatile uint32_t g_uptime_seconds;

/* Beep timeout (in 10ms ticks), used by *SET:BEEP command */
volatile uint16_t g_beep_timeout;
volatile uint8_t  g_led_user_lock; /* *SET:LED user-override (10ms ticks) */

/* Boot animation state */
static uint8_t g_boot_phase;
static uint16_t g_boot_timer;

/*=========================================================================
 * Utility Functions
 *=========================================================================*/

/* Simple busy-wait delay */
void Delay(uint32_t value)
{
    uint32_t ui32Loop;
    for (ui32Loop = 0; ui32Loop < value; ui32Loop++) {};
}

/* Send a single character over UART0 */
void UART0_SendChar(char c)
{
    UARTCharPut(UART0_BASE, c);
}

/* Send a null-terminated string over UART0 */
void UART0_SendString(const char *str)
{
    while (*str) {
        UARTCharPut(UART0_BASE, *str++);
    }
}

/* Send a buffer of specified length over UART0 */
void UART0_SendBuf(const char *buf, uint16_t len)
{
    uint16_t i;
    for (i = 0; i < len; i++) {
        UARTCharPut(UART0_BASE, buf[i]);
    }
}

/*=========================================================================
 * I2C Utility Functions (from exp2.c)
 *=========================================================================*/

uint8_t I2C0_WriteByte(uint8_t DevAddr, uint8_t RegAddr, uint8_t WriteData)
{
    uint8_t rop;

    while (I2CMasterBusy(I2C0_BASE)) {};
    I2CMasterSlaveAddrSet(I2C0_BASE, DevAddr, false);
    I2CMasterDataPut(I2C0_BASE, RegAddr);
    I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_BURST_SEND_START);
    while (I2CMasterBusy(I2C0_BASE)) {};
    rop = (uint8_t)I2CMasterErr(I2C0_BASE);

    I2CMasterDataPut(I2C0_BASE, WriteData);
    I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_BURST_SEND_FINISH);
    while (I2CMasterBusy(I2C0_BASE)) {};

    rop = (uint8_t)I2CMasterErr(I2C0_BASE);
    return rop;
}

uint8_t I2C0_ReadByte(uint8_t DevAddr, uint8_t RegAddr)
{
    uint8_t value;

    while (I2CMasterBusy(I2C0_BASE)) {};
    I2CMasterSlaveAddrSet(I2C0_BASE, DevAddr, false);
    I2CMasterDataPut(I2C0_BASE, RegAddr);
    /* BURST_SEND_START: sends START+addr+data WITHOUT STOP, keeping
     * the bus active for a repeated START in read direction. */
    I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_BURST_SEND_START);
    while (I2CMasterBusy(I2C0_BASE)) {};

    I2CMasterSlaveAddrSet(I2C0_BASE, DevAddr, true);
    I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_BURST_RECEIVE_START);
    while (I2CMasterBusy(I2C0_BASE)) {};
    value = (uint8_t)I2CMasterDataGet(I2C0_BASE);
    I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_BURST_RECEIVE_FINISH);
    while (I2CMasterBusy(I2C0_BASE)) {};

    return value;
}

/*=========================================================================
 * Hardware Initialization Functions
 *=========================================================================*/

static void S800_GPIO_Init(void)
{
    /* Enable GPIO port for USER key inputs (USERSW1=PJ0, USERSW2=PJ1).
     * All K1-K8 are on TCA6424 Port0 via the expansion board. */
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOJ);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOJ));

    /* USER1=PJ0, USER2=PJ1: input with pull-up */
    GPIOPinTypeGPIOInput(USER1_GPIO_PORT, USER1_GPIO_PIN | USER2_GPIO_PIN);
    GPIOPadConfigSet(USER1_GPIO_PORT, USER1_GPIO_PIN | USER2_GPIO_PIN,
                     GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);
}

static void S800_I2C0_Init(void)
{
    uint8_t result;

    SysCtlPeripheralEnable(SYSCTL_PERIPH_I2C0);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOB));

    GPIOPinConfigure(GPIO_PB2_I2C0SCL);
    GPIOPinConfigure(GPIO_PB3_I2C0SDA);
    GPIOPinTypeI2CSCL(GPIO_PORTB_BASE, GPIO_PIN_2);
    GPIOPinTypeI2C(GPIO_PORTB_BASE, GPIO_PIN_3);

    I2CMasterInitExpClk(I2C0_BASE, g_ui32SysClock, true);
    I2CMasterEnable(I2C0_BASE);

    /* Configure TCA6424:
     *   Port0: all inputs (extended keys)
     *   Port1: all outputs (segment data)
     *   Port2: all outputs (digit select)
     */
    result  = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_CONFIG_PORT0, 0xFF);
    result |= I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_CONFIG_PORT1, 0x00);
    result |= I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_CONFIG_PORT2, 0x00);
    /* Enable pull-ups on Port0 inputs so keys don't float.
     * 0x46 = Pull-Up Enable Port0 (TCA6424A). If the chip does
     * not support this register the write is safely ignored. */
    result |= I2C0_WriteByte(TCA6424_I2CADDR, 0x46, 0xFF);

    /* Configure PCA9557: all outputs */
    result |= I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_CONFIG, 0x00);
    result |= I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, 0xFF);

    (void)result;
}

static void S800_UART_Init(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOA));

    GPIOPinConfigure(GPIO_PA0_U0RX);
    GPIOPinConfigure(GPIO_PA1_U0TX);
    GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);

    UARTConfigSetExpClk(UART0_BASE, g_ui32SysClock,
                        UART_BAUD_RATE, UART_CONFIG_VAL);
    UARTFIFOLevelSet(UART0_BASE, UART_FIFO_TX2_8, UART_FIFO_RX1_8);

    IntEnable(INT_UART0);
    UARTIntEnable(UART0_BASE, UART_INT_RX | UART_INT_RT);
}

/*=========================================================================
 * System Initialization
 *=========================================================================*/

static void System_Init(void)
{
    /* Configure system clock: 20MHz from 16MHz XTAL with PLL */
    g_ui32SysClock = SysCtlClockFreqSet(
        (SYSCTL_XTAL_16MHZ | SYSCTL_OSC_INT | SYSCTL_USE_PLL | SYSCTL_CFG_VCO_480),
        SYSTEM_CLOCK_HZ);

    /* Configure SysTick for 1ms period */
    SysTickPeriodSet(g_ui32SysClock / SYSTICK_FREQUENCY);
    SysTickEnable();
    SysTickIntEnable();

    /* Initialize timing globals */
    g_flag_1ms     = 0;
    g_flag_10ms    = 0;
    g_cnt_10ms     = (uint16_t)(SYSTICK_FREQUENCY / 100 - 1); /* 9 */
    g_flag_100ms   = 0;
    g_cnt_100ms    = 9;
    g_flag_1000ms  = 0;
    g_cnt_1000ms   = 9;
    g_uptime_seconds = 0;
    g_beep_timeout = 0;

    /* Initialize hardware */
    S800_GPIO_Init();
    S800_I2C0_Init();
    S800_UART_Init();

    /* Enable global interrupts */
    IntMasterEnable();

    /* Initialize all firmware modules */
    Clock_Init(24, 1, 1, 0, 0, 0);  /* 2024-01-01 00:00:00 */
    Alarm_Init();
    Display_Init();
    Keys_Init();
    Buzzer_Init();
    LED_Init();
    Protocol_Init();
    Events_Init();

    /* Set initial LED day/night state (DAY) */
    LED_Set(LED_DAYNIGHT, 1);

    /* Set alarm enabled LED */
    LED_Set(LED_ALARM_EN, g_alarm.enabled);
}

/*=========================================================================
 * Boot Screen Animation (PRD Section 3.1)
 *
 * Phase sequence:
 *   0: All 8 digits + 8 LEDs full ON, delay
 *   1: All OFF, delay (flash cycle)
 *   2: Repeat flash (back to ON)
 *   3: All OFF
 *   4: Show student ID last 8 digits, LEDs on, flash
 *   5: Show name pinyin, LEDs on, flash
 *   6: Show software version, delay >= 1s
 *   7: Boot complete, enter normal mode
 *=========================================================================*/

#define BOOT_STUDENT_ID     "31910727"     /* Last 8 digits of student ID  */
#define BOOT_NAME_PINYIN    "CUIJNTNG"     /* Name pinyin (<=8 chars)      */
#define BOOT_VERSION        " V-100  "     /* Software version */

#define BOOT_FLASH_ON_MS    600             /* All-segments ON duration    */
#define BOOT_FLASH_OFF_MS   400             /* Blank gap between phases    */
#define BOOT_SHOW_MS        1500            /* Student ID / Name on time   */
#define BOOT_VERSION_MS     2000            /* Version display >=1s        */

static void BootAnimation_Init(void)
{
    g_boot_phase = 0;
    g_boot_timer = 0;

    /* All LEDs on, all segments on */
    g_led_state = 0xFF;
    I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, 0x00); /* all LED on */

    {
        uint8_t j;
        for (j = 0; j < DISP_DIGITS; j++) {
            g_disp_chars[j] = '8'; /* show 8 on all digits */
        }
        g_dp_mask = 0xFF;
    }
}

static uint8_t BootAnimation_Run(void)
{
    uint8_t i;

    g_boot_timer++;

    switch (g_boot_phase) {

    case 0: /* All ON phase */
        if (g_boot_timer >= BOOT_FLASH_ON_MS / 10) {
            g_boot_timer = 0;
            g_boot_phase = 1;
            /* All OFF */
            g_led_state = 0x00;
            I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, 0xFF);
            for (i = 0; i < DISP_DIGITS; i++) {
                g_disp_chars[i] = ' ';
            }
            g_dp_mask = 0x00;
        }
        break;

    case 1: /* All OFF phase */
        if (g_boot_timer >= BOOT_FLASH_OFF_MS / 10) {
            g_boot_timer = 0;
            g_boot_phase = 2;
            /* All ON again (second flash) */
            g_led_state = 0xFF;
            I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, 0x00);
            for (i = 0; i < DISP_DIGITS; i++) {
                g_disp_chars[i] = '8';
            }
            g_dp_mask = 0xFF;
        }
        break;

    case 2: /* Second ON phase */
        if (g_boot_timer >= BOOT_FLASH_ON_MS / 10) {
            g_boot_timer = 0;
            g_boot_phase = 3;
            /* All OFF */
            g_led_state = 0x00;
            I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, 0xFF);
            for (i = 0; i < DISP_DIGITS; i++) {
                g_disp_chars[i] = ' ';
            }
            g_dp_mask = 0x00;
        }
        break;

    case 3: /* Pre-student-ID blank */
        if (g_boot_timer >= BOOT_FLASH_OFF_MS / 10) {
            g_boot_timer = 0;
            g_boot_phase = 4;
            /* Show student ID + LEDs sync flash */
            {
                uint8_t slen;
                slen = (uint8_t)strlen(BOOT_STUDENT_ID);
                for (i = 0; i < DISP_DIGITS; i++) {
                    if (i < slen) {
                        g_disp_chars[i] = BOOT_STUDENT_ID[i];
                    } else {
                        g_disp_chars[i] = ' ';
                    }
                }
            }
            g_dp_mask = 0x00;
            g_led_state = 0xFF;
            I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, 0x00);
        }
        break;

    case 4: /* Student ID display */
        if (g_boot_timer >= BOOT_SHOW_MS / 10) {
            g_boot_timer = 0;
            g_boot_phase = 5;
            /* Flash off briefly, then show name */
            g_led_state = 0x00;
            I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, 0xFF);
            for (i = 0; i < DISP_DIGITS; i++) {
                g_disp_chars[i] = ' ';
            }
            g_dp_mask = 0x00;
        }
        break;

    case 5: /* Show name pinyin */
        {
            /* Wait a short gap then show name */
            static uint8_t name_shown = 0;
            if (!name_shown) {
                if (g_boot_timer >= BOOT_FLASH_OFF_MS / 10) {
                    uint8_t nlen;
                    nlen = (uint8_t)strlen(BOOT_NAME_PINYIN);
                    for (i = 0; i < DISP_DIGITS; i++) {
                        if (i < nlen) {
                            g_disp_chars[i] = BOOT_NAME_PINYIN[i];
                        } else {
                            g_disp_chars[i] = ' ';
                        }
                    }
                    g_dp_mask = 0x00;
                    g_led_state = 0xFF;
                    I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, 0x00);
                    g_boot_timer = 0;
                    name_shown = 1;
                }
            } else {
                if (g_boot_timer >= BOOT_SHOW_MS / 10) {
                    g_boot_timer = 0;
                    g_boot_phase = 6;
                    name_shown = 0;
                    /* Flash off */
                    g_led_state = 0x00;
                    I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, 0xFF);
                    for (i = 0; i < DISP_DIGITS; i++) {
                        g_disp_chars[i] = ' ';
                    }
                    g_dp_mask = 0x00;
                }
            }
        }
        break;

    case 6: /* Show software version */
        {
            static uint8_t ver_shown = 0;
            if (!ver_shown) {
                if (g_boot_timer >= BOOT_FLASH_OFF_MS / 10) {
                    uint8_t vlen;
                    vlen = (uint8_t)strlen(BOOT_VERSION);
                    for (i = 0; i < DISP_DIGITS; i++) {
                        if (i < vlen) {
                            g_disp_chars[i] = BOOT_VERSION[i];
                        } else {
                            g_disp_chars[i] = ' ';
                        }
                    }
                    g_dp_mask = 0x00;
                    g_led_state = 0xFF;
                    I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, 0x00);
                    g_boot_timer = 0;
                    ver_shown = 1;
                }
            } else {
                if (g_boot_timer >= BOOT_VERSION_MS / 10) {
                    g_boot_timer = 0;
                    g_boot_phase = 7;
                    ver_shown = 0;
                    /* Blank display + LEDs for transition to clock */
                    for (i = 0; i < DISP_DIGITS; i++) {
                        g_disp_chars[i] = ' ';
                    }
                    g_dp_mask = 0x00;
                    g_led_state = 0x00;
                    I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, 0xFF);
                }
            }
        }
        break;

    case 7: /* Post-version blank pause before clock */
        if (g_boot_timer >= (BOOT_FLASH_OFF_MS / 10)) {
            return 1; /* Boot done — main loop will init clock display */
        }
        break;
    }

    return 0; /* Boot still in progress */
}

/*=========================================================================
 * Update the edit mode display (called during edit in main loop).
 * Shows the edit temp values with blinking on the current field.
 *=========================================================================*/
static void UpdateEditDisplay(void)
{
    char buf[16];
    char disp8[DISP_DIGITS + 1];
    uint8_t i;
    uint8_t dp;
    uint8_t field_start = 0;
    uint8_t field_len = 0;

    Keys_GetEditDisplay(buf, sizeof(buf));

    /* Parse the formatted edit string into 8 display positions */
    /* Format: "YY.MM.DD" or "HH.MM.SS" where dots use the DP bit */
    {
        uint8_t src_idx;
        uint8_t dst_idx;

        src_idx = 0;
        dst_idx = 0;
        dp      = 0;
        while (src_idx < (uint8_t)strlen(buf) && dst_idx < DISP_DIGITS) {
            if (buf[src_idx] == '.') {
                /* Set DP on previous digit */
                if (dst_idx > 0) {
                    dp |= (uint8_t)(1 << (dst_idx - 1));
                }
                src_idx++;
            } else {
                disp8[dst_idx] = buf[src_idx];
                src_idx++;
                dst_idx++;
            }
        }
        while (dst_idx < DISP_DIGITS) {
            disp8[dst_idx] = ' ';
            dst_idx++;
        }
    }
    disp8[DISP_DIGITS] = '\0';

    /* Determine which field is being edited and blink it */
    if (g_edit_blink) {
        /* During blink "off" phase, blank the current field */

        if (g_edit_state == EDIT_DATE) {
            if (g_edit_field == DATE_FIELD_YEAR)  { field_start = 0; field_len = 2; }
            if (g_edit_field == DATE_FIELD_MONTH) { field_start = 2; field_len = 2; }
            if (g_edit_field == DATE_FIELD_DAY)   { field_start = 4; field_len = 2; }
        } else if (g_edit_state == EDIT_TIME) {
            if (g_edit_field == TIME_FIELD_HOUR)   { field_start = 0; field_len = 2; }
            if (g_edit_field == TIME_FIELD_MINUTE) { field_start = 2; field_len = 2; }
            if (g_edit_field == TIME_FIELD_SECOND) { field_start = 4; field_len = 2; }
        } else if (g_edit_state == EDIT_ALARM) {
            if (g_edit_field == ALARM_FIELD_HOUR)   { field_start = 0; field_len = 2; }
            if (g_edit_field == ALARM_FIELD_MINUTE) { field_start = 2; field_len = 2; }
            if (g_edit_field == ALARM_FIELD_SECOND) { field_start = 4; field_len = 2; }
            if (g_edit_field == ALARM_FIELD_DAY)    { field_start = 6; field_len = 2; }
        } else {
            field_start = 0; field_len = 0;
        }

        /* Blank the field */
        for (i = field_start; i < field_start + field_len && i < DISP_DIGITS; i++) {
            disp8[i] = ' ';
        }
    }

    /* Copy to global display chars */
    for (i = 0; i < DISP_DIGITS; i++) {
        g_disp_chars[i] = disp8[i];
    }
    g_dp_mask = dp;
}

/*=========================================================================
 * Main Program
 *=========================================================================*/

int main(void)
{
    /* C89/C90: all variables declared at function start */
    uint8_t  scan_idx;
    uint8_t  boot_done;
    uint16_t boot_guard;    /* post-boot mode/format lock (10ms ticks) */
    uint8_t  key_id;
    uint8_t  is_long;
    uint8_t  edit_state;
    uint8_t  alarm_was_ringing;
    uint8_t  alarm_now_ringing;
    ClockTime clock_now;
    uint16_t beep_timer;
    uint8_t  i;

    /* Initialize */
    scan_idx          = 0;
    boot_done         = 0;
    key_id            = 0;
    is_long           = 0;
    edit_state        = EDIT_NONE;
    alarm_was_ringing = 0;
    alarm_now_ringing = 0;
    memset(&clock_now, 0, sizeof(clock_now));
    beep_timer        = 0;

    System_Init();
    BootAnimation_Init();

    /* Main loop */
    while (1) {

        /*-----------------------------------------------------------------
         * Process UART commands (only after boot completes)
         *-----------------------------------------------------------------*/
        if (boot_done) {
            Protocol_Process();
        }

        /*-----------------------------------------------------------------
         * 1ms Tasks: Display scan
         *-----------------------------------------------------------------*/
        if (g_flag_1ms) {
            g_flag_1ms = 0;

            if (boot_done) {
                Display_Scan(scan_idx);
            } else {
                /* During boot, still scan the display manually */
                Display_Scan(scan_idx);
            }

            scan_idx++;
            if (scan_idx >= DISP_DIGITS) {
                scan_idx = 0;
            }
        }

        /*-----------------------------------------------------------------
         * 10ms Tasks: Key scan, buzzer rhythm, LED flash timeout,
         *             beep timeout, boot animation
         *-----------------------------------------------------------------*/
        if (g_flag_10ms) {
            g_flag_10ms = 0;

            if (boot_done) {
                /* Key scanning */
                Keys_Scan10ms();

                /* Handle key events */
                while (Keys_GetEvent(&key_id, &is_long)) {
                    /* Report key event to PC */
                    Events_ReportKey(key_id);
                }

                /* Post-boot guard: force TIME+LEFT and re-render
                 * immediately so any spurious FORMAT/DISP key events
                 * are visually corrected within 10ms. Only active
                 * when NOT editing (user edits have priority). */
                /* Post-boot guard: suppress spurious key events
                 * from floating TCA6424 by forcing TIME+LEFT
                 * for the first 1.5s. Without this, K5/K7 floating
                 * immediately corrupt mode/format after boot. */
                if (boot_guard > 0) {
                    boot_guard--;
                    if (Keys_GetEditState() == EDIT_NONE) {
                        g_disp_mode   = DISP_MODE_TIME;
                        g_disp_format = FORMAT_LEFT;
                        Display_UpdateFromClock(&g_clock);
                    }
                }

                /* Edit state LED indicator (skip during user LED lock) */
                edit_state = Keys_GetEditState();
                if (g_led_user_lock == 0) {
                    LED_Set(LED_EDIT_ACTIVE, (uint8_t)(edit_state != EDIT_NONE ? 1 : 0));
                }

                /* Update edit display if in edit mode */
                if (edit_state != EDIT_NONE) {
                    UpdateEditDisplay();
                }

                /* Buzzer rhythm handler */
                alarm_now_ringing = Alarm_IsRinging();
                if (alarm_now_ringing && !alarm_was_ringing) {
                    Buzzer_StartRing();
                    Events_ReportAlarm(1);
                }
                if (!alarm_now_ringing && alarm_was_ringing) {
                    Buzzer_StopRing();
                    Events_ReportAlarm(0);
                }
                alarm_was_ringing = alarm_now_ringing;

                if (alarm_now_ringing) {
                    Buzzer_RhythmHandler();
                }

                /* Beep timeout for *SET:BEEP */
                if (g_beep_timeout > 0) {
                    g_beep_timeout--;
                    if (g_beep_timeout == 0) {
                        Buzzer_Off();
                    }
                }

                {
                    uint8_t led_out;
                    led_out = (uint8_t)(~g_led_state);
                    if (g_disp_night == MODE_NIGHT)
                        led_out = (uint8_t)((led_out & 0x01) | 0xFE);
                    I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, led_out);
                }

                /* User LED lock timeout */
                if (g_led_user_lock > 0) g_led_user_lock--;

                /* LED flash timeout */
                LED_UpdateFlashTimeout();

                /* Event queue sending (rate-limited to 1 per 50ms internally) */
                if (Events_HasPending()) {
                    Events_SendNext();
                }

                /* Update flow counter for scroll */
                if (g_disp_mode == DISP_MODE_FULL || g_disp_mode == DISP_MODE_YEAR) {
                    g_flow_counter++;
                    if (g_flow_counter >= g_flow_delay) {
                        g_flow_counter = 0;
                        Display_FlowAdvance();
                        if (g_disp_on) {
                            char fd[9]; uint8_t fj;
                            for (fj = 0; fj < 8; fj++) {
                                char c = g_disp_chars[fj];
                                fd[fj] = (c == ' ') ? '_' : c;
                            }
                            fd[8] = '\0';
                            Events_ReportDisp(fd, g_dp_mask);
                        }
                    }
                }

            } else {
                /* Boot animation runs on 10ms ticks */
                boot_done = BootAnimation_Run();
                if (boot_done) {
                    /* Transition to normal clock display:
                     * Explicitly reset all display state to safe defaults,
                     * then fill from clock. */
                    g_disp_mode   = DISP_MODE_TIME;
                    g_disp_format = FORMAT_LEFT;
                    g_disp_night  = MODE_DAY;
                    g_disp_on     = 1;
                    boot_guard    = 150; /* lock format for 1.5s (150*10ms) */
                    Display_UpdateFromClock(&g_clock);
                    Keys_Init();
                }
            }
        }

        /*-----------------------------------------------------------------
         * 100ms Tasks: (reserved for future use)
         *-----------------------------------------------------------------*/
        if (g_flag_100ms) {
            g_flag_100ms = 0;

            /* No 100ms-specific tasks currently.
             * Event sending moved to 10ms handler for correct rate limiting.
             */
        }

        /*-----------------------------------------------------------------
         * 1000ms Tasks: Clock tick, alarm check, heartbeat LED,
         *               display update, event heartbeats
         *-----------------------------------------------------------------*/
        if (g_flag_1000ms) {
            g_flag_1000ms = 0;

            if (boot_done) {
                /* Advance clock by one second */
                Clock_Tick();

                /* Update uptime */
                g_uptime_seconds++;

                /* Copy clock for alarm check */
                clock_now = g_clock;

                /* Check alarm */
                Alarm_Check(&clock_now);

                /* System LED updates — skip when user has override lock */
                if (g_led_user_lock == 0) {
                    if (g_alarm.enabled) {
                        LED_Set(LED_ALARM_EN, 1);
                    } else {
                        LED_Set(LED_ALARM_EN, 0);
                    }
                    if (Alarm_IsRinging()) {
                        LED_Set(LED_ALARM_RING, 1);
                    } else {
                        LED_Set(LED_ALARM_RING, 0);
                    }
                    LED_Heartbeat();
                }

                /* Update display from clock (if not in edit mode). */
                if (boot_done && Keys_GetEditState() == EDIT_NONE) {
                    Display_UpdateFromClock(&clock_now);
                }
                /* Else: edit display already updated in 10ms handler */

                /* Event heartbeats (DISP + LED every 1s).
                 * In NIGHT mode, report only D0 (heartbeat) to the twin. */
                {
                    char disp_str[9];
                    uint8_t j, led_for_evt;
                    if (g_disp_on) {
                        for (j = 0; j < DISP_DIGITS; j++) {
                            char c = g_disp_chars[j];
                            disp_str[j] = (c == ' ') ? '_' : c;
                        }
                    } else {
                        for (j = 0; j < DISP_DIGITS; j++)
                            disp_str[j] = ' ';
                    }
                    disp_str[8] = '\0';
                    led_for_evt = g_led_state;
                    if (g_disp_night == MODE_NIGHT)
                        led_for_evt &= 0x01;
                    Events_1HzHandler(&clock_now, led_for_evt,
                                      disp_str, g_dp_mask);
                }
            }

            /* Suppress unused variable warning */
            beep_timer = g_beep_timeout;
            (void)beep_timer;
            (void)i;
        }
    }

    /* main() never returns in embedded systems */
}

/*=========================================================================
 * Interrupt Service Routines
 *=========================================================================*/

/* SysTick interrupt handler: 1ms timebase */
void SysTick_Handler(void)
{
    g_flag_1ms = 1;

    /* 10ms counter */
    if (g_cnt_10ms > 0) {
        g_cnt_10ms--;
    } else {
        g_cnt_10ms = (uint16_t)(SYSTICK_FREQUENCY / 100 - 1); /* 9 */
        g_flag_10ms = 1;

        /* 100ms counter (counts 10ms ticks) */
        if (g_cnt_100ms > 0) {
            g_cnt_100ms--;
        } else {
            g_cnt_100ms = 9;
            g_flag_100ms = 1;

            /* 1000ms counter (counts 100ms ticks) */
            if (g_cnt_1000ms > 0) {
                g_cnt_1000ms--;
            } else {
                g_cnt_1000ms = 9;
                g_flag_1000ms = 1;
            }
        }
    }
}

/* UART0 interrupt handler: receive characters */
void UART0_Handler(void)
{
    uint32_t ui32Status;
    int32_t recv_char;

    ui32Status = UARTIntStatus(UART0_BASE, true);
    UARTIntClear(UART0_BASE, ui32Status);

    while (UARTCharsAvail(UART0_BASE)) {
        recv_char = UARTCharGetNonBlocking(UART0_BASE);
        if (recv_char >= 0) {
            Protocol_CharReceived((uint8_t)recv_char);
        }
    }
}

/*=========================================================================*/
/*  Module: clock.c                                                      */
/*=========================================================================*/


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

/*=========================================================================*/
/*  Module: alarm.c                                                      */
/*=========================================================================*/


/*=========================================================================
 * Global alarm instance. Default: disabled, 07:00:00, every day.
 *=========================================================================*/
AlarmState g_alarm;

/*=========================================================================
 * Initialize alarm to defaults (disabled, 07:00:00, every day).
 *=========================================================================*/
void Alarm_Init(void)
{
    g_alarm.hour    = 7;
    g_alarm.minute  = 0;
    g_alarm.second  = 0;
    g_alarm.day     = 0;    /* 0 = every day */
    g_alarm.enabled = 0;
    g_alarm.ringing = 0;
}

/*=========================================================================
 * Check if the current time matches the alarm trigger.
 * Called once per second. If alarm is enabled and time matches,
 * start ringing.
 *=========================================================================*/
void Alarm_Check(ClockTime *now)
{
    uint8_t match;

    if (!g_alarm.enabled) {
        return;
    }

    /* Already ringing: do not re-trigger */
    if (g_alarm.ringing) {
        return;
    }

    match = 0;
    if (now->hour   == g_alarm.hour   &&
        now->minute == g_alarm.minute &&
        now->second == g_alarm.second) {

        if (g_alarm.day == 0) {
            /* Every day */
            match = 1;
        } else if (g_alarm.day == now->day) {
            /* Specific day of month */
            match = 1;
        }
    }

    if (match) {
        g_alarm.ringing = 1;
    }
}

/*=========================================================================
 * Stop the alarm (turn off ringing, but do not disable the alarm).
 *=========================================================================*/
void Alarm_Stop(void)
{
    g_alarm.ringing = 0;
}

/*=========================================================================
 * Set the alarm time and day.
 * d=0 means every day; d=1-31 means specific day of month.
 * Setting the alarm automatically enables it.
 *=========================================================================*/
void Alarm_Set(uint8_t h, uint8_t m, uint8_t s, uint8_t d)
{
    g_alarm.hour    = h;
    g_alarm.minute  = m;
    g_alarm.second  = s;
    g_alarm.day     = d;
    g_alarm.enabled = 1;
    g_alarm.ringing = 0;
}

/*=========================================================================
 * Return 1 if the alarm is currently ringing.
 *=========================================================================*/
uint8_t Alarm_IsRinging(void)
{
    return g_alarm.ringing;
}

/*=========================================================================*/
/*  Module: display.c                                                    */
/*=========================================================================*/


/*=========================================================================
 * 7-Segment Code Tables (from exp2.c)
 *=========================================================================*/
const uint8_t g_seg_table_num[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

const uint8_t g_seg_table_alpha[26] = {
    0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71,   /* A B C D E F */
    0x3D, 0x76, 0x30, 0x1E, 0x7A, 0x38,   /* G H I J K L */
    0x55, 0x37, 0x3F, 0x73, 0x67, 0x70,   /* M N O P Q R */
    0x6D, 0x78, 0x3E, 0x7E, 0x6A, 0x36,   /* S T U V W X */
    0x6E, 0x49                              /* Y Z */
};

/*=========================================================================
 * Global Display State
 *=========================================================================*/
char     g_disp_buffer[DISP_BUF_SIZE] = "S800CLK";
uint8_t  g_disp_mode    = DISP_MODE_TIME;
uint8_t  g_disp_format  = FORMAT_LEFT;
uint8_t  g_disp_on      = 1;
uint8_t  g_disp_night   = MODE_DAY;
uint8_t  g_dp_mask      = 0x00;
char     g_disp_chars[DISP_DIGITS] = {' ',' ',' ',' ',' ',' ',' ',' '};

int16_t  g_flow_position = 0;
int8_t   g_flow_direction = 1;
uint8_t  g_flow_speed    = FLOW_SPEED_SLOW;
uint16_t g_flow_delay    = FLOW_DELAY_SLOW;
uint16_t g_flow_counter  = 0;

/*=========================================================================
 * Convert a character to its 7-segment code.
 * Handles 0-9, A-Z/a-z, '-', ' ', DP bit.
 *=========================================================================*/
uint8_t Display_GetSegCode(char c)
{
    if (c >= '0' && c <= '9') return g_seg_table_num[c - '0'];
    if (c >= 'A' && c <= 'Z') return g_seg_table_alpha[c - 'A'];
    if (c >= 'a' && c <= 'z') return g_seg_table_alpha[c - 'a'];
    if (c == '.') return 0x80;  /* DP only — used by boot animation */
    if (c == '-') return 0x40;
    if (c == '_') return 0x08;
    if (c == '=') return 0x48;
    return 0x00; /* space and unknown chars = blank */
}

/*=========================================================================
 * Initialize display. Clear all digits.
 *=========================================================================*/
void Display_Init(void)
{
    uint8_t i;

    /* Blank the display */
    I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, 0x00);
    I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT1, 0x00);

    for (i = 0; i < DISP_DIGITS; i++) {
        g_disp_chars[i] = ' ';
    }

    g_dp_mask       = 0x00;
    g_disp_mode     = DISP_MODE_TIME;
    g_disp_format   = FORMAT_LEFT;
    g_disp_on       = 1;
    g_disp_night    = MODE_DAY;
    g_flow_position = 0;
    g_flow_direction= 1;
    g_flow_speed    = FLOW_SPEED_SLOW;
    g_flow_delay    = FLOW_DELAY_SLOW;
    g_flow_counter  = 0;
    g_disp_buffer[0] = '\0';
}

/*=========================================================================
 * Scan one digit to the display. Called every 1ms from main loop.
 * Uses I2C to write segment data and digit select to TCA6424.
 * The segment code ORs in the DP bit from g_dp_mask for this digit.
 * If display is off, blanks the digit.
 * In night mode, only digits 0-3 (HH.MM) are lit.
 *=========================================================================*/
void Display_Scan(uint8_t digit_idx)
{
    uint8_t seg_code;
    uint8_t digit_sel;
    char    ch;

    /* Validate digit index */
    if (digit_idx >= DISP_DIGITS) {
        return;
    }

    /* Turn off all digits first (prevents ghosting) */
    I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, 0x00);

    /* If display is off, leave it blank */
    if (!g_disp_on) {
        return;
    }

    /* Night mode: only digits 0-3 (hours:minutes) are active */
    if (g_disp_night == MODE_NIGHT && digit_idx >= 4) {
        return;
    }

    /* Get the character for this digit position */
    ch = g_disp_chars[digit_idx];

    /* Get base segment code (no DP) */
    seg_code = Display_GetSegCode(ch);

    /* Apply DP mask for this digit */
    if (g_dp_mask & (1 << digit_idx)) {
        seg_code |= 0x80;
    }

    /* Write segment data to Port1 */
    I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT1, seg_code);

    /* Enable this digit on Port2 */
    digit_sel = (uint8_t)(1 << digit_idx);
    I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, digit_sel);
}

/*=========================================================================
 * Fill the 8 display character slots from the display buffer,
 * applying flow position and direction.
 * If the buffer is shorter than 8 chars, pad with spaces.
 * Supports FORMAT RIGHT (reverse the visible window).
 *=========================================================================*/
void Display_FillFromBuffer(void)
{
    uint8_t str_len;
    int16_t vpos, i;

    str_len = (uint8_t)strlen(g_disp_buffer);

    for (i = 0; i < DISP_DIGITS; i++) {
        vpos = g_flow_position + i;
        if ((g_disp_mode == DISP_MODE_FULL || g_disp_mode == DISP_MODE_YEAR)
            && str_len > DISP_DIGITS) {
            while (vpos >= str_len) vpos -= str_len;
            while (vpos < 0)      vpos += str_len;
        }
        if (str_len > 0 && vpos >= 0 && vpos < str_len) {
            g_disp_chars[i] = g_disp_buffer[vpos];
        } else {
            g_disp_chars[i] = ' ';
        }
    }

    /* FORMAT_RIGHT: reverse the 8-digit window */
    if (g_disp_format == FORMAT_RIGHT) {
        char rev[DISP_DIGITS];
        for (i = 0; i < DISP_DIGITS; i++)
            rev[i] = g_disp_chars[DISP_DIGITS - 1 - i];
        for (i = 0; i < DISP_DIGITS; i++)
            g_disp_chars[i] = rev[i];
    }

    /* Compute g_dp_mask from which digits show a '.' (0x80).
     * This goes into *EVT:DISP as the 2-hex-digit DP byte. */
    g_dp_mask = 0;
    for (i = 0; i < DISP_DIGITS; i++) {
        if (g_disp_chars[i] == '.')
            g_dp_mask |= (uint8_t)(1 << i);
    }
}

/*=========================================================================
 * Update the display buffer from clock data based on current mode.
 * Called every 1 second (in 1000ms handler).
 *=========================================================================*/
void Display_UpdateFromClock(ClockTime *now)
{
    char buf[DISP_BUF_SIZE];
    uint8_t i;

    if (g_disp_mode == DISP_MODE_TIME) {
        /* Format: HH.MM.SS  → displays as H H. M M. S S (8 slots, dots at pos 1,3) */
        sprintf(buf, "%02d.%02d.%02d",
                now->hour, now->minute, now->second);
    } else if (g_disp_mode == DISP_MODE_DATE) {
        /* Format: YY.MM.DD */
        sprintf(buf, "%02d.%02d.%02d",
                now->year % 100, now->month, now->day);
    } else if (g_disp_mode == DISP_MODE_YEAR) {
        /* Format: YYYY.MMDD (9 chars) → flow-scrolls across 8 digits */
        sprintf(buf, "%04d.%02d%02d",
                2000 + now->year, now->month, now->day);
    } else {
        /* FULL mode: use the message buffer as-is */
        /* Buffer is already set by Display_SetBuffer */
        Display_FillFromBuffer();
        return;
    }

    /* In TIME/DATE/YEAR modes, copy the formatted string to disp_buffer
     * and reset flow to show from start. */
    for (i = 0; i < DISP_BUF_SIZE; i++) {
        if (i < (uint8_t)strlen(buf)) {
            g_disp_buffer[i] = buf[i];
        } else {
            g_disp_buffer[i] = '\0';
            break;
        }
    }
    g_disp_buffer[DISP_BUF_SIZE - 1] = '\0';

    /* Only reset flow when NOT in YEAR/FULL (scrolling) modes,
     * otherwise the flow position resets every second. */
    if (g_disp_mode != DISP_MODE_YEAR && g_disp_mode != DISP_MODE_FULL) {
        g_flow_position = 0;
    }

    Display_FillFromBuffer();
}

/*=========================================================================
 * Set the display mode (TIME, DATE, YEAR, FULL).
 *=========================================================================*/
void Display_SetMode(uint8_t mode)
{
    if (mode <= DISP_MODE_FULL) {
        g_disp_mode = mode;
    }
    /* Re-render immediately so DISP key feedback is instant */
    Display_UpdateFromClock(&g_clock);
}

/*=========================================================================
 * Set the display format (LEFT or RIGHT).
 *=========================================================================*/
void Display_SetFormat(uint8_t format)
{
    if (format == FORMAT_LEFT || format == FORMAT_RIGHT) {
        g_disp_format   = format;
        g_flow_direction = (format == FORMAT_LEFT) ? 1 : -1;
    }
    Display_FillFromBuffer();
}

/*=========================================================================
 * Set the display message buffer for FULL mode.
 * Copies up to DISP_BUF_SIZE-1 characters.
 *=========================================================================*/
void Display_SetBuffer(const char *str)
{
    uint8_t i;
    uint8_t len;

    len = (uint8_t)strlen(str);
    if (len >= DISP_BUF_SIZE) {
        len = DISP_BUF_SIZE - 1;
    }

    for (i = 0; i < len; i++) {
        g_disp_buffer[i] = str[i];
    }
    g_disp_buffer[len] = '\0';

    g_flow_position = 0;
    g_disp_mode = DISP_MODE_FULL;
    Display_FillFromBuffer();
}

/*=========================================================================
 * Advance the flow/scrolling position by one step.
 * Called every flow_delay * 10ms from main loop.
 * Wraps around when exceeding buffer bounds.
 *=========================================================================*/
void Display_FlowAdvance(void)
{
    int16_t str_len;

    if (!g_disp_on) return;
    if (g_disp_mode != DISP_MODE_FULL && g_disp_mode != DISP_MODE_YEAR) {
        return;
    }

    str_len = (int16_t)strlen(g_disp_buffer);
    if (str_len <= DISP_DIGITS) {
        return;
    }

    /* Circular scroll: advance by one position, wrap around the
     * buffer length so content loops seamlessly (tail connects to head). */
    g_flow_position = g_flow_position + g_flow_direction;

    if (g_flow_position >= str_len) {
        g_flow_position = 0;
    } else if (g_flow_position < 0) {
        g_flow_position = str_len - 1;
    }

    Display_FillFromBuffer();
}

/*=========================================================================
 * Set night mode: in night mode, only first 4 digits (HH.MM) are shown.
 *=========================================================================*/
void Display_SetNight(uint8_t night)
{
    g_disp_night = night;
}

/*=========================================================================*/
/*  Module: keys.c                                                       */
/*=========================================================================*/


/*=========================================================================
 * Key name strings (for protocol and events)
 *=========================================================================*/
static const char *g_key_names[NUM_KEYS] = {
    "FUNC", "SHIFT", "ADD", "SAVE", "DISP",
    "SPEED", "FORMAT", "EXT", "USER1", "USER2"
};

/*=========================================================================
 * Per-key state for debounce and long-press detection
 *=========================================================================*/
static uint8_t  g_key_raw[NUM_KEYS];       /* raw reading (0=pressed, 1=released) */
static uint8_t  g_key_state[NUM_KEYS];     /* stable debounced state */
static uint8_t  g_key_dcnt[NUM_KEYS];      /* debounce counter */
static uint16_t g_key_press_time[NUM_KEYS];/* how long pressed (10ms ticks) */
static uint8_t  g_key_event[NUM_KEYS];     /* pending event type (0=none, 1=short, 2=long) */
static uint8_t  g_key_event_count;         /* number of pending events */

/*=========================================================================
 * Edit state globals
 *=========================================================================*/
uint8_t  g_edit_state   = EDIT_NONE;
uint8_t  g_edit_field   = 0;
uint16_t g_edit_timeout = 0;
uint8_t  g_edit_blink   = 0;

/* Edit temporary values (hold edits until saved) */
static ClockTime g_edit_temp_time;
static AlarmState g_edit_temp_alarm;

/*=========================================================================
 * Read all 10 keys: K1-K4 from GPIO, K5-K8+USER1/USER2 from TCA6424 Port0.
 * Returns the raw state in g_key_raw array.
 *=========================================================================*/
static void Keys_ReadRaw(void)
{
    uint8_t port0_val;
    uint8_t result;

    /* USER1/USER2: main-board GPIO (PJ0, PJ1 per exp2.c) */
    g_key_raw[KEY_ID_USER1] = (GPIOPinRead(USER1_GPIO_PORT, USER1_GPIO_PIN) ? 1 : 0);
    g_key_raw[KEY_ID_USER2] = (GPIOPinRead(USER2_GPIO_PORT, USER2_GPIO_PIN) ? 1 : 0);

    /* K1-K8: expansion-board TCA6424 Port0 (SW1~SW8) */
    port0_val = I2C0_ReadByte(TCA6424_I2CADDR, TCA6424_INPUT_PORT0);
    result = port0_val;

    g_key_raw[KEY_ID_FUNC]   = (port0_val & (1 << KEY1_BIT)) ? 1 : 0;
    g_key_raw[KEY_ID_SHIFT]  = (port0_val & (1 << KEY2_BIT)) ? 1 : 0;
    g_key_raw[KEY_ID_ADD]    = (port0_val & (1 << KEY3_BIT)) ? 1 : 0;
    g_key_raw[KEY_ID_SAVE]   = (port0_val & (1 << KEY4_BIT)) ? 1 : 0;
    g_key_raw[KEY_ID_DISP]   = (port0_val & (1 << KEY5_BIT)) ? 1 : 0;
    g_key_raw[KEY_ID_SPEED]  = (port0_val & (1 << KEY6_BIT)) ? 1 : 0;
    g_key_raw[KEY_ID_FORMAT] = (port0_val & (1 << KEY7_BIT)) ? 1 : 0;
    g_key_raw[KEY_ID_EXT]    = (port0_val & (1 << KEY8_BIT)) ? 1 : 0;

    (void)result;
}

/*=========================================================================
 * Initialize key scanning state.
 *=========================================================================*/
void Keys_Init(void)
{
    uint8_t i;

    for (i = 0; i < NUM_KEYS; i++) {
        g_key_raw[i]        = 1;
        g_key_state[i]      = 1;
        g_key_dcnt[i]       = 0;
        g_key_press_time[i] = 0;
        g_key_event[i]      = KEY_EVENT_NONE;
    }
    g_key_event_count = 0;

    g_edit_state   = EDIT_NONE;
    g_edit_field   = 0;
    g_edit_timeout = 0;
    g_edit_blink   = 0;
}

/*=========================================================================
 * Load current clock/alarm values into edit temp buffer
 * based on current edit state.
 *=========================================================================*/
static void Keys_LoadEditTemp(void)
{
    if (g_edit_state == EDIT_DATE) {
        g_edit_temp_time = g_clock;
    } else if (g_edit_state == EDIT_TIME) {
        g_edit_temp_time = g_clock;
    } else if (g_edit_state == EDIT_ALARM) {
        g_edit_temp_alarm = g_alarm;
    }
}

/*=========================================================================
 * Save edit temp values to actual clock or alarm.
 *=========================================================================*/
static void Keys_SaveEdit(void)
{
    if (g_edit_state == EDIT_DATE || g_edit_state == EDIT_TIME) {
        g_clock = g_edit_temp_time;
        /* Recompute day of week */
        g_clock.day_of_week = 0; /* Will be recomputed; use a fixed approach */
    } else if (g_edit_state == EDIT_ALARM) {
        g_alarm = g_edit_temp_alarm;
        g_alarm.enabled = 1;
    }
}

/*=========================================================================
 * Get max value for the given edit field.
 *=========================================================================*/
static uint8_t Keys_GetFieldMax(void)
{
    if (g_edit_state == EDIT_DATE) {
        if (g_edit_field == DATE_FIELD_YEAR)  return 99;
        if (g_edit_field == DATE_FIELD_MONTH) return 12;
        if (g_edit_field == DATE_FIELD_DAY)
            return Clock_DaysInMonth(g_edit_temp_time.year, g_edit_temp_time.month);
    } else if (g_edit_state == EDIT_TIME) {
        if (g_edit_field == TIME_FIELD_HOUR)   return 23;
        if (g_edit_field == TIME_FIELD_MINUTE) return 59;
        if (g_edit_field == TIME_FIELD_SECOND) return 59;
    } else if (g_edit_state == EDIT_ALARM) {
        if (g_edit_field == ALARM_FIELD_HOUR)   return 23;
        if (g_edit_field == ALARM_FIELD_MINUTE) return 59;
        if (g_edit_field == ALARM_FIELD_SECOND) return 59;
        if (g_edit_field == ALARM_FIELD_DAY)    return 31;
    }
    return 59;
}

/*=========================================================================
 * Get min value for the given edit field.
 *=========================================================================*/
static uint8_t Keys_GetFieldMin(void)
{
    if (g_edit_state == EDIT_DATE) {
        if (g_edit_field == DATE_FIELD_MONTH) return 1;
        if (g_edit_field == DATE_FIELD_DAY)   return 1;
    }
    if (g_edit_state == EDIT_ALARM) {
        if (g_edit_field == ALARM_FIELD_DAY) return 0;
    }
    return 0;
}

/*=========================================================================
 * Increment the current edit field value with wrap-around.
 *=========================================================================*/
static void Keys_IncrementField(void)
{
    uint8_t max_val, min_val;
    uint8_t *pval;
    uint8_t i;

    max_val = Keys_GetFieldMax();
    min_val = Keys_GetFieldMin();

    pval = NULL;

    if (g_edit_state == EDIT_DATE || g_edit_state == EDIT_TIME) {
        if (g_edit_state == EDIT_DATE) {
            if (g_edit_field == DATE_FIELD_YEAR)  pval = &g_edit_temp_time.year;
            if (g_edit_field == DATE_FIELD_MONTH) pval = &g_edit_temp_time.month;
            if (g_edit_field == DATE_FIELD_DAY)   pval = &g_edit_temp_time.day;
        } else {
            if (g_edit_field == TIME_FIELD_HOUR)   pval = &g_edit_temp_time.hour;
            if (g_edit_field == TIME_FIELD_MINUTE) pval = &g_edit_temp_time.minute;
            if (g_edit_field == TIME_FIELD_SECOND) pval = &g_edit_temp_time.second;
        }
    } else if (g_edit_state == EDIT_ALARM) {
        if (g_edit_field == ALARM_FIELD_HOUR)   pval = &g_edit_temp_alarm.hour;
        if (g_edit_field == ALARM_FIELD_MINUTE) pval = &g_edit_temp_alarm.minute;
        if (g_edit_field == ALARM_FIELD_SECOND) pval = &g_edit_temp_alarm.second;
        if (g_edit_field == ALARM_FIELD_DAY)    pval = &g_edit_temp_alarm.day;
    }

    if (pval != NULL) {
        (*pval)++;
        if (*pval > max_val) {
            *pval = min_val;
        }
    }

    /* Update display buffer with current edit values */
    /* The display will be updated in the main loop during edit mode */
    (void)i; /* suppress warning */
}

/*=========================================================================
 * Move to the next field in the current edit mode.
 *=========================================================================*/
static void Keys_NextField(void)
{
    uint8_t max_fields;

    if (g_edit_state == EDIT_DATE) {
        max_fields = 3;
    } else if (g_edit_state == EDIT_TIME) {
        max_fields = 3;
    } else if (g_edit_state == EDIT_ALARM) {
        max_fields = 4;
    } else {
        return;
    }

    g_edit_field++;
    if (g_edit_field >= max_fields) {
        g_edit_field = 0;
    }
}

/*=========================================================================
 * Enter edit mode for the specified state.
 *=========================================================================*/
static void Keys_EnterEdit(uint8_t state)
{
    g_edit_state = state;
    g_edit_field = 0;
    g_edit_timeout = EDIT_TIMEOUT_MS / 10; /* convert ms to 10ms ticks */
    g_edit_blink = 0;
    Keys_LoadEditTemp();
}

/*=========================================================================
 * Exit edit mode without saving.
 *=========================================================================*/
void Keys_ExitEditNoSave(void)
{
    g_edit_state = EDIT_NONE;
    g_edit_field = 0;
    g_edit_timeout = 0;
    g_edit_blink = 0;
}

/*=========================================================================
 * Cycle edit state: NONE->DATE->TIME->ALARM->NONE
 * If alarm is ringing, stop alarm first.
 *=========================================================================*/
static void Keys_CycleFunc(void)
{
    /* If alarm is ringing, FUNC stops it (priority) */
    if (Alarm_IsRinging()) {
        Alarm_Stop();
        return;
    }

    if (g_edit_state == EDIT_NONE) {
        Keys_EnterEdit(EDIT_DATE);
    } else if (g_edit_state == EDIT_DATE) {
        Keys_EnterEdit(EDIT_TIME);
    } else if (g_edit_state == EDIT_TIME) {
        Keys_EnterEdit(EDIT_ALARM);
    } else {
        /* Exit edit mode */
        Keys_ExitEditNoSave();
    }
}

/*=========================================================================
 * Dispatch a key press event.
 *=========================================================================*/
static void Keys_Dispatch(uint8_t key_id, uint8_t is_long)
{
    /* Always reset timeout on any key press during edit */
    if (g_edit_state != EDIT_NONE) {
        g_edit_timeout = EDIT_TIMEOUT_MS / 10;
    }

    switch (key_id) {
    case KEY_ID_FUNC:
        if (is_long) {
            /* Long FUNC = SAVE + exit (like SAVE) */
            if (g_edit_state != EDIT_NONE) {
                Keys_SaveEdit();
                Keys_ExitEditNoSave();
            }
        } else {
            /* Short FUNC: cycle edit modes / stop alarm */
            Keys_CycleFunc();
        }
        break;

    case KEY_ID_SHIFT:
        if (g_edit_state != EDIT_NONE) {
            Keys_NextField();
        }
        break;

    case KEY_ID_ADD:
        if (g_edit_state != EDIT_NONE) {
            Keys_IncrementField();
            /* If long press, keep auto-repeating (handled by caller) */
        }
        break;

    case KEY_ID_SAVE:
        if (g_edit_state != EDIT_NONE) {
            Keys_SaveEdit();
            Keys_ExitEditNoSave();
        }
        break;

    case KEY_ID_DISP:
        /* Cycle display mode: TIME -> DATE -> YEAR -> TIME */
        {
            uint8_t next_mode;
            next_mode = g_disp_mode + 1;
            if (next_mode > DISP_MODE_YEAR) {
                next_mode = DISP_MODE_TIME;
            }
            Display_SetMode(next_mode);
        }
        break;

    case KEY_ID_SPEED:
        /* Toggle flow speed */
        if (g_flow_speed == FLOW_SPEED_SLOW) {
            g_flow_speed = FLOW_SPEED_FAST;
            g_flow_delay = FLOW_DELAY_FAST;
        } else {
            g_flow_speed = FLOW_SPEED_SLOW;
            g_flow_delay = FLOW_DELAY_SLOW;
        }
        break;

    case KEY_ID_FORMAT:
        /* Toggle format direction */
        if (g_disp_format == FORMAT_LEFT) {
            Display_SetFormat(FORMAT_RIGHT);
        } else {
            Display_SetFormat(FORMAT_LEFT);
        }
        break;

    case KEY_ID_EXT:
    case KEY_ID_USER1:
    case KEY_ID_USER2:
        /* These generate events only, no local action (handled by PC) */
        break;

    default:
        break;
    }
}

/*=========================================================================
 * Scan keys every 10ms. Handles debounce, long-press detection,
 * edit timeout, and queues key events.
 *=========================================================================*/
void Keys_Scan10ms(void)
{
    uint8_t i;
    uint8_t raw;
    uint8_t stable;

    Keys_ReadRaw();

    for (i = 0; i < NUM_KEYS; i++) {
        raw = g_key_raw[i];
        stable = g_key_state[i];

        if (raw != stable) {
            /* State change: increment or decrement debounce counter */
            if (g_key_dcnt[i] < DEBOUNCE_MS / 10) {
                g_key_dcnt[i]++;
            } else {
                /* Debounced: state has changed */
                g_key_state[i] = raw;
                g_key_dcnt[i] = 0;

                if (raw == 0) {
                    /* Key pressed */
                    g_key_press_time[i] = 0;
                } else {
                    /* Key released */
                    if (g_key_press_time[i] < LONG_PRESS_MS / 10) {
                        /* Short press */
                        if (g_key_event[i] == KEY_EVENT_NONE &&
                            g_key_event_count < 10) {
                            g_key_event[i] = KEY_EVENT_SHORT;
                            g_key_event_count++;
                        }
                    }
                    g_key_press_time[i] = 0;
                }
            }
        } else if (raw == 0) {
            /* Key held down */
            g_key_press_time[i]++;

            /* Detect long press */
            if (g_key_press_time[i] == LONG_PRESS_MS / 10) {
                if (g_key_event[i] == KEY_EVENT_NONE &&
                    g_key_event_count < 10) {
                    g_key_event[i] = KEY_EVENT_LONG;
                    g_key_event_count++;
                }
            }

            /* Auto-repeat for ADD key while held (every 100ms after initial) */
            if (i == KEY_ID_ADD && g_key_press_time[i] > LONG_PRESS_MS / 10) {
                if ((g_key_press_time[i] % 10) == 0) {
                    /* Queue repeat ADD events while in edit mode */
                    if (g_edit_state != EDIT_NONE) {
                        Keys_IncrementField();
                        g_edit_timeout = EDIT_TIMEOUT_MS / 10;
                    }
                }
            }

            /* Reset debounce counter while held */
            g_key_dcnt[i] = 0;
        } else {
            /* Key released and stable: reset press time */
            g_key_press_time[i] = 0;
        }
    }

    /* Edit timeout: decrement and auto-exit if timeout reached */
    if (g_edit_state != EDIT_NONE) {
        if (g_edit_timeout > 0) {
            g_edit_timeout--;
            if (g_edit_timeout == 0) {
                Keys_ExitEditNoSave();
            }
        }

        /* Blink timer: toggle every 500ms */
        {
            static uint8_t blink_timer = 0;
            blink_timer++;
            if (blink_timer >= 50) { /* 50 * 10ms = 500ms */
                blink_timer = 0;
                g_edit_blink = (uint8_t)(!g_edit_blink);
            }
        }
    }
}

/*=========================================================================
 * Get the next pending key event. Returns 1 if event available.
 * The event is consumed (cleared) when retrieved.
 *=========================================================================*/
uint8_t Keys_GetEvent(uint8_t *key_id, uint8_t *is_long)
{
    uint8_t i;

    for (i = 0; i < NUM_KEYS; i++) {
        if (g_key_event[i] != KEY_EVENT_NONE) {
            *key_id  = i;
            *is_long = (g_key_event[i] == KEY_EVENT_LONG) ? 1 : 0;

            /* Dispatch the key action locally */
            Keys_Dispatch(i, *is_long);

            /* Clear the event */
            g_key_event[i] = KEY_EVENT_NONE;
            if (g_key_event_count > 0) {
                g_key_event_count--;
            }
            return 1;
        }
    }
    return 0;
}

/*=========================================================================
 * Get current edit state (for LED and event reporting).
 *=========================================================================*/
uint8_t Keys_GetEditState(void)
{
    return g_edit_state;
}

/*=========================================================================
 * Look up the protocol name for a key ID.
 *=========================================================================*/
const char* Keys_GetKeyName(uint8_t key_id)
{
    if (key_id < NUM_KEYS) {
        return g_key_names[key_id];
    }
    return "UNKNOWN";
}

/*=========================================================================
 * Simulate a key press (from *SET:KEY command).
 * Directly dispatches the key action without physical debounce.
 *=========================================================================*/
void Keys_Simulate(uint8_t key_id)
{
    if (key_id < NUM_KEYS) {
        Keys_Dispatch(key_id, 0); /* 0 = short press */
    }
}

/*=========================================================================
 * Provide access to edit temp values for display update during editing.
 *=========================================================================*/
void Keys_GetEditDisplay(char *buf, uint8_t bufsize)
{
    if (g_edit_state == EDIT_DATE) {
        sprintf(buf, "%02d.%02d.%02d",
                 g_edit_temp_time.year % 100,
                 g_edit_temp_time.month,
                 g_edit_temp_time.day);
    } else if (g_edit_state == EDIT_TIME) {
        sprintf(buf, "%02d.%02d.%02d",
                 g_edit_temp_time.hour,
                 g_edit_temp_time.minute,
                 g_edit_temp_time.second);
    } else if (g_edit_state == EDIT_ALARM) {
        sprintf(buf, "%02d.%02d.%02d",
                 g_edit_temp_alarm.hour,
                 g_edit_temp_alarm.minute,
                 g_edit_temp_alarm.second);
    } else {
        buf[0] = '\0';
    }
}

/*=========================================================================*/
/*  Module: buzzer.c                                                     */
/*=========================================================================*/


/*=========================================================================
 * Buzzer state
 *=========================================================================*/
static uint8_t  g_buzzer_on;
static uint8_t  g_buzzer_ringing;
static uint8_t  g_buzzer_rhythm_counter;
static uint16_t g_buzzer_ring_duration;

/* Frequency sweep: test tones 1k~4k Hz, 2s each */

void Buzzer_Init(void)
{
    g_buzzer_on             = 0;
    g_buzzer_ringing        = 0;
    g_buzzer_rhythm_counter = 0;
    g_buzzer_ring_duration  = 0;

    SysCtlPeripheralEnable(BUZZER_TIMER_PERIPH);
    while (!SysCtlPeripheralReady(BUZZER_TIMER_PERIPH));

    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOF));
    GPIOPinTypeGPIOOutput(BUZZER_PORT, BUZZER_PIN);
    GPIOPinWrite(BUZZER_PORT, BUZZER_PIN, 0);

    /* 8 kHz, 50% duty: Load=20M/8000=2500 */
    TimerConfigure(BUZZER_TIMER_BASE, TIMER_CFG_PERIODIC);
    TimerLoadSet(BUZZER_TIMER_BASE, BUZZER_TIMER, 2500 - 1);

    TimerIntEnable(BUZZER_TIMER_BASE, TIMER_TIMA_TIMEOUT);
    IntEnable(INT_TIMER0A);
}

void Buzzer_On(void)
{
    g_buzzer_on = 1;
    TimerEnable(BUZZER_TIMER_BASE, BUZZER_TIMER);
}

void Buzzer_Off(void)
{
    g_buzzer_on = 0;
    TimerDisable(BUZZER_TIMER_BASE, BUZZER_TIMER);
    GPIOPinWrite(BUZZER_PORT, BUZZER_PIN, 0);
}

void Buzzer_Toggle(void)
{
    if (g_buzzer_on) Buzzer_Off(); else Buzzer_On();
}

void Buzzer_RhythmHandler(void)
{
    extern uint8_t g_disp_night;
    if (!g_buzzer_ringing || g_disp_night == MODE_NIGHT) { Buzzer_Off(); return; }

    g_buzzer_rhythm_counter++;
    g_buzzer_ring_duration++;

    if (g_buzzer_ring_duration >= 850) {  /* 8.5s auto-stop */
        Alarm_Stop();
        Buzzer_StopRing();
        return;
    }

    /* ON/OFF rhythm: 200ms on, 200ms off */
    if ((g_buzzer_rhythm_counter % 40) < 20)
        Buzzer_On();
    else
        Buzzer_Off();
}

void Buzzer_StartRing(void)
{
    g_buzzer_ringing        = 1;
    g_buzzer_rhythm_counter = 0;
    g_buzzer_ring_duration  = 0;
    g_buzzer_on             = 0;
}

void Buzzer_StopRing(void)
{
    g_buzzer_ringing        = 0;
    g_buzzer_rhythm_counter = 0;
    g_buzzer_ring_duration  = 0;
    Buzzer_Off();
}

uint8_t Buzzer_IsRinging(void) { return g_buzzer_ringing; }

void TIMER0A_Handler(void)
{
    static uint8_t buzz_toggle = 0;
    TimerIntClear(BUZZER_TIMER_BASE, TIMER_TIMA_TIMEOUT);
    buzz_toggle = (uint8_t)(!buzz_toggle);
    GPIOPinWrite(BUZZER_PORT, BUZZER_PIN, buzz_toggle ? BUZZER_PIN : 0);
}

/*=========================================================================*/
/*  Module: led.c                                                        */
/*=========================================================================*/


/*=========================================================================
 * Global LED state byte.
 * Bits: D0=heartbeat, D1=alarm_en, D2=alarm_ring, D3=edit_active,
 *       D4=RX_active, D5=TX_active, D6=NTP_status, D7=daynight.
 * PCA9557 outputs are active-low: write ~g_led_state to turn LEDs on.
 *=========================================================================*/
uint8_t g_led_state;

/* Flash timeout counters (in 10ms ticks). 200ms = 20 ticks. */
static uint8_t g_rx_flash_timer;
static uint8_t g_tx_flash_timer;

/* Heartbeat phase */
static uint8_t g_heartbeat_phase;

/*=========================================================================
 * Initialize LEDs: all off, flash timers at 0.
 *=========================================================================*/
void LED_Init(void)
{
    g_led_state        = 0x00;
    g_rx_flash_timer   = 0;
    g_tx_flash_timer   = 0;
    g_heartbeat_phase  = 0;

    /* Write all LEDs off (PCA9557 active low: 0xFF = all off) */
    I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, 0xFF);
}

/*=========================================================================
 * Set or clear a specific LED bit.
 * Does NOT write to hardware (call LED_Write or Buzzer_WriteOutput).
 *=========================================================================*/
void LED_Set(uint8_t bit, uint8_t val)
{
    if (bit > 7) return;

    if (val) {
        g_led_state |= (uint8_t)(1 << bit);
    } else {
        g_led_state &= (uint8_t)(~(1 << bit));
    }
}

/*=========================================================================
 * Write the current LED state to PCA9557.
 * Converts to active-low for PCA9557 hardware.
 *=========================================================================*/
void LED_Write(void)
{
    uint8_t output_byte;
    uint8_t result;

    /* Active low: 0 = LED on, 1 = LED off */
    output_byte = (uint8_t)(~g_led_state);
    result = I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, output_byte);
    (void)result;
}

/*=========================================================================
 * Toggle the heartbeat LED (D0) each second.
 *=========================================================================*/
void LED_Heartbeat(void)
{
    g_heartbeat_phase = (uint8_t)(!g_heartbeat_phase);

    if (g_heartbeat_phase) {
        g_led_state |= (uint8_t)(1 << LED_HEARTBEAT);
    } else {
        g_led_state &= (uint8_t)(~(1 << LED_HEARTBEAT));
    }
}

/*=========================================================================
 * Flash the RX activity LED (D4). Stays on for 200ms then auto-off.
 *=========================================================================*/
void LED_RXFlash(void)
{
    { extern volatile uint8_t g_led_user_lock; if (g_led_user_lock > 0) return; }
    LED_Set(LED_RX_ACTIVE, 1);
    g_rx_flash_timer = 20;
}

/*=========================================================================
 * Flash the TX activity LED (D5). Stays on for 200ms then auto-off.
 *=========================================================================*/
void LED_TXFlash(void)
{
    { extern volatile uint8_t g_led_user_lock; if (g_led_user_lock > 0) return; }
    LED_Set(LED_TX_ACTIVE, 1);
    g_tx_flash_timer = 20;
}

/*=========================================================================
 * Update flash timeouts. Called every 10ms.
 * Turns off flash LEDs when their timers expire.
 *=========================================================================*/
void LED_UpdateFlashTimeout(void)
{
    extern volatile uint8_t g_led_user_lock;
    if (g_led_user_lock > 0) return;
    if (g_rx_flash_timer > 0) {
        g_rx_flash_timer--;
        if (g_rx_flash_timer == 0) {
            LED_Set(LED_RX_ACTIVE, 0);
        }
    }

    if (g_tx_flash_timer > 0) {
        g_tx_flash_timer--;
        if (g_tx_flash_timer == 0) {
            LED_Set(LED_TX_ACTIVE, 0);
        }
    }
}

/*=========================================================================*/
/*  Module: protocol.c                                                   */
/*=========================================================================*/


/*=========================================================================
 * Ring Buffer for incoming UART characters
 *=========================================================================*/
static volatile uint8_t g_ring_buf[PROTO_RING_SIZE];
static volatile uint16_t g_ring_head;    /* ISR writes here    */
static volatile uint16_t g_ring_tail;    /* main loop reads here */
static volatile uint8_t  g_ring_overflow;

/*=========================================================================
 * Line Buffer and Parser State
 *=========================================================================*/
static char    g_line_buf[PROTO_LINE_SIZE];
static uint8_t g_line_idx;

/*=========================================================================
 * Response buffer (used for building command responses)
 *=========================================================================*/
static char g_resp_buf[PROTO_LINE_SIZE * 2];

/*=========================================================================
 * Uptime reference for *PONG
 *=========================================================================*/
extern volatile uint32_t g_uptime_seconds;

/*=========================================================================
 * Initialize the protocol module.
 *=========================================================================*/
void Protocol_Init(void)
{
    uint16_t i;

    for (i = 0; i < PROTO_RING_SIZE; i++) {
        g_ring_buf[i] = 0;
    }
    g_ring_head     = 0;
    g_ring_tail     = 0;
    g_ring_overflow = 0;

    g_line_idx   = 0;
    g_line_buf[0] = '\0';
    g_resp_buf[0] = '\0';
}

/*=========================================================================
 * Receive one character from UART ISR. Put it into the ring buffer.
 *=========================================================================*/
void Protocol_CharReceived(uint8_t c)
{
    uint16_t next_head;
    uint8_t result;

    next_head = g_ring_head + 1;
    if (next_head >= PROTO_RING_SIZE) {
        next_head = 0;
    }

    /* Check for overflow */
    if (next_head == g_ring_tail) {
        g_ring_overflow = 1;
        return;
    }

    g_ring_buf[g_ring_head] = c;
    g_ring_head = next_head;

    /* Flash RX LED */
    LED_RXFlash();

    result = g_ring_overflow; /* suppress unused warning */
    (void)result;
}

/*=========================================================================
 * Check if a character matches a given pattern with abbreviation support.
 * Pattern format: uppercase=required, lowercase=optional.
 * e.g., "MINute" matches "MIN", "MINU", "MINUT", "MINUTE".
 *
 * Returns 1 if input matches the pattern.
 *=========================================================================*/
static uint8_t MatchAbbrev(const char *input, const char *pattern)
{
    uint8_t pat_len;
    uint8_t req_len;
    uint8_t in_len;
    uint8_t i;
    char ic, pc;

    in_len  = (uint8_t)strlen(input);
    pat_len = (uint8_t)strlen(pattern);

    /* Count required characters (uppercase in pattern) */
    req_len = 0;
    for (i = 0; i < pat_len; i++) {
        if (pattern[i] >= 'A' && pattern[i] <= 'Z') {
            req_len++;
        }
    }

    /* Input must be at least required length and at most pattern length */
    if (in_len < req_len || in_len > pat_len) {
        return 0;
    }

    /* Case-insensitive prefix match */
    for (i = 0; i < in_len; i++) {
        ic = input[i];
        pc = pattern[i];
        if (ic >= 'a' && ic <= 'z') ic = (char)(ic - 32);
        if (pc >= 'a' && pc <= 'z') pc = (char)(pc - 32);
        if (ic != pc) return 0;
    }

    return 1;
}

/*=========================================================================
 * Skip whitespace characters (space and tab).
 * Returns pointer to first non-whitespace char.
 *=========================================================================*/
static char* SkipSpaces(char *p)
{
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return p;
}

/*=========================================================================
 * Extract the next whitespace-delimited token from p.
 * Modifies *pp to point past the token.
 * Returns pointer to the token (null-terminated in the original buffer).
 * Returns NULL if no more tokens.
 *=========================================================================*/
static char* NextToken(char **pp)
{
    char *p;
    char *start;

    p = SkipSpaces(*pp);
    if (*p == '\0') {
        return NULL;
    }

    start = p;
    while (*p && *p != ' ' && *p != '\t') {
        p++;
    }

    if (*p) {
        *p = '\0';
        *pp = p + 1;
    } else {
        *pp = p;
    }

    return start;
}

/*=========================================================================
 * Send a response string over UART.  Flashes TX LED.
 *=========================================================================*/
static void Protocol_SendResponse(const char *resp)
{
    while (*resp) {
        UART0_SendChar(*resp++);
    }
    LED_TXFlash();
}

/*=========================================================================
 * Send an "OK <data>" response with FORMAT_RIGHT reversal applied to
 * the data portion.  Used for display-related GET commands (TIME,
 * DATE, DISP) so the PC receives data in the same order as the 7-seg.
 * GET:FORMAT and GET:ALARM use raw Protocol_SendResponse instead.
 *=========================================================================*/
static void Protocol_SendOKData(const char *data)
{
    char buf[64];
    uint8_t len;
    uint8_t i;
    char tmp;

    len = (uint8_t)strlen(data);
    if (len > 60) len = 60;

    buf[0] = 'O'; buf[1] = 'K'; buf[2] = ' ';
    for (i = 0; i < len; i++) buf[3 + i] = data[i];
    buf[3 + len] = '\r';
    buf[4 + len] = '\n';
    buf[5 + len] = '\0';

    /* Reverse data portion if FORMAT_RIGHT */
    if (g_disp_format == FORMAT_RIGHT && len > 0) {
        for (i = 0; i < len / 2; i++) {
            tmp = buf[3 + i];
            buf[3 + i] = buf[3 + len - 1 - i];
            buf[3 + len - 1 - i] = tmp;
        }
    }

    Protocol_SendResponse(buf);
}

/*=========================================================================
 * Parse a hex string (1-2 chars) to a byte value.
 * Returns 1 on success, 0 on failure.
 *=========================================================================*/
static uint8_t ParseHex2(const char *str, uint8_t *val)
{
    uint32_t parsed;
    char *endptr;

    /* Validate hex string */
    {
        uint8_t i;
        uint8_t slen;

        slen = (uint8_t)strlen(str);
        if (slen == 0 || slen > 2) return 0;

        for (i = 0; i < slen; i++) {
            if (!((str[i] >= '0' && str[i] <= '9') ||
                  (str[i] >= 'A' && str[i] <= 'F') ||
                  (str[i] >= 'a' && str[i] <= 'f'))) {
                return 0;
            }
        }
    }

    parsed = strtoul(str, &endptr, 16);
    if (*endptr != '\0') return 0;
    if (parsed > 255) return 0;

    *val = (uint8_t)parsed;
    return 1;
}

/*=========================================================================
 * Handle *RST command.
 * Returns: "OK\r\n"
 *=========================================================================*/
static void Cmd_RST(void)
{
    /* Reset to default state: 2024-01-01 00:00:00, TIME mode, LEFT, display ON */
    Clock_Init(24, 1, 1, 0, 0, 0);
    g_disp_mode   = DISP_MODE_TIME;
    g_disp_format = FORMAT_LEFT;
    g_disp_night  = MODE_DAY;
    g_disp_on     = 1;
    Display_UpdateFromClock(&g_clock);
    Protocol_SendResponse("OK\r\n");
}

/*=========================================================================
 * Handle *SET:DATE command.
 * Parameters: YEAR <yy> / MONTH <mm> / DATE <dd> (one or more fields).
 *=========================================================================*/
static void Cmd_SET_DATE(char *params)
{
    char *token;
    char *rest;
    uint8_t val;
    ClockTime temp_time;
    uint8_t has_update;

    /* Start with current clock values */
    temp_time  = g_clock;
    has_update = 0;

    rest = params;
    while ((token = NextToken(&rest)) != NULL) {
        /* Get the parameter name */
        /* Get the value token */
        char *val_str = NextToken(&rest);
        if (val_str == NULL) {
            Protocol_SendResponse("ERROR\r\n");
            return;
        }

        val = (uint8_t)atoi(val_str);

        if (MatchAbbrev(token, "YEAR")) {
            /* Valid range: 0-99 */
            if (val > 99) {
                Protocol_SendResponse("ERROR\r\n");
                return;
            }
            temp_time.year = val;
            has_update = 1;
        } else if (MatchAbbrev(token, "MONTH")) {
            if (val < 1 || val > 12) {
                Protocol_SendResponse("ERROR\r\n");
                return;
            }
            temp_time.month = val;
            has_update = 1;
        } else if (MatchAbbrev(token, "DATE")) {
            if (val < 1 || val > 31) {
                Protocol_SendResponse("ERROR\r\n");
                return;
            }
            temp_time.day = val;
            has_update = 1;
        } else {
            /* Unknown parameter name */
            Protocol_SendResponse("ERROR\r\n");
            return;
        }
    }

    if (has_update) {
        /* Validate day against month */
        if (temp_time.day > Clock_DaysInMonth(temp_time.year, temp_time.month)) {
            temp_time.day = Clock_DaysInMonth(temp_time.year, temp_time.month);
        }
        g_clock = temp_time;
        Protocol_SendResponse("OK\r\n");
    } else {
        Protocol_SendResponse("ERROR\r\n");
    }
}

/*=========================================================================
 * Handle *SET:TIME command.
 * Parameters: HOUR <hh> / MINute <mm> / SECond <ss>.
 *=========================================================================*/
static void Cmd_SET_TIME(char *params)
{
    char *token;
    char *rest;
    uint8_t val;
    uint8_t has_update;

    has_update = 0;

    rest = params;
    while ((token = NextToken(&rest)) != NULL) {
        char *val_str = NextToken(&rest);
        if (val_str == NULL) {
            Protocol_SendResponse("ERROR\r\n");
            return;
        }

        val = (uint8_t)atoi(val_str);

        if (MatchAbbrev(token, "HOUR")) {
            if (val > 23) {
                Protocol_SendResponse("ERROR\r\n");
                return;
            }
            g_clock.hour = val;
            has_update = 1;
        } else if (MatchAbbrev(token, "MINute")) {
            if (val > 59) {
                Protocol_SendResponse("ERROR\r\n");
                return;
            }
            g_clock.minute = val;
            has_update = 1;
        } else if (MatchAbbrev(token, "SECond")) {
            if (val > 59) {
                Protocol_SendResponse("ERROR\r\n");
                return;
            }
            g_clock.second = val;
            has_update = 1;
        } else {
            Protocol_SendResponse("ERROR\r\n");
            return;
        }
    }

    if (has_update) {
        Protocol_SendResponse("OK\r\n");
    } else {
        Protocol_SendResponse("ERROR\r\n");
    }
}

/*=========================================================================
 * Handle *SET:ALARM command.
 * Parameters: HOUR <hh> / MINute <mm> / SECond <ss> / OFF.
 *=========================================================================*/
static void Cmd_SET_ALARM(char *params)
{
    char *token;
    char *rest;
    uint8_t val;
    uint8_t has_update;

    has_update = 0;

    rest = params;
    while ((token = NextToken(&rest)) != NULL) {
        /* Check for OFF keyword */
        if (MatchAbbrev(token, "OFF")) {
            g_alarm.enabled = 0;
            g_alarm.ringing = 0;
            Buzzer_StopRing();
            Protocol_SendResponse("OK\r\n");
            return;
        }

        {
            char *val_str = NextToken(&rest);
            if (val_str == NULL) {
                Protocol_SendResponse("ERROR\r\n");
                return;
            }

            val = (uint8_t)atoi(val_str);

            if (MatchAbbrev(token, "HOUR")) {
                if (val > 23) {
                    Protocol_SendResponse("ERROR\r\n");
                    return;
                }
                g_alarm.hour = val;
                g_alarm.enabled = 1;
                has_update = 1;
            } else if (MatchAbbrev(token, "MINute")) {
                if (val > 59) {
                    Protocol_SendResponse("ERROR\r\n");
                    return;
                }
                g_alarm.minute = val;
                g_alarm.enabled = 1;
                has_update = 1;
            } else if (MatchAbbrev(token, "SECond")) {
                if (val > 59) {
                    Protocol_SendResponse("ERROR\r\n");
                    return;
                }
                g_alarm.second = val;
                g_alarm.enabled = 1;
                has_update = 1;
            } else {
                Protocol_SendResponse("ERROR\r\n");
                return;
            }
        }
    }

    if (has_update) {
        g_alarm.ringing = 0;
        Protocol_SendResponse("OK\r\n");
    } else {
        Protocol_SendResponse("ERROR\r\n");
    }
}

/*=========================================================================
 * Handle *SET:DISPlay command.
 * Parameters: ON / OFF.
 *=========================================================================*/
static void Cmd_SET_DISPLAY(char *params)
{
    char *token;

    token = NextToken(&params);
    if (token == NULL) {
        Protocol_SendResponse("ERROR\r\n");
        return;
    }

    if (MatchAbbrev(token, "ON")) {
        g_disp_on = 1;
        Protocol_SendResponse("OK\r\n");
    } else if (MatchAbbrev(token, "OFF")) {
        g_disp_on = 0;
        Events_ReportDisp("        ", 0);  /* tell PC twin to go dark */
        Protocol_SendResponse("OK\r\n");
    } else if (MatchAbbrev(token, "TIME")) {
        g_disp_mode = DISP_MODE_TIME;
        Display_UpdateFromClock(&g_clock);
        Protocol_SendResponse("OK\r\n");
    } else if (MatchAbbrev(token, "DATE")) {
        g_disp_mode = DISP_MODE_DATE;
        Display_UpdateFromClock(&g_clock);
        Protocol_SendResponse("OK\r\n");
    } else if (MatchAbbrev(token, "YEAR")) {
        g_disp_mode = DISP_MODE_YEAR;
        Display_UpdateFromClock(&g_clock);
        Protocol_SendResponse("OK\r\n");
    } else {
        Protocol_SendResponse("ERROR\r\n");
    }
}

/*=========================================================================
 * Handle *SET:FORMAT command.
 * Parameters: LEFT / RIGHT.
 *=========================================================================*/
static void Cmd_SET_FORMAT(char *params)
{
    char *token;

    token = NextToken(&params);
    if (token == NULL) {
        Protocol_SendResponse("ERROR\r\n");
        return;
    }

    if (MatchAbbrev(token, "LEFT")) {
        Display_SetFormat(FORMAT_LEFT);
        Protocol_SendResponse("OK\r\n");
    } else if (MatchAbbrev(token, "RIGHT")) {
        Display_SetFormat(FORMAT_RIGHT);
        Protocol_SendResponse("OK\r\n");
    } else {
        Protocol_SendResponse("ERROR\r\n");
    }
}

/*=========================================================================
 * Handle *SET:MSG command.
 * Parameters: <text> (up to 32 bytes).
 *=========================================================================*/
static void Cmd_SET_MSG(char *params)
{
    char *text;

    /* The rest of the line (after trimming leading space) is the message */
    text = SkipSpaces(params);
    if (text == NULL || *text == '\0') {
        Protocol_SendResponse("ERROR\r\n");
        return;
    }

    if (strlen(text) > 32) {
        Protocol_SendResponse("ERROR\r\n");
        return;
    }

    Display_SetBuffer(text);
    /* Weather auto-revert is handled by PC sending *SET:MODE TIME
     * after 5s.  Manual *SET:MSG stays until user changes display. */
    Protocol_SendResponse("OK\r\n");
}

/*=========================================================================
 * Handle *SET:BEEP command.
 * Parameters: <ms> (10-5000).
 *=========================================================================*/
static void Cmd_SET_BEEP(char *params)
{
    char *token;
    uint32_t ms_val;

    token = NextToken(&params);
    if (token == NULL) {
        Protocol_SendResponse("ERROR\r\n");
        return;
    }

    ms_val = (uint32_t)atoi(token);
    if (ms_val < 10 || ms_val > 5000) {
        Protocol_SendResponse("ERROR\r\n");
        return;
    }

    /* Trigger beep: start buzzer now; main loop handles duration */
    Buzzer_On();
    /* Set beep timeout counter (in 10ms ticks) */
    g_beep_timeout = (uint16_t)(ms_val / 10);
    Protocol_SendResponse("OK\r\n");
}

/*=========================================================================
 * Handle *SET:LED command.
 * Parameters: <hex2> (00-FF).
 *=========================================================================*/
static void Cmd_SET_LED(char *params)
{
    char *token;
    uint8_t val;

    token = NextToken(&params);
    if (token == NULL) {
        Protocol_SendResponse("ERROR\r\n");
        return;
    }

    if (!ParseHex2(token, &val)) {
        Protocol_SendResponse("ERROR\r\n");
        return;
    }

    g_led_state = val;
    g_led_user_lock = 200;  /* user override for 2s (200*10ms) */
    Protocol_SendResponse("OK\r\n");
}

/*=========================================================================
 * Handle *SET:KEY command.
 * Parameters: <NAME> (FUNC/SHIFT/ADD/SAVE/DISP/SPEED/FORMAT/EXT/USER1/USER2).
 *=========================================================================*/
static void Cmd_SET_KEY(char *params)
{
    char *token;
    uint8_t i;

    token = NextToken(&params);
    if (token == NULL) {
        Protocol_SendResponse("ERROR\r\n");
        return;
    }

    /* Match against known key names */
    for (i = 0; i < NUM_KEYS; i++) {
        const char *name;
        name = Keys_GetKeyName(i);
        if (MatchAbbrev(token, name)) {
            /* Simulate key press: short press for all keys */
            Keys_Simulate(i);
            Protocol_SendResponse("OK\r\n");
            return;
        }
    }

    Protocol_SendResponse("ERROR\r\n");
}

/*=========================================================================
 * Handle *SET:MODE command.
 * Parameters: DAY / NIGHT.
 *=========================================================================*/
static void Cmd_SET_MODE(char *params)
{
    char *token;

    token = NextToken(&params);
    if (token == NULL) {
        Protocol_SendResponse("ERROR\r\n");
        return;
    }

    if (MatchAbbrev(token, "DAY")) {
        Display_SetNight(MODE_DAY);
        LED_Set(LED_DAYNIGHT, 1); /* DAY = D7 on */
        Events_ReportMode(MODE_DAY);
        Protocol_SendResponse("OK\r\n");
    } else if (MatchAbbrev(token, "NIGHT")) {
        Display_SetNight(MODE_NIGHT);
        LED_Set(LED_DAYNIGHT, 0); /* NIGHT = D7 off */
        Events_ReportMode(MODE_NIGHT);
        Protocol_SendResponse("OK\r\n");
    } else {
        Protocol_SendResponse("ERROR\r\n");
    }
}

/*=========================================================================
 * Handle *GET:DATE command.
 * Returns: "OK YY.MM.DD\r\n"
 *=========================================================================*/
static void Cmd_GET_DATE(void)
{
    sprintf(g_resp_buf, "%02d.%02d.%02d",
            g_clock.year % 100, g_clock.month, g_clock.day);
    Protocol_SendOKData(g_resp_buf);
}

/*=========================================================================
 * Handle *GET:TIME command.
 * Returns: "OK HH.MM.SS\r\n"
 *=========================================================================*/
static void Cmd_GET_TIME(void)
{
    sprintf(g_resp_buf, "%02d.%02d.%02d",
            g_clock.hour, g_clock.minute, g_clock.second);
    Protocol_SendOKData(g_resp_buf);
}

/*=========================================================================
 * Handle *GET:ALARM command.
 * Returns: "OK HH.MM.SS DAY\r\n" or "OK OFF\r\n"
 *=========================================================================*/
static void Cmd_GET_ALARM(void)
{
    if (g_alarm.enabled) {
        sprintf(g_resp_buf, "OK %02d.%02d.%02d %d\r\n",
                g_alarm.hour, g_alarm.minute, g_alarm.second, g_alarm.day);
    } else {
        sprintf(g_resp_buf, "OK OFF\r\n");
    }
    Protocol_SendResponse(g_resp_buf);
}

/*=========================================================================
 * Handle *GET:DISP command.
 * Returns: "OK <display_status>\r\n"
 *=========================================================================*/
static void Cmd_GET_DISP(void)
{
    if (g_disp_on) {
        /* Return the current display buffer content */
        sprintf(g_resp_buf, "OK %s\r\n", g_disp_buffer);
    } else {
        sprintf(g_resp_buf, "OK OFF\r\n");
    }
    Protocol_SendResponse(g_resp_buf);
}

/*=========================================================================
 * Handle *GET:FORMAT command.
 * Returns: "OK LEFT\r\n" or "OK RIGHT\r\n"
 *=========================================================================*/
static void Cmd_GET_FORMAT(void)
{
    if (g_disp_format == FORMAT_RIGHT) {
        Protocol_SendResponse("OK RIGHT\r\n");
    } else {
        Protocol_SendResponse("OK LEFT\r\n");
    }
}

/*=========================================================================
 * Handle *PING command.
 * Returns: "*PONG <uptime_seconds>\r\n"
 *=========================================================================*/
static void Cmd_PING(void)
{
    sprintf(g_resp_buf, "*PONG %lu\r\n", (unsigned long)g_uptime_seconds);
    /* PONG is NOT reversed by FORMAT RIGHT (it doesn't start with "OK ") */
    Protocol_SendResponse(g_resp_buf);
}

/*=========================================================================
 * Parse a complete command line and execute it.
 *=========================================================================*/
static void Protocol_ParseLine(char *line)
{
    char *p;
    char *cmd_str;
    char *subcmd_str;
    char *params;

    /* Trim trailing whitespace (including \r, \n) */
    {
        int32_t end_pos;
        end_pos = (int32_t)strlen(line) - 1;
        while (end_pos >= 0) {
            if (line[end_pos] == '\r' || line[end_pos] == '\n' ||
                line[end_pos] == ' '  || line[end_pos] == '\t') {
                line[end_pos] = '\0';
                end_pos--;
            } else {
                break;
            }
        }
    }

    p = line;

    /* Must start with '*' */
    if (*p != '*') {
        Protocol_SendResponse("ERROR\r\n");
        return;
    }
    p++;

    /* Extract command token */
    cmd_str = NextToken(&p);
    if (cmd_str == NULL) {
        Protocol_SendResponse("ERROR\r\n");
        return;
    }

    /* Check command: RST */
    if (MatchAbbrev(cmd_str, "RST")) {
        Cmd_RST();
        return;
    }

    /* Check command: PING */
    if (MatchAbbrev(cmd_str, "PING")) {
        Cmd_PING();
        return;
    }

    /* For SET and GET, need a subcommand after ':'.
     * The ':' may be attached to the command token (e.g., "SET:DATE")
     * or be a separate token (e.g., "SET" then ":DATE").
     * Split the colon FIRST, then check the cleaned command against SET/GET. */
    {
        char   *colon_in_cmd;
        uint8_t is_set;
        uint8_t is_get;

        /* Check if ':' is part of the command token */
        colon_in_cmd = strchr(cmd_str, ':');
        if (colon_in_cmd != NULL) {
            /* Colon attached: "SET:KEY" → cmd="SET", subcmd="KEY" */
            *colon_in_cmd = '\0';
            subcmd_str = colon_in_cmd + 1;
            params     = p;
        } else {
            /* Colon separate: next token is ":SUBCMD" */
            subcmd_str = NextToken(&p);
            if (subcmd_str == NULL || subcmd_str[0] != ':') {
                Protocol_SendResponse("ERROR\r\n");
                return;
            }
            subcmd_str = subcmd_str + 1;  /* skip ':' */
            params     = p;
        }

        /* Now cmd_str is clean ("SET" or "GET"), test it */
        is_set = (uint8_t)MatchAbbrev(cmd_str, "SET");
        is_get = (uint8_t)MatchAbbrev(cmd_str, "GET");

        if (is_set || is_get) {

            /* Dispatch subcommand */
            if (MatchAbbrev(subcmd_str, "DATE")) {
                if (is_set) Cmd_SET_DATE(params);
                else        Cmd_GET_DATE();
            } else if (MatchAbbrev(subcmd_str, "TIME")) {
                if (is_set) Cmd_SET_TIME(params);
                else        Cmd_GET_TIME();
            } else if (MatchAbbrev(subcmd_str, "ALARM")) {
                if (is_set) Cmd_SET_ALARM(params);
                else        Cmd_GET_ALARM();
            } else if (MatchAbbrev(subcmd_str, "DISPlay")) {
                if (is_set) Cmd_SET_DISPLAY(params);
                else        Cmd_GET_DISP();
            } else if (MatchAbbrev(subcmd_str, "FORMAT")) {
                if (is_set) Cmd_SET_FORMAT(params);
                else        Cmd_GET_FORMAT();
            } else if (MatchAbbrev(subcmd_str, "MSG")) {
                if (is_set) Cmd_SET_MSG(params);
                else        Protocol_SendResponse("ERROR\r\n");
            } else if (MatchAbbrev(subcmd_str, "BEEP")) {
                if (is_set) Cmd_SET_BEEP(params);
                else        Protocol_SendResponse("ERROR\r\n");
            } else if (MatchAbbrev(subcmd_str, "LED")) {
                if (is_set) Cmd_SET_LED(params);
                else        Protocol_SendResponse("ERROR\r\n");
            } else if (MatchAbbrev(subcmd_str, "KEY")) {
                if (is_set) Cmd_SET_KEY(params);
                else        Protocol_SendResponse("ERROR\r\n");
            } else if (MatchAbbrev(subcmd_str, "MODE")) {
                if (is_set) Cmd_SET_MODE(params);
                else        Protocol_SendResponse("ERROR\r\n");
            } else {
                Protocol_SendResponse("ERROR\r\n");
            }
            return;
        }
    }

    /* Unknown command */
    Protocol_SendResponse("ERROR\r\n");
}

/*=========================================================================
 * Process characters from the ring buffer.
 * Builds complete lines and parses them.
 * Called from main loop.
 *=========================================================================*/
void Protocol_Process(void)
{
    uint8_t c;

    /* Check for ring buffer overflow */
    if (g_ring_overflow) {
        Protocol_SendResponse("ERROR\r\n");
        g_ring_overflow = 0;
        g_ring_head = 0;
        g_ring_tail = 0;
        g_line_idx = 0;
        return;
    }

    /* Process characters from ring buffer */
    while (g_ring_tail != g_ring_head) {
        c = g_ring_buf[g_ring_tail];

        g_ring_tail++;
        if (g_ring_tail >= PROTO_RING_SIZE) {
            g_ring_tail = 0;
        }

        /* Check for line termination: \r, \n, or \r\n */
        if (c == '\r' || c == '\n') {
            /* Handle \r\n sequence: if we see \r, check next char for \n */
            if (c == '\r' && g_ring_tail != g_ring_head) {
                /* Peek at next char */
                if (g_ring_buf[g_ring_tail] == '\n') {
                    /* Consume the \n */
                    g_ring_tail++;
                    if (g_ring_tail >= PROTO_RING_SIZE) {
                        g_ring_tail = 0;
                    }
                }
            }

            if (g_line_idx > 0) {
                g_line_buf[g_line_idx] = '\0';
                g_line_idx = 0;

                /* Parse the line immediately */
                Protocol_ParseLine(g_line_buf);
            }
        } else {
            /* Regular character: add to line buffer */
            if (g_line_idx < PROTO_LINE_SIZE - 1) {
                g_line_buf[g_line_idx] = (char)c;
                g_line_idx++;
            } else {
                /* Line too long: discard and reset */
                g_line_idx = 0;
                Protocol_SendResponse("ERROR\r\n");
            }
        }
    }
}

/*=========================================================================*/
/*  Module: events.c                                                     */
/*=========================================================================*/


/*=========================================================================
 * Event queue entry
 *=========================================================================*/
typedef struct {
    char    msg[EVENT_MSG_MAX];
    uint8_t len;
    uint8_t valid;
} EventEntry;

/*=========================================================================
 * Event queue
 *=========================================================================*/
static EventEntry g_event_queue[EVENT_QUEUE_SIZE];
static uint8_t    g_event_head;       /* write position */
static uint8_t    g_event_tail;       /* read position  */
static uint8_t    g_event_count;

/* Rate limiting: max 1 event per 50ms */
static uint16_t   g_event_rate_timer;  /* 10ms ticks since last send */

/* External references for key names */
const char* Keys_GetKeyName(uint8_t key_id);

/*=========================================================================
 * Initialize the event queue.
 *=========================================================================*/
void Events_Init(void)
{
    uint8_t i;

    for (i = 0; i < EVENT_QUEUE_SIZE; i++) {
        g_event_queue[i].msg[0] = '\0';
        g_event_queue[i].len    = 0;
        g_event_queue[i].valid  = 0;
    }
    g_event_head        = 0;
    g_event_tail        = 0;
    g_event_count       = 0;
    g_event_rate_timer  = 0;
}

/*=========================================================================
 * Enqueue an event message. Returns 0 if queue is full.
 *=========================================================================*/
static uint8_t Events_Enqueue(const char *msg)
{
    uint8_t len;

    if (g_event_count >= EVENT_QUEUE_SIZE) {
        return 0; /* queue full */
    }

    len = (uint8_t)strlen(msg);
    if (len >= EVENT_MSG_MAX) {
        len = EVENT_MSG_MAX - 1;
    }

    {
        uint8_t i;
        for (i = 0; i < len; i++) {
            g_event_queue[g_event_head].msg[i] = msg[i];
        }
        g_event_queue[g_event_head].msg[len] = '\0';
    }
    g_event_queue[g_event_head].len   = len;
    g_event_queue[g_event_head].valid = 1;

    g_event_head++;
    if (g_event_head >= EVENT_QUEUE_SIZE) {
        g_event_head = 0;
    }
    g_event_count++;

    return 1;
}

/*=========================================================================
 * Report a key press event: *EVT:KEY <NAME>\r\n
 *=========================================================================*/
void Events_ReportKey(uint8_t key_id)
{
    const char *name;
    char msg[EVENT_MSG_MAX];

    name = Keys_GetKeyName(key_id);
    sprintf(msg, "*EVT:KEY %s\r\n", name);
    Events_Enqueue(msg);
}

/*=========================================================================
 * Report alarm event: *EVT:ALARM\r\n or *EVT:ALARM OFF\r\n
 *=========================================================================*/
void Events_ReportAlarm(uint8_t on)
{
    if (on) {
        Events_Enqueue("*EVT:ALARM\r\n");
    } else {
        Events_Enqueue("*EVT:ALARM OFF\r\n");
    }
}

/*=========================================================================
 * Report edit event: *EVT:EDIT <TYPE> <VALUE>\r\n
 * type: 1=DATE, 2=TIME, 3=ALARM
 *=========================================================================*/
void Events_ReportEdit(uint8_t type, const char *value)
{
    char msg[EVENT_MSG_MAX];
    const char *type_str;

    type_str = "UNKNOWN";
    if (type == EDIT_DATE)  type_str = "DATE";
    if (type == EDIT_TIME)  type_str = "TIME";
    if (type == EDIT_ALARM) type_str = "ALARM";

    sprintf(msg, "*EVT:EDIT %s %s\r\n", type_str, value);
    Events_Enqueue(msg);
}

/*=========================================================================
 * Report display event: *EVT:DISP <8char> <dpHex>\r\n
 *=========================================================================*/
void Events_ReportDisp(const char *seg_str, uint8_t dp_hex)
{
    char msg[EVENT_MSG_MAX];
    char disp9[9];
    uint8_t i;

    /* Ensure exactly 8 chars for the display string */
    for (i = 0; i < 8; i++) {
        if (seg_str && seg_str[i]) {
            disp9[i] = seg_str[i];
        } else {
            disp9[i] = ' ';
        }
    }
    disp9[8] = '\0';

    sprintf(msg, "*EVT:DISP %s %02X\r\n", disp9, dp_hex);
    Events_Enqueue(msg);
}

/*=========================================================================
 * Report LED event: *EVT:LED <hex2>\r\n
 *=========================================================================*/
void Events_ReportLED(uint8_t led_hex)
{
    char msg[EVENT_MSG_MAX];

    sprintf(msg, "*EVT:LED %02X\r\n", led_hex);
    Events_Enqueue(msg);
}

/*=========================================================================
 * Report mode event: *EVT:MODE <STATE>\r\n
 * mode: 0=DAY, 1=NIGHT
 *=========================================================================*/
void Events_ReportMode(uint8_t mode)
{
    if (mode == MODE_NIGHT) {
        Events_Enqueue("*EVT:MODE NIGHT\r\n");
    } else {
        Events_Enqueue("*EVT:MODE DAY\r\n");
    }
}

/*=========================================================================
 * 1Hz heartbeat handler: sends DISP and LED events every second.
 * Called from the 1000ms handler in main.
 *=========================================================================*/
void Events_1HzHandler(ClockTime *now, uint8_t led_state,
                       const char *disp_str, uint8_t dp)
{
    char time_str[16];

    /* Send DISP heartbeat */
    Events_ReportDisp(disp_str, dp);

    /* Send LED heartbeat */
    Events_ReportLED(led_state);

    /* Suppress unused parameter warning */
    sprintf(time_str, "%02d:%02d:%02d",
            now->hour, now->minute, now->second);
    (void)time_str;
}

/*=========================================================================
 * Check if there are pending events to send.
 *=========================================================================*/
uint8_t Events_HasPending(void)
{
    return (g_event_count > 0) ? 1 : 0;
}

/*=========================================================================
 * Send the next pending event over UART.
 * Rate-limited to max 1 event per 50ms to avoid flooding.
 * Called from the 100ms handler in main.
 *=========================================================================*/
void Events_SendNext(void)
{
    uint8_t i;
    char *pmsg;

    /* Rate limiting: 20ms between sends (50 events/sec max).
     * At 115200 baud this uses <10% bandwidth, enough for flow. */
    g_event_rate_timer++;
    if (g_event_rate_timer < 2) {
        return;
    }
    g_event_rate_timer = 0;

    if (g_event_count == 0) {
        return;
    }

    /* Get the next event from the queue */
    if (!g_event_queue[g_event_tail].valid) {
        /* Queue corrupted; reset */
        Events_Init();
        return;
    }

    pmsg = g_event_queue[g_event_tail].msg;

    /* Send the event over UART */
    for (i = 0; i < g_event_queue[g_event_tail].len; i++) {
        UART0_SendChar(pmsg[i]);
    }

    /* Apply FORMAT RIGHT reversal if needed */
    /* NOTE: FORMAT RIGHT reversal for responses is handled in protocol.c.
     * Event messages are NOT reversed — only command responses are. */

    /* Mark as sent and advance tail */
    g_event_queue[g_event_tail].valid = 0;
    g_event_tail++;
    if (g_event_tail >= EVENT_QUEUE_SIZE) {
        g_event_tail = 0;
    }
    g_event_count--;
}