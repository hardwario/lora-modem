#include "atci.h"
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdio.h>
#include "lpuart.h"
#include "log.h"
#include "halt.h"
#include "system.h"
#include "irq.h"


enum parser_state
{
    ATCI_START_STATE = 0,
    ATCI_PREFIX_STATE,
    ATCI_ATTENTION_STATE
};


static struct
{
    const atci_command_t *commands;
    size_t commands_length;
    char rx_buffer[256];
    size_t rx_length;
    bool rx_error;
    bool aborted;
    enum parser_state parser_state;

    char tmp[256];

    struct
    {
        size_t length;
        atci_encoding_t encoding;
        void (*callback)(atci_data_status_t status, atci_param_t *param);
    } read_next_data;

} state;


void atci_init(unsigned int baudrate, const atci_command_t *commands, int length)
{
    memset(&state, 0, sizeof(state));

    lpuart_init(baudrate);

    state.commands = commands;
    state.commands_length = length;
}


size_t atci_print(const char *message)
{
    size_t len = strlen(message);
    lpuart_write_blocking(message, len);
    return len;
}


size_t atci_printf(const char *format, ...)
{
    va_list ap;
    size_t length;
    va_start(ap, format);
    length = vsnprintf(state.tmp, sizeof(state.tmp), format, ap);
    va_end(ap);

    if (length > sizeof(state.tmp))
        length = sizeof(state.tmp);

    lpuart_write_blocking(state.tmp, length);
    return length;
}


size_t atci_print_buffer_as_hex(const void *buffer, size_t length)
{
    char byte;
    size_t on_write = 0;

    for (size_t i = 0; i < length; i++) {
        byte = ((char *)buffer)[i];

        char upper = (byte >> 4) & 0xf;
        char lower = byte & 0x0f;

        state.tmp[on_write++] = upper < 10 ? upper + '0' : upper - 10 + 'A';
        state.tmp[on_write++] = lower < 10 ? lower + '0' : lower - 10 + 'A';
    }

    lpuart_write_blocking(state.tmp, on_write);
    return on_write;
}


size_t atci_write(const char *buffer, size_t length)
{
    lpuart_write_blocking(buffer, length);
    return length;
}


static int hex2bin(char c)
{
    if ((c >= '0') && (c <= '9')) return c - '0';
    else if ((c >= 'A') && (c <= 'F')) return c - 'A' + 10;
    else if ((c >= 'a') && (c <= 'f')) return c - 'a' + 10;
    else return -1;
}


size_t atci_param_get_buffer_from_hex(atci_param_t *param, void *buffer, size_t length, size_t param_length)
{
    char c;
    size_t i, max_i = length * 2, l = 0;
    int temp;

    if (param_length == 0) {
        param_length = param->length - param->offset;
    } else if ((param->length - param->offset) < param_length) {
        return 0;
    }

    if ((buffer == NULL) || (length < param_length / 2))
        return 0;

    for (i = 0; (i < max_i) && (param_length - i); i++) {
        c = param->txt[param->offset++];

        temp = hex2bin(c);
        if (temp < 0) return 0;

        if (i % 2 == 0) {
            ((uint8_t *)buffer)[l] = temp << 4;
        } else {
            ((uint8_t *)buffer)[l++] |= temp;
        }
    }

    return l;
}


bool atci_param_get_uint(atci_param_t *param, uint32_t *value)
{
    if (param->offset >= param->length) return false;

    char c;
    *value = 0;

    while (param->offset < param->length) {
        c = param->txt[param->offset];

        if (isdigit(c)) {
            *value *= 10;
            *value += c - '0';
        } else {
            if (c == ',') return true;
            return false;
        }

        param->offset++;
    }

    return true;
}


bool atci_param_get_int(atci_param_t *param, int32_t *value)
{
    if (param->offset >= param->length)
        return false;

    char c = param->txt[param->offset];

    int mul = c == '-' ? -1 : 1;
    if (c == '+' || c=='-') param->offset++;

    bool rv = atci_param_get_uint(param, (uint32_t*)value);
    *value *= mul;
    return rv;
}


