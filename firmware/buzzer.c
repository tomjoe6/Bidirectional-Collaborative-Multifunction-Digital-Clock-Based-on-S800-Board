#include "buzzer.h"
#include "hw_config.h"

/*=========================================================================
 * Buzzer state
 * g_buzzer_on: 1 = buzzer output active
 * g_buzzer_ringing: 1 = alarm rhythm active
 * g_buzzer_rhythm_counter: counts 100ms ticks for rhythm pattern
 * g_buzzer_ring_duration: total ringing time in 100ms ticks (max 10s = 100)
 *=========================================================================*/
static uint8_t  g_buzzer_on;
static uint8_t  g_buzzer_ringing;
static uint8_t  g_buzzer_rhythm_counter;
static uint8_t  g_buzzer_ring_duration;

/* Forward declaration of LED_Write for combined PCA9557 output */
/* LED_Write is in led.c; we need to coordinate PCA9557 writes.
 * We use I2C0_WriteByte directly here since buzzer bit 7 is shared
 * with LED D7. The main loop coordinates the write order. */
extern uint8_t g_led_state;

/*=========================================================================
 * Initialize buzzer to off state.
 *=========================================================================*/
void Buzzer_Init(void)
{
    g_buzzer_on             = 0;
    g_buzzer_ringing        = 0;
    g_buzzer_rhythm_counter = 0;
    g_buzzer_ring_duration  = 0;
}

/*=========================================================================
 * Turn buzzer on (PCA9557 P07 = 0, active low).
 *=========================================================================*/
void Buzzer_On(void)
{
    g_buzzer_on = 1;
}

/*=========================================================================
 * Turn buzzer off (PCA9557 P07 = 1).
 *=========================================================================*/
void Buzzer_Off(void)
{
    g_buzzer_on = 0;
}

/*=========================================================================
 * Toggle buzzer state.
 *=========================================================================*/
void Buzzer_Toggle(void)
{
    g_buzzer_on = (uint8_t)(!g_buzzer_on);
}

/*=========================================================================
 * Write the actual PCA9557 output, combining LED state and buzzer state.
 * Bit 7 = buzzer (active low: 0=on, 1=off)
 * All bits: 0=turns on LED/buzzer, 1=turns off.
 *=========================================================================*/
void Buzzer_WriteOutput(void)
{
    uint8_t output_byte;
    uint8_t result;

    /* Start with LED state inverted (active low) */
    output_byte = (uint8_t)(~g_led_state);

    /* Overlay buzzer on bit 7: 0=buzzer ON, 1=buzzer OFF */
    if (g_buzzer_on) {
        output_byte &= (uint8_t)(~(1 << BUZZER_BIT));  /* clear bit 7 → buzzer on */
    } else {
        output_byte |= (uint8_t)(1 << BUZZER_BIT);     /* set bit 7 → buzzer off */
    }

    result = I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, output_byte);
    (void)result;
}

/*=========================================================================
 * Rhythm handler: called every 100ms.
 * Creates an ON-OFF-ON-OFF... pattern for the alarm buzzer.
 * Pattern: ON for 200ms (2 ticks), OFF for 200ms (2 ticks).
 * Auto-stops after 10 seconds (100 ticks).
 *=========================================================================*/
void Buzzer_RhythmHandler(void)
{
    /* If not ringing, ensure buzzer is off */
    if (!g_buzzer_ringing) {
        g_buzzer_on = 0;
        return;
    }

    g_buzzer_rhythm_counter++;
    g_buzzer_ring_duration++;

    /* Auto-stop after 10 seconds (100 * 100ms) */
    if (g_buzzer_ring_duration >= 100) {
        Buzzer_StopRing();
        return;
    }

    /* Rhythm pattern: 100ms period → toggle every cycle for 200ms ON/OFF */
    /* Counter mod 4: 0=ON, 1=ON, 2=OFF, 3=OFF → toggle every 2 cycles */
    if ((g_buzzer_rhythm_counter % 4) < 2) {
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
    g_buzzer_on             = 0;
}

/*=========================================================================
 * Check if rhythm is active.
 *=========================================================================*/
uint8_t Buzzer_IsRinging(void)
{
    return g_buzzer_ringing;
}
