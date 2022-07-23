#ifndef _LOG_H
#define _LOG_H

#include <stddef.h>

#ifndef LOG_BUFFER_SIZE
#define LOG_BUFFER_SIZE 256
#endif

#ifndef LOG_TO_USART
#define LOG_TO_USART 1
#endif

#ifndef LOG_TO_RTT
#define LOG_TO_RTT 0
#endif

#define LOG_DUMP_WIDTH 8

//! @brief Log level

typedef enum
{
    //! @brief Logging DUMP
    LOG_LEVEL_DUMP = 0,

    //! @brief Log level DEBUG
    LOG_LEVEL_DEBUG = 1,

    //! @brief Log level INFO
    LOG_LEVEL_INFO = 2,

    //! @brief Log level WARNING
    LOG_LEVEL_WARNING = 3,

    //! @brief Log level ERROR
    LOG_LEVEL_ERROR = 4,

    //! @brief Logging disabled
    LOG_LEVEL_OFF = 5

} log_level_t;

//! @brief Log timestamp

typedef enum
{
    //! @brief Timestamp logging disabled
    LOG_TIMESTAMP_OFF = -1,

    //! @brief Timestamp logging enabled (absolute time format)
    LOG_TIMESTAMP_ABS = 0,

    //! @brief Timestamp logging enabled (relative time format)
    LOG_TIMESTAMP_REL = 1

} log_timestamp_t;

//! @brief Initialize logging facility
//! @param[in] level Minimum required message level for propagation
//! @param[in] timestamp Timestamp logging setting

#if defined(DEBUG)

void _log_init(log_level_t level, log_timestamp_t timestamp);

log_level_t _log_get_level(void);

void _log_set_level(log_level_t level);

//! @brief Log DUMP message (annotated in log as <X>)
//! @param[in] buffer Pointer to source buffer
//! @param[in] length Number of bytes to be printed
//! @param[in] format Format string (printf style)
//! @param[in] ... Optional format arguments

void _log_dump(const void *buffer, size_t length, const char *format, ...) __attribute__ ((format (printf, 3, 4)));

void _log_message(log_level_t level, char id, const char *format, ...) __attribute__ ((format (printf, 3, 4)));

//! @brief Start a log line composed via repeated calls to log_*
void _log_compose(void);

//! @brief Finish the log line previously started via log_compose
void _log_finish(void);

#define log_init(...)      _log_init(__VA_ARGS__)
#define log_get_level(...) _log_get_level(__VA_ARGS__)
#define log_set_level(...) _log_set_level(__VA_ARGS__)
#define log_dump(...)      _log_dump(__VA_ARGS__)
#define log_debug(...)     _log_message(LOG_LEVEL_DEBUG, 'D', __VA_ARGS__)
#define log_info(...)      _log_message(LOG_LEVEL_INFO, 'I', __VA_ARGS__)
#define log_warning(...)   _log_message(LOG_LEVEL_WARNING, 'W', __VA_ARGS__)
#define log_error(...)     _log_message(LOG_LEVEL_ERROR, 'E', __VA_ARGS__)
#define log_compose(...)   _log_compose(__VA_ARGS__)
#define log_finish(...)    _log_finish(__VA_ARGS__)

#else

#define log_init(...)      {}
#define log_get_level(...) {}
#define log_set_level(...) {}
#define log_dump(...)      {}
#define log_debug(...)     {}
#define log_info(...)      {}
#define log_warning(...)   {}
#define log_error(...)     {}
#define log_compose(...)   {}
#define log_finish(...)    {}

#endif

#endif // _LOG_H
