#include "buzzer.h"
#include "alarm.h"
#include "hw_config.h"
#include "hw_types.h"
#include "hw_gpio.h"
#include "gpio.h"
#include "sysctl.h"
#include "timer.h"
#include "interrupt.h"
#include "hw_ints.h"

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
