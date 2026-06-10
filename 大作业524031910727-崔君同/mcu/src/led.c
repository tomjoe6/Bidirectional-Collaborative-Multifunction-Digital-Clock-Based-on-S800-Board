#include "led.h"

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
