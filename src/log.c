#include "log.h"
#include "rtc.h"
#include "system.h"
#include "console.h"
#include "usart.h"
#include <segger_rtt.h>

typedef struct
{
    bool initialized;
    log_level_t level;
    log_timestamp_t timestamp;
    uint32_t tick_last;
    char buffer[LOG_BUFFER_SIZE];

} log_t;

static log_t _log = {.initialized = false};

static void _log_message(log_level_t level, char id, const char *format, va_list ap);

void log_init(log_level_t level, log_timestamp_t timestamp)
{
    if (_log.initialized)
    {
        return;
    }

    memset(&_log, 0, sizeof(_log));

    _log.level = level;
    _log.timestamp = timestamp;
    _log.initialized = true;

    #if LOG_TO_USART != 0
    usart_init();
    #endif

    #if LOG_TO_RTT != 0
    SEGGER_RTT_Init();
    #endif
}

void log_dump(const void *buffer, size_t length, const char *format, ...)
{
    #if LOG_TO_USART == 0 && LOG_TO_RTT == 0
    return;
    #endif

    va_list ap;

    if (_log.level > LOG_LEVEL_DUMP)
    {
        return;
    }

    va_start(ap, format);
    _log_message(LOG_LEVEL_DUMP, 'X', format, ap);
    va_end(ap);

    size_t offset_base = 0;

    for (; offset_base < sizeof(_log.buffer); offset_base++)
    {
        if (_log.buffer[offset_base] == '>')
        {
            break;
        }
    }

    offset_base += 2;

    size_t position;

    size_t offset;

    if (buffer != NULL && length != 0)
    {
        for (position = 0; position < length; position += LOG_DUMP_WIDTH)
        {
            offset = offset_base + snprintf(_log.buffer + offset_base, sizeof(_log.buffer) - offset_base, "%3d: ", position);

            char *ptr_hex = _log.buffer + offset;

            offset += (LOG_DUMP_WIDTH * 3 + 2 + 1);

            char *ptr_text = _log.buffer + offset;

            offset += LOG_DUMP_WIDTH;

            uint32_t line_size;

            uint32_t i;

            if ((position + LOG_DUMP_WIDTH) <= length)
            {
                line_size = LOG_DUMP_WIDTH;
            }
            else
            {
                line_size = length - position;
            }

            for (i = 0; i < line_size; i++)
            {
                uint8_t value = ((uint8_t *)buffer)[position + i];

                if (i == (LOG_DUMP_WIDTH / 2))
                {
                    *ptr_hex++ = '|';
                    *ptr_hex++ = ' ';
                }

                snprintf(ptr_hex, 4, "%02X ", value);

                ptr_hex += 3;

                if (value < 32 || value > 126)
                {
                    *ptr_text++ = '.';
                }
                else
                {
                    *ptr_text++ = value;
                }
            }

            for (; i < LOG_DUMP_WIDTH; i++)
            {
                if (i == (LOG_DUMP_WIDTH / 2))
                {
                    *ptr_hex++ = '|';
                    *ptr_hex++ = ' ';
                }

                strcpy(ptr_hex, "   ");

                ptr_hex += 3;

                *ptr_text++ = ' ';
            }

            _log.buffer[offset++] = '\r';
            _log.buffer[offset++] = '\n';

            #if LOG_TO_USART != 0
            usart_write(_log.buffer, offset);
            #endif

            #if LOG_TO_RTT != 0
            SEGGER_RTT_Write(0, _log.buffer, offset);
            #endif
        }
    }
}

void log_debug(const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    _log_message(LOG_LEVEL_DEBUG, 'D', format, ap);
    va_end(ap);
}

void log_info(const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    _log_message(LOG_LEVEL_INFO, 'I', format, ap);
    va_end(ap);
}

void log_warning(const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    _log_message(LOG_LEVEL_WARNING, 'W', format, ap);
    va_end(ap);
}

void log_error(const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    _log_message(LOG_LEVEL_ERROR, 'E', format, ap);
    va_end(ap);
}

static void _log_message(log_level_t level, char id, const char *format, va_list ap)
{
    #if LOG_TO_USART == 0 && LOG_TO_RTT == 0
    return;
    #endif

    if (!_log.initialized)
    {
        return;
    }

    if (_log.level > level)
    {
        return;
    }

    size_t offset;

    if (_log.timestamp == LOG_TIMESTAMP_ABS)
    {
        uint32_t tick_now = rtc_get_timer_value();

        uint32_t timestamp_abs = tick_now / 10;

        offset = sprintf(_log.buffer, "# %lu.%02lu <%c> ", timestamp_abs / 100, timestamp_abs % 100, id);
    }
    else if (_log.timestamp == LOG_TIMESTAMP_REL)
    {
        uint32_t tick_now = rtc_get_timer_value();

        uint32_t timestamp_rel = (tick_now - _log.tick_last) / 10;

        offset = sprintf(_log.buffer, "# +%lu.%02lu <%c> ", timestamp_rel / 100, timestamp_rel % 100, id);

        _log.tick_last = tick_now;
    }
    else
    {
        strcpy(_log.buffer, "# <!> ");

        _log.buffer[3] = id;

        offset = 6;
    }

    offset += vsnprintf(&_log.buffer[offset], sizeof(_log.buffer) - offset - 1, format, ap);

    if (offset >= sizeof(_log.buffer))
    {
        offset = sizeof(_log.buffer) - 3 - 3; // space for ...\r\n
        _log.buffer[offset++] = '.';
        _log.buffer[offset++] = '.';
        _log.buffer[offset++] = '.';
    }

    _log.buffer[offset++] = '\r';
    _log.buffer[offset++] = '\n';

    #if LOG_TO_USART != 0
    usart_write(_log.buffer, offset);
    #endif

    #if LOG_TO_RTT != 0
    SEGGER_RTT_Write(0, _log.buffer, offset);
    #endif
}
