#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "hw_memmap.h"
#include "debug.h"
#include "gpio.h"
#include "hw_i2c.h"
#include "hw_types.h"
#include "i2c.h"
#include "pin_map.h"
#include "sysctl.h"
#include "systick.h"
#include "interrupt.h"
#include "uart.h"
#include "hw_ints.h"

#include "hw_config.h"
#include "clock.h"
#include "alarm.h"
#include "display.h"
#include "keys.h"
#include "buzzer.h"
#include "led.h"
#include "protocol.h"
#include "events.h"

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
volatile uint8_t  g_msg_timeout;   /* weather msg auto-revert timer (seconds) */

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
#define BOOT_VERSION        " 1.0.0  "     /* Software version (7seg-safe) */

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

                /* Edit state LED indicator */
                edit_state = Keys_GetEditState();
                if (edit_state != EDIT_NONE) {
                    LED_Set(LED_EDIT_ACTIVE, 1);
                } else {
                    LED_Set(LED_EDIT_ACTIVE, 0);
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

                /* Write combined LED+buzzer output to PCA9557 at 10ms rate
                 * so buzzer rhythm and LED flashes are responsive. */
                Buzzer_WriteOutput();

                /* LED flash timeout */
                LED_UpdateFlashTimeout();

                /* Event queue sending (rate-limited to 1 per 50ms internally) */
                if (Events_HasPending()) {
                    Events_SendNext();
                }

                /* Update flow counter for scroll */
                if (g_disp_mode == DISP_MODE_FULL) {
                    g_flow_counter++;
                    if (g_flow_counter >= g_flow_delay) {
                        g_flow_counter = 0;
                        Display_FlowAdvance();
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

                /* Update alarm LED indicators */
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

                /* Heartbeat LED toggle */
                LED_Heartbeat();

                /* Update display from clock (if not in edit mode).
                 * Post-boot mode/format enforcement is handled by the
                 * 10ms handler's boot_guard (300 ticks = 3 seconds). */
                if (boot_done && Keys_GetEditState() == EDIT_NONE) {
                    Display_UpdateFromClock(&clock_now);
                }
                /* Else: edit display already updated in 10ms handler */

                /* Weather/msg auto-revert: after 5s in FULL mode,
                 * switch back to clock display. */
                if (g_msg_timeout > 0) {
                    g_msg_timeout--;
                    if (g_msg_timeout == 0) {
                        g_disp_mode = DISP_MODE_TIME;
                        Display_UpdateFromClock(&g_clock);
                    }
                }

                /* Event heartbeats (DISP + LED every 1s) */
                {
                    char disp_str[9];
                    uint8_t j;
                    for (j = 0; j < DISP_DIGITS; j++) {
                        disp_str[j] = g_disp_chars[j];
                    }
                    disp_str[8] = '\0';
                    Events_1HzHandler(&clock_now, g_led_state,
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
