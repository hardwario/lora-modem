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

#define UNKNOWN_CMD "+ERR=-1\r\n\r\n"

enum parser_state
{
    ATCI_START_STATE = 0,
    ATCI_PREFIX_STATE,
    ATCI_ATTENTION_STATE
};

static void _atci_process_character(char character);
static void _atci_process_command(void);
static void finish_next_data(atci_data_status_t status);

static struct
{
    const atci_command_t *commands;
    size_t commands_length;
    char rx_buffer[256];
    size_t rx_length;
    bool rx_error;
    bool aborted;
    enum parser_state state;

    char tmp[256];

    struct
    {
        size_t length;
        atci_encoding_t encoding;
        void (*callback)(atci_data_status_t status, atci_param_t *param);
    } read_next_data;

} _atci;

void atci_init(unsigned int baudrate, const atci_command_t *commands, int length)
{
    memset(&_atci, 0, sizeof(_atci));

    lpuart_init(baudrate);

    _atci.commands = commands;
    _atci.commands_length = length;
}

void atci_process(void)
{
    uint32_t masked;
    cbuf_view_t data;

    masked = disable_irq();
    system_sleep_lock &= ~SYSTEM_MODULE_ATCI;
    reenable_irq(masked);

    while (true)
    {
        if (_atci.aborted)
        {
            finish_next_data(ATCI_DATA_ABORTED);
            _atci.aborted = false;
        }

        masked = disable_irq();
        cbuf_head(&lpuart_rx_fifo, &data);
        reenable_irq(masked);

        if ((data.len[0] + data.len[1]) == 0)
        {
            break;
        }

        for (size_t i = 0; i < data.len[0]; i++)
        {
            _atci_process_character((char)data.ptr[0][i]);
        }

        for (size_t i = 0; i < data.len[1]; i++)
        {
            _atci_process_character((char)data.ptr[1][i]);
        }

        masked = disable_irq();
        cbuf_consume(&lpuart_rx_fifo, data.len[0] + data.len[1]);
        reenable_irq(masked);
    }
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
    length = vsnprintf(_atci.tmp, sizeof(_atci.tmp), format, ap);
    va_end(ap);

    if (length > sizeof(_atci.tmp))
    {
        length = sizeof(_atci.tmp);
    }

    lpuart_write_blocking(_atci.tmp, length);
    return length;
}

size_t atci_print_buffer_as_hex(const void *buffer, size_t length)
{
    char byte;
    size_t on_write = 0;

    for (size_t i = 0; i < length; i++)
    {
        byte = ((char *)buffer)[i];

        char upper = (byte >> 4) & 0xf;
        char lower = byte & 0x0f;

        _atci.tmp[on_write++] = upper < 10 ? upper + '0' : upper - 10 + 'A';
        _atci.tmp[on_write++] = lower < 10 ? lower + '0' : lower - 10 + 'A';
    }

    lpuart_write_blocking(_atci.tmp, on_write);
    return on_write;
}

size_t atci_write(const char *buffer, size_t length)
{
    lpuart_write_blocking(buffer, length);
    return length;
}

static int hex2bin(char c)
{
    if ((c >= '0') && (c <= '9'))
    {
        return c - '0';
    }
    else if ((c >= 'A') && (c <= 'F'))
    {
        return c - 'A' + 10;
    }
    else if ((c >= 'a') && (c <= 'f'))
    {
        return c - 'a' + 10;
    }
    else
    {
        return -1;
    }
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

    if ((buffer == NULL) || (length < param_length / 2)) {
        return 0;
    }

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
    if (param->offset >= param->length)
    {
        return false;
    }

    char c;

    *value = 0;

    while (param->offset < param->length)
    {
        c = param->txt[param->offset];

        if (isdigit(c))
        {
            *value *= 10;
            *value += c - '0';
        }
        else
        {
            if (c == ',')
            {
                return true;
            }
            return false;
        }

        param->offset++;
    }

    return true;
}

bool atci_param_get_int(atci_param_t *param, int32_t *value)
{
    if (param->offset >= param->length)
    {
        return false;
    }

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
    if (sizeof(_atci.rx_buffer) <= length)
        return false;

    if (length == 0) {
        if (callback != NULL) {
            atci_param_t param = { .txt = "", .length = 0, .offset = 0 };
            callback(ATCI_DATA_OK, &param);
        }
        return true;
    }

    _atci.read_next_data.length = length;
    _atci.read_next_data.encoding = encoding;
    _atci.read_next_data.callback = callback;

    return true;
}

void atci_abort_read_next_data(void)
{
    _atci.aborted = true;
    uint32_t mask = disable_irq();
    system_sleep_lock |= SYSTEM_MODULE_ATCI;
    reenable_irq(mask);

}

void atci_clac_action(atci_param_t *param)
{
    (void)param;
    for (size_t i = 0; i < _atci.commands_length; i++)
    {
        atci_printf("AT%s\r\n", _atci.commands[i].command);
    }
    lpuart_write_blocking("\r\n", 2);
}

void atci_help_action(atci_param_t *param)
{
    (void)param;
    for (size_t i = 0; i < _atci.commands_length; i++)
    {
        atci_printf("AT%s %s\r\n", _atci.commands[i].command, _atci.commands[i].hint);
    }
    lpuart_write_blocking("\r\n", 2);
}

