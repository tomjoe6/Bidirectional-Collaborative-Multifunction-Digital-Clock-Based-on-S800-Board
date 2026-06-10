#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "protocol.h"
#include "hw_config.h"
#include "clock.h"
#include "alarm.h"
#include "display.h"
#include "keys.h"
#include "buzzer.h"
#include "led.h"
#include "events.h"

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