bool atci_param_is_comma(atci_param_t *param)
{
    return param->txt[param->offset++] == ',';
}


bool atci_set_read_next_data(size_t length, atci_encoding_t encoding, void (*callback)(atci_data_status_t status, atci_param_t *param))
{
    if (sizeof(state.rx_buffer) <= length)
        return false;

    if (length == 0) {
        if (callback != NULL) {
            atci_param_t param = { .txt = "", .length = 0, .offset = 0 };
            callback(ATCI_DATA_OK, &param);
        }
        return true;
    }

    state.read_next_data.length = length;
    state.read_next_data.encoding = encoding;
    state.read_next_data.callback = callback;

    return true;
}


void atci_abort_read_next_data(void)
{
    state.aborted = true;
    uint32_t mask = disable_irq();
    system_sleep_lock |= SYSTEM_MODULE_ATCI;
    reenable_irq(mask);
}


void atci_clac_action(atci_param_t *param)
{
    (void)param;
    for (size_t i = 0; i < state.commands_length; i++)
        atci_printf("AT%s\r\n", state.commands[i].command);

    lpuart_write_blocking(ATCI_OK, ATCI_OK_LEN);
}


void atci_help_action(atci_param_t *param)
{
    (void)param;
    for (size_t i = 0; i < state.commands_length; i++)
        atci_printf("AT%s %s\r\n", state.commands[i].command, state.commands[i].hint);

    lpuart_write_blocking(ATCI_OK, ATCI_OK_LEN);
}


static void finish_next_data(atci_data_status_t status)
{
    state.read_next_data.length = 0;
    state.read_next_data.encoding = ATCI_ENCODING_BIN;
    state.rx_buffer[state.rx_length] = 0;

    if (state.read_next_data.callback != NULL) {
        atci_param_t param = {
            .txt = state.rx_buffer,
            .length = state.rx_length,
            .offset = 0
        };
        state.read_next_data.callback(status, &param);
    }

    state.rx_length = 0;
}


static void process_command(void)
{
    log_debug("ATCI: %s", state.rx_buffer);

    if (state.rx_length < 2) return;
    if (state.rx_buffer[0] != 'A' && state.rx_buffer[0] != 'a') return;
    if (state.rx_buffer[1] != 'T' && state.rx_buffer[1] != 't') return;

    if (state.rx_length == 2) {
        lpuart_write_blocking(ATCI_OK, ATCI_OK_LEN);
        return;
    }

    state.rx_buffer[state.rx_length] = 0;

    for(size_t i = 2; i < state.rx_length; i++)
        switch(state.rx_buffer[i]) {
            case '=':
            case '?':
            case ' ':
                break;

            default:
                state.rx_buffer[i] = toupper(state.rx_buffer[i]);
                break;
        }

    char *name = state.rx_buffer + 2;
    size_t name_len = state.rx_length - 2;

    const atci_command_t *cmd;
    size_t cmd_len;
    for (size_t i = 0; i < state.commands_length; i++) {
        cmd = state.commands + i;
        cmd_len = strlen(cmd->command);

        if (name_len < cmd_len) continue;
        if (strncmp(name, cmd->command, cmd_len) != 0) continue;

        if (cmd_len == name_len) {
            if (cmd->action != NULL) {
                cmd->action(NULL);
                return;
            }
        } else if (name[cmd_len] == '=') {
            if (name[cmd_len + 1] == '?' && (cmd_len + 2 == name_len) && cmd->help) {
                cmd->help();
                return;
            }

            if (cmd->set != NULL) {
                atci_param_t param = {
                    .txt    = name + cmd_len + 1,
                    .length = name_len - cmd_len - 1,
                    .offset = 0
                };
                cmd->set(&param);
                return;
            }
        } else if (name[cmd_len] == '?' && cmd_len + 1 == name_len) {
            if (cmd->read != NULL) {
                cmd->read();
                return;
            }
        } else if (name[cmd_len] == ' ' && cmd_len + 1 < name_len) {
            if (cmd->action != NULL) {
                atci_param_t param = {
                    .txt    = name + cmd_len + 1,
                    .length = name_len - cmd_len - 1,
                    .offset = 0
                };
                cmd->action(&param);
                return;
            }
        }
    }

    lpuart_write_blocking(ATCI_UNKNOWN_CMD, ATCI_UKNOWN_CMD_LEN);
}


