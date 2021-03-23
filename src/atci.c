#include "atci.h"
#include "console.h"
#include "log.h"

static void _atci_process_character(char character);
static void _atci_process_line(void);

static struct
{
    const atci_command_t *commands;
    size_t commands_length;
    char rx_buffer[256];
    size_t rx_length;
    bool rx_error;

    char tmp[256];

    struct
    {
        size_t length;
        void (*callback)(atci_param_t *param);
    } read_next_data;

} _atci;

void atci_init(const atci_command_t *commands, int length)
{
    memset(&_atci, 0, sizeof(_atci));

    console_init();

    _atci.commands = commands;
    _atci.commands_length = length;
}

void atci_process(void)
{
    while (true)
    {
        static char buffer[16];

        size_t length = console_read(buffer, sizeof(buffer));

        if (length == 0)
        {
            break;
        }

        for (size_t i = 0; i < length; i++)
        {
            _atci_process_character((char)buffer[i]);
        }
    }
}

size_t atci_print(const char *message)
{
    return console_write(message, strlen(message));
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

    return console_write(_atci.tmp, length);
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

    return console_write(_atci.tmp, on_write);
}

size_t atci_param_get_buffer_from_hex(atci_param_t *param, void *buffer, size_t length)
{
    if ((buffer == NULL) || ((param->length - param->offset) / 2 < length))
    {
        return 0;
    }

    char c;
    size_t i;
    size_t max_i = length * 2;
    uint8_t temp;
    size_t l = 0;

    for (i = 0; (i < max_i) && (param->offset < param->length); i++)
    {
        c = param->txt[param->offset++];

        if ((c >= '0') && (c <= '9'))
        {
            temp = c - '0';
        }
        else if ((c >= 'A') && (c <= 'F'))
        {
            temp = c - 'A' + 10;
        }
        else if ((c >= 'a') && (c <= 'f'))
        {
            temp = c - 'a' + 10;
        }
        else
        {
            return false;
        }

        if (i % 2 == 0)
        {
            ((uint8_t *)buffer)[l] = temp << 4;
        }
        else
        {
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

bool atci_param_is_comma(atci_param_t *param)
{
    return param->txt[param->offset++] == ',';
}

void atci_set_read_next_data(size_t length, void (*callback)(atci_param_t *param))
{
    _atci.read_next_data.length = length;
    _atci.read_next_data.callback = callback;
}

void atci_clac_action(atci_param_t *param)
{
    (void)param;
    for (size_t i = 0; i < _atci.commands_length; i++)
    {
        atci_printf("AT%s\r\n", _atci.commands[i].command);
    }
}

void atci_help_action(atci_param_t *param)
{
    (void)param;
    for (size_t i = 0; i < _atci.commands_length; i++)
    {
        atci_printf("AT%s %s\r\n", _atci.commands[i].command, _atci.commands[i].hint);
    }
}

static void _atci_process_line(void)
{
    log_debug("ATCI: %s", _atci.rx_buffer);

    if (_atci.rx_length < 2 || _atci.rx_buffer[0] != 'A' || _atci.rx_buffer[1] != 'T')
    {
        return;
    }

    if (_atci.rx_length == 2)
    {
        console_write("+OK", 3);
        return;
    }

    _atci.rx_buffer[_atci.rx_length] = 0;

    char *line = _atci.rx_buffer + 2;

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

        if (strncmp(line, command->command, command_len) != 0)
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
        else if (line[command_len] == '=')
        {
            if ((line[command_len + 1]) == '?' && (command_len + 2 == length))
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
                    .txt = line + command_len + 1,
                    .length = length - command_len - 1,
                    .offset = 0};

                command->set(&param);
                return;
            }
        }
        else if (line[command_len] == '?' && command_len + 1 == length)
        {
            if (command->read != NULL)
            {
                command->read();
                return;
            }
        }
        else if (line[command_len] == ' ' && command_len + 1 < length)
        {
            if (command->action != NULL)
            {
                atci_param_t param = {
                    .txt = line + command_len + 1,
                    .length = length - command_len - 1,
                    .offset = 0};

                command->action(&param);
                return;
            }
        }
        else
        {
            atci_printf("Unknown: %s", command);
            return;
        }
        break;
    }

    console_write("+ERR=-1", 7);
}

static void _atci_process_character(char character)
{
    if (_atci.read_next_data.length != 0)
    {
        _atci.rx_buffer[_atci.rx_length++] = character;
        if (_atci.read_next_data.length == _atci.rx_length)
        {
            _atci.read_next_data.length = 0;
            _atci.rx_buffer[_atci.rx_length] = 0;

            if (_atci.read_next_data.callback != NULL)
            {
                atci_param_t param = {
                    .txt = _atci.rx_buffer,
                    .length = _atci.rx_length,
                    .offset = 0};
                _atci.read_next_data.callback(&param);
            }

            _atci.rx_length = 0;
            _atci.rx_error = false;

            console_write("\r\n\r\n", 4);
        }
        return;
    }

    if (character == '\r')
    {
        if (_atci.rx_error)
        {
            console_write("+ERR=-1", 7);
        }
        else if (_atci.rx_length > 0)
        {
            _atci.rx_buffer[_atci.rx_length] = 0;
            _atci_process_line();
        }

        if (_atci.read_next_data.length == 0)
        {
            console_write("\r\n\r\n", 4);
        }

        _atci.rx_length = 0;
        _atci.rx_error = false;
    }
    else if (character == '\r')
    {
        return;
    }
    else if (character == '\x1b')
    {
        _atci.rx_length = 0;
        _atci.rx_error = false;
    }
    else if (_atci.rx_length == sizeof(_atci.rx_buffer) - 1)
    {
        _atci.rx_error = true;
    }
    else if (!_atci.rx_error)
    {
        _atci.rx_buffer[_atci.rx_length++] = character;
    }
}