static void _atci_process_command(void)
{
    log_debug("ATCI: %s", _atci.rx_buffer);

    if (_atci.rx_length < 2 || _atci.rx_buffer[0] != 'A' || _atci.rx_buffer[1] != 'T')
    {
        return;
    }

    if (_atci.rx_length == 2)
    {
        lpuart_write_blocking("+OK\r\n\r\n", 7);
        return;
    }

    _atci.rx_buffer[_atci.rx_length] = 0;

    char *name = _atci.rx_buffer + 2;

    size_t length = _atci.rx_length - 2;

    size_t command_len;

    const atci_command_t *command;

    for (size_t i = 0; i < _atci.commands_length; i++)
    {
        command = _atci.commands + i;

        command_len = strlen(command->command);

        if (length < command_len)
        {
            continue;
        }

        if (strncmp(name, command->command, command_len) != 0)
        {
            continue;
        }

        if (command_len == length)
        {
            if (command->action != NULL)
            {
                command->action(NULL);
                return;
            }
        }
        else if (name[command_len] == '=')
        {
            if ((name[command_len + 1]) == '?' && (command_len + 2 == length))
            {
                if (command->help != NULL)
                {
                    command->help();
                    return;
                }
            }

            if (command->set != NULL)
            {
                atci_param_t param = {
                    .txt = name + command_len + 1,
                    .length = length - command_len - 1,
                    .offset = 0};

                command->set(&param);
                return;
            }
        }
        else if (name[command_len] == '?' && command_len + 1 == length)
        {
            if (command->read != NULL)
            {
                command->read();
                return;
            }
        }
        else if (name[command_len] == ' ' && command_len + 1 < length)
        {
            if (command->action != NULL)
            {
                atci_param_t param = {
                    .txt = name + command_len + 1,
                    .length = length - command_len - 1,
                    .offset = 0};

                command->action(&param);
                return;
            }
        }
    }

    lpuart_write_blocking(UNKNOWN_CMD, sizeof(UNKNOWN_CMD) - 1);
}

static void finish_next_data(atci_data_status_t status)
{
    _atci.read_next_data.length = 0;
    _atci.read_next_data.encoding = ATCI_ENCODING_BIN;
    _atci.rx_buffer[_atci.rx_length] = 0;

    if (_atci.read_next_data.callback != NULL)
    {
        atci_param_t param = {
            .txt = _atci.rx_buffer,
            .length = _atci.rx_length,
            .offset = 0
        };
        _atci.read_next_data.callback(status, &param);
    }

    _atci.rx_length = 0;
}

static void _atci_process_data(char character)
{
    int c;
    static bool even = true;

    switch(_atci.read_next_data.encoding)
    {
        case ATCI_ENCODING_BIN:
            _atci.rx_buffer[_atci.rx_length++] = character;
            break;

        case ATCI_ENCODING_HEX:
            c = hex2bin(character);
            if (c < 0) {
                _atci.rx_error = true;
                break;
            }
            if (even)
            {
                _atci.rx_buffer[_atci.rx_length] = c << 4;
                even = false;
            }
            else
            {
                _atci.rx_buffer[_atci.rx_length++] |= c;
                even = true;
            }
            break;

        default:
            halt("Unsupported payload encoding");
            break;
    }

    if (_atci.read_next_data.length == _atci.rx_length || _atci.rx_error)
    {
        even = true;
        finish_next_data(_atci.rx_error ? ATCI_DATA_ENCODING_ERROR : ATCI_DATA_OK);
        _atci.rx_error = false;
    }
}

static void reset(void)
{
    _atci.rx_length = 0;
    _atci.state = ATCI_START_STATE;
}

static int append_to_buffer(char c)
{
    if (_atci.rx_length >= sizeof(_atci.rx_buffer) - 1)
    {
        return -1;
    }

    _atci.rx_buffer[_atci.rx_length++] = c;
    return 0;
}

static void _atci_process_character(char character)
{
    if (_atci.read_next_data.length != 0)
    {
        _atci_process_data(character);
        return;
    }

    if (character == '\n')
    {
        // Ignore LF characters, AT commands are terminated with CR
        return;
    }
    else if (character == '\x1b')
    {
        // If we get an ESC character, reset the buffer
        reset();
        return;
    }

    switch (_atci.state) {
        case ATCI_START_STATE:
            if (character == 'A')
            {
                append_to_buffer(character);
                _atci.state = ATCI_PREFIX_STATE;
            }
            break;

        case ATCI_PREFIX_STATE:
            if (character == 'T')
            {
                append_to_buffer(character);
                _atci.state = ATCI_ATTENTION_STATE;
            }
            else
            {
                reset();
            }
            break;

        case ATCI_ATTENTION_STATE:
            if (character == '\r')
            {
                _atci.rx_buffer[_atci.rx_length] = 0;
                _atci_process_command();
                reset();
            }
            else if (append_to_buffer(character) < 0)
            {
                lpuart_write_blocking(UNKNOWN_CMD, sizeof(UNKNOWN_CMD) - 1);
                reset();
            }
            break;

        default:
            halt("Bug: Invalid state in ATCI parser");
            break;
    }
}
