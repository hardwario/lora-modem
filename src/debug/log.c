#include "log.h"

#include <string.h>
#include <stdio.h>
#include <rtt/segger_rtt.h>
#include "rtc.h"
#include "system.h"
#include "usart.h"

enum log_state
{
    LOG_STATE_SIMPLE_MSG = 0,
    LOG_STATE_COMPOSITE_MSG,
    LOG_STATE_HAVE_HEADER
};

typedef struct
{
    bool initialized;
    log_level_t level;
    log_timestamp_t timestamp;
    uint32_t tick_last;
    enum log_state state;
    char buffer[LOG_BUFFER_SIZE];
} log_t;


static log_t _log = { .initialized = false };


void _log_init(log_level_t level, log_timestamp_t timestamp)
{
    if (_log.initialized) return;

    memset(&_log, 0, sizeof(_log));

    _log.level = level;
    _log.timestamp = timestamp;
    _log.initialized = true;
    _log.state = LOG_STATE_SIMPLE_MSG;

#if LOG_TO_USART != 0
    usart_init();
#endif

#if LOG_TO_RTT != 0
    SEGGER_RTT_Init();
#endif
}


log_level_t _log_get_level(void)
{
    return _log.level;
}


void _log_set_level(log_level_t level)
{
    _log.level = level;
}


static void _write(const char *buf, size_t len)
{
#if LOG_TO_USART != 0
    usart_write(buf, len);
#endif

#if LOG_TO_RTT != 0
    SEGGER_RTT_Write(0, buf, len);
#endif
}


static void _write_message(log_level_t level, char id, const char *format, va_list ap)
{
#if LOG_TO_USART == 0 && LOG_TO_RTT == 0
    return;
#endif

    if (!_log.initialized) return;

    if (_log.level > level) return;

    size_t offset = 0;

    if (_log.state == LOG_STATE_SIMPLE_MSG || _log.state == LOG_STATE_COMPOSITE_MSG) {
        if (_log.timestamp == LOG_TIMESTAMP_ABS) {
            TimerTime_t now = rtc_tick2ms(rtc_get_timer_value());
            TimerTime_t timestamp_abs = now / 10;
            offset = sprintf(_log.buffer, "# %lu.%02lu <%c> ", timestamp_abs / 100, timestamp_abs % 100, id);
        } else if (_log.timestamp == LOG_TIMESTAMP_REL) {
            TimerTime_t now = rtc_tick2ms(rtc_get_timer_value());
            TimerTime_t timestamp_rel = (now - _log.tick_last) / 10;
            offset = sprintf(_log.buffer, "# +%lu.%02lu <%c> ", timestamp_rel / 100, timestamp_rel % 100, id);
            _log.tick_last = now;
        } else {
            strcpy(_log.buffer, "# <!> ");
            _log.buffer[3] = id;
            offset = 6;
        }

        if (_log.state == LOG_STATE_COMPOSITE_MSG)
            _log.state = LOG_STATE_HAVE_HEADER;
    }

    offset += vsnprintf(&_log.buffer[offset], sizeof(_log.buffer) - offset - 1, format, ap);

    if (offset >= sizeof(_log.buffer)) {
        offset = sizeof(_log.buffer) - 3 - 3; // space for ...\r\n
        _log.buffer[offset++] = '.';
        _log.buffer[offset++] = '.';
        _log.buffer[offset++] = '.';
    }

    if (_log.state == LOG_STATE_SIMPLE_MSG) {
        _log.buffer[offset++] = '\r';
        _log.buffer[offset++] = '\n';
    }

    _write(_log.buffer, offset);
}


void _log_dump(const void *buffer, size_t length, const char *format, ...)
{
    size_t position;
    size_t offset;
    va_list ap;
    uint32_t line_size;
    uint32_t i;

#if LOG_TO_USART == 0 && LOG_TO_RTT == 0
    return;
#endif

    if (_log.level > LOG_LEVEL_DUMP) return;

    va_start(ap, format);
    _write_message(LOG_LEVEL_DUMP, 'X', format, ap);
    va_end(ap);

    size_t offset_base = 0;

    for (; offset_base < sizeof(_log.buffer); offset_base++) {
        if (_log.buffer[offset_base] == '>') break;
    }

    offset_base += 2;

    if (buffer != NULL && length != 0) {
        for (position = 0; position < length; position += LOG_DUMP_WIDTH) {
            offset = offset_base + snprintf(_log.buffer + offset_base, sizeof(_log.buffer) - offset_base, "%3d: ", position);
            char *ptr_hex = _log.buffer + offset;
            offset += (LOG_DUMP_WIDTH * 3 + 2 + 1);
            char *ptr_text = _log.buffer + offset;
            offset += LOG_DUMP_WIDTH;

            if ((position + LOG_DUMP_WIDTH) <= length) {
                line_size = LOG_DUMP_WIDTH;
            } else {
                line_size = length - position;
            }

            for (i = 0; i < line_size; i++) {
                uint8_t value = ((uint8_t *)buffer)[position + i];

                if (i == (LOG_DUMP_WIDTH / 2)) {
                    *ptr_hex++ = '|';
                    *ptr_hex++ = ' ';
                }

                snprintf(ptr_hex, 4, "%02X ", value);

                ptr_hex += 3;

                if (value < 32 || value > 126) {
                    *ptr_text++ = '.';
                } else {
                    *ptr_text++ = value;
                }
            }

            for (; i < LOG_DUMP_WIDTH; i++) {
                if (i == (LOG_DUMP_WIDTH / 2)) {
                    *ptr_hex++ = '|';
                    *ptr_hex++ = ' ';
                }

                strcpy(ptr_hex, "   ");

                ptr_hex += 3;
                *ptr_text++ = ' ';
            }

            _log.buffer[offset++] = '\r';
            _log.buffer[offset++] = '\n';

            _write(_log.buffer, offset);
        }
    }
}


void _log_message(log_level_t level, char id, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    _write_message(level, id, format, ap);
    va_end(ap);
}


void _log_finish(void)
{
    switch(_log.state) {
        case LOG_STATE_SIMPLE_MSG:
            break;

        case LOG_STATE_COMPOSITE_MSG:
            break;

        case LOG_STATE_HAVE_HEADER:
            _write("\r\n", 2);
            break;

        default:
            // This is bug, do nothing
            break;
    }

    _log.state = LOG_STATE_SIMPLE_MSG;
}


void _log_compose(void)
{
    switch(_log.state) {
        case LOG_STATE_SIMPLE_MSG:
            _log.state = LOG_STATE_COMPOSITE_MSG;
            break;

        case LOG_STATE_COMPOSITE_MSG:
            // do nothing
            break;

        case LOG_STATE_HAVE_HEADER:
            // The previous message started via log_compose hasn't been finish.
            // Finish it now and start a new one.
            _log_finish();
            _log.state = LOG_STATE_COMPOSITE_MSG;
            break;

        default:
            // This is a bug, do nothing
            break;
    }
}
