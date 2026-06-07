#include "buzzer.h"
#include "alarm.h"
#include "hw_config.h"
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
static uint8_t  g_buzzer_ring_duration;

/* Timer base for buzzer tone generation on PF3 */
#define BUZZER_TIMER_BASE   TIMER0_BASE
#define BUZZER_TIMER        TIMER_A
#define BUZZER_TIMER_PERIPH SYSCTL_PERIPH_TIMER0

/*=========================================================================
 * Initialize buzzer: PF3 GPIO output driven by Timer0A at 4 kHz.
 * SysClk=20MHz → timer clk=20MHz → period=20M/4000=5000 → match=2500
 *=========================================================================*/
void Buzzer_Init(void)
{
    g_buzzer_on             = 0;
    g_buzzer_ringing        = 0;
    g_buzzer_rhythm_counter = 0;
    g_buzzer_ring_duration  = 0;

    /* Enable Timer0 */
    SysCtlPeripheralEnable(BUZZER_TIMER_PERIPH);
    while (!SysCtlPeripheralReady(BUZZER_TIMER_PERIPH));

    /* Configure PF3 as GPIO output, start LOW */
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOF));
    GPIOPinTypeGPIOOutput(BUZZER_PORT, BUZZER_PIN);
    GPIOPinWrite(BUZZER_PORT, BUZZER_PIN, 0);

    /* Timer0A periodic: toggles PF3 in ISR @ 4 kHz (common piezo resonance).
     * SysClk = 20 MHz = timer clock (no prescale).
     * 4 kHz → 250 µs period → Load = 20M/4000 = 5000.
     * 50 % duty → Match = 2500. */
    TimerConfigure(BUZZER_TIMER_BASE, TIMER_CFG_PERIODIC);
    TimerLoadSet(BUZZER_TIMER_BASE, BUZZER_TIMER, 5000 - 1);
    TimerMatchSet(BUZZER_TIMER_BASE, BUZZER_TIMER, 2500 - 1);

    TimerIntEnable(BUZZER_TIMER_BASE, TIMER_TIMA_TIMEOUT);
    IntEnable(INT_TIMER0A);
    /* Leave timer disabled until Buzzer_On() is called */
}

/*=========================================================================
 * Turn buzzer on: start timer toggling PF3.
 *=========================================================================*/
void Buzzer_On(void)
{
    g_buzzer_on = 1;
    TimerEnable(BUZZER_TIMER_BASE, BUZZER_TIMER);
}

/*=========================================================================
 * Turn buzzer off: stop timer, pull PF3 low.
 *=========================================================================*/
void Buzzer_Off(void)
{
    g_buzzer_on = 0;
    TimerDisable(BUZZER_TIMER_BASE, BUZZER_TIMER);
    GPIOPinWrite(BUZZER_PORT, BUZZER_PIN, 0);
}

/*=========================================================================
 * Toggle buzzer state.
 *=========================================================================*/
void Buzzer_Toggle(void)
{
    if (g_buzzer_on) {
        Buzzer_Off();
    } else {
        Buzzer_On();
    }
}

/*=========================================================================
 * No-op: buzzer is independent of PCA9557.
 *=========================================================================*/
void Buzzer_WriteOutput(void)
{
    (void)0;
}

/*=========================================================================
 * Rhythm handler: called every 10ms from main loop.
 * ON 200ms, OFF 200ms, auto-stop after 10 seconds.
 *=========================================================================*/
void Buzzer_RhythmHandler(void)
{
    if (!g_buzzer_ringing) {
        Buzzer_Off();
        return;
    }

    g_buzzer_rhythm_counter++;
    g_buzzer_ring_duration++;

    if (g_buzzer_ring_duration >= 1000) {
        Alarm_Stop();
        Buzzer_StopRing();
        return;
    }

    if ((g_buzzer_rhythm_counter % 40) < 20) {
        Buzzer_On();
    } else {
        Buzzer_Off();
    }
}

/*=========================================================================
 * Start the alarm rhythm.
 *=========================================================================*/
void Buzzer_StartRing(void)
{
    g_buzzer_ringing        = 1;
    g_buzzer_rhythm_counter = 0;
    g_buzzer_ring_duration  = 0;
    g_buzzer_on             = 0;
}

/*=========================================================================
 * Stop the alarm rhythm.
 *=========================================================================*/
void Buzzer_StopRing(void)
{
    g_buzzer_ringing        = 0;
    g_buzzer_rhythm_counter = 0;
    g_buzzer_ring_duration  = 0;
    Buzzer_Off();
}

/*=========================================================================
 * Check if rhythm is active.
 *=========================================================================*/
uint8_t Buzzer_IsRinging(void)
{
    return g_buzzer_ringing;
}

/*=========================================================================
 * Timer0A ISR: toggle PF3 to create square wave.
 *=========================================================================*/
void TIMER0A_Handler(void)
{
    TimerIntClear(BUZZER_TIMER_BASE, TIMER_TIMA_TIMEOUT);
    /* Toggle PF3 */
    if (GPIOPinRead(BUZZER_PORT, BUZZER_PIN)) {
        GPIOPinWrite(BUZZER_PORT, BUZZER_PIN, 0);
    } else {
        GPIOPinWrite(BUZZER_PORT, BUZZER_PIN, BUZZER_PIN);
    }
}