static void process_data(char character)
{
    int c;
    static bool even = true;

    switch(state.read_next_data.encoding) {
        case ATCI_ENCODING_BIN:
            state.rx_buffer[state.rx_length++] = character;
            break;

        case ATCI_ENCODING_HEX:
            c = hex2bin(character);
            if (c < 0) {
                state.rx_error = true;
                break;
            }
            if (even) {
                state.rx_buffer[state.rx_length] = c << 4;
                even = false;
            } else {
                state.rx_buffer[state.rx_length++] |= c;
                even = true;
            }
            break;

        default:
            halt("Unsupported payload encoding");
            break;
    }

    if (state.read_next_data.length == state.rx_length || state.rx_error) {
        even = true;
        finish_next_data(state.rx_error ? ATCI_DATA_ENCODING_ERROR : ATCI_DATA_OK);
        state.rx_error = false;
    }
}


static void reset(void)
{
    state.rx_length = 0;
    state.parser_state = ATCI_START_STATE;
}


static int append_to_buffer(char c)
{
    if (state.rx_length >= sizeof(state.rx_buffer) - 1) return -1;
    state.rx_buffer[state.rx_length++] = c;
    return 0;
}


static void process_character(char character)
{
    if (state.read_next_data.length != 0) {
        process_data(character);
        return;
    }

    // Ignore LF characters, AT commands are terminated with CR
    if (character == '\n') return;

    if (character == '\x1b') {
        // If we get an ESC character, reset the buffer
        reset();
        return;
    }

    switch (state.parser_state) {
        case ATCI_START_STATE:
            if (character == 'A' || character == 'a') {
                append_to_buffer(character);
                state.parser_state = ATCI_PREFIX_STATE;
            }
            break;

        case ATCI_PREFIX_STATE:
            if (character == 'T' || character == 't') {
                append_to_buffer(character);
                state.parser_state = ATCI_ATTENTION_STATE;
            } else {
                reset();
            }
            break;

        case ATCI_ATTENTION_STATE:
            if (character == '\r') {
                state.rx_buffer[state.rx_length] = 0;
                process_command();
                reset();
            } else if (append_to_buffer(character) < 0) {
                lpuart_write_blocking(ATCI_UNKNOWN_CMD, sizeof(ATCI_UNKNOWN_CMD) - 1);
                reset();
            }
            break;

        default:
            halt("Bug: Invalid state in ATCI parser");
            break;
    }
}


void atci_process(void)
{
    uint32_t masked;
    cbuf_view_t data;

    masked = disable_irq();
    system_sleep_lock &= ~SYSTEM_MODULE_ATCI;
    reenable_irq(masked);

    while (true) {
        if (state.aborted) {
            finish_next_data(ATCI_DATA_ABORTED);
            state.aborted = false;
        }

        masked = disable_irq();
        cbuf_head(&lpuart_rx_fifo, &data);
        reenable_irq(masked);

        if ((data.len[0] + data.len[1]) == 0) break;

        for (size_t i = 0; i < data.len[0]; i++)
            process_character((char)data.ptr[0][i]);

        for (size_t i = 0; i < data.len[1]; i++)
            process_character((char)data.ptr[1][i]);

        masked = disable_irq();
        cbuf_consume(&lpuart_rx_fifo, data.len[0] + data.len[1]);
        reenable_irq(masked);
    }
}
