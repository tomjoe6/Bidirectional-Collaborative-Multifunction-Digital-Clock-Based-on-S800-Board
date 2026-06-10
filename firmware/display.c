#include <string.h>
#include <stdio.h>
#include "display.h"
#include "clock.h"

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
    if (c == '.') return 0x00;  /* DP handled by FillFromBuffer g_dp_mask */
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
    char temp[DISP_DIGITS + 4];  /* extra room for dots before removal */
    uint8_t temp_dp, temp_fill;

    str_len = (uint8_t)strlen(g_disp_buffer);

    /* Copy chars into temp with circular wrap for flow */
    temp_fill = 0;
    for (i = 0; i < (int16_t)(DISP_DIGITS + 4); i++) {
        vpos = g_flow_position + i;
        if ((g_disp_mode == DISP_MODE_FULL || g_disp_mode == DISP_MODE_YEAR)
            && str_len > DISP_DIGITS) {
            while (vpos >= str_len) vpos -= str_len;
            while (vpos < 0)      vpos += str_len;
        }
        if (vpos >= 0 && vpos < str_len && str_len > 0) {
            temp[i] = g_disp_buffer[vpos];
            temp_fill = (uint8_t)(i + 1);
        } else {
            temp[i] = ' ';
        }
    }

    /* DP processing: '.' sets DP on PREVIOUS digit, not a char itself */
    temp_dp = 0;
    for (i = 0; i < (int16_t)temp_fill; i++) {
        if (temp[i] == '.') {
            if (i > 0) temp_dp |= (uint8_t)(1 << (i - 1));
            /* shift left */
            { uint8_t j; for (j = (uint8_t)i; j < DISP_DIGITS + 3; j++) temp[j] = temp[j + 1]; }
            temp[DISP_DIGITS + 3] = ' ';
        }
    }

    /* Copy first 8 chars to display, pad spaces */
    for (i = 0; i < DISP_DIGITS; i++)
        g_disp_chars[i] = temp[i];

    /* FORMAT_RIGHT: reverse chars + DP */
    if (g_disp_format == FORMAT_RIGHT) {
        char rev[DISP_DIGITS]; uint8_t rev_dp = 0, k;
        for (i = 0; i < DISP_DIGITS; i++) rev[i] = g_disp_chars[DISP_DIGITS - 1 - i];
        for (i = 0; i < DISP_DIGITS; i++) {
            if (temp_dp & (1 << i)) {
                k = DISP_DIGITS - 2 - (uint8_t)i;
                if (k < DISP_DIGITS)
                    rev_dp |= (uint8_t)(1 << k);
            }
        }
        for (i = 0; i < DISP_DIGITS; i++) g_disp_chars[i] = rev[i];
        temp_dp = rev_dp;
    }

    g_dp_mask = temp_dp;
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
