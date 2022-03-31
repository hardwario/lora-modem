#ifndef _LOG_H
#define _LOG_H

#include "common.h"

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

void log_init(log_level_t level, log_timestamp_t timestamp);

log_level_t log_get_level(void);

void log_set_level(log_level_t level);

//! @brief Log DUMP message (annotated in log as <X>)
//! @param[in] buffer Pointer to source buffer
//! @param[in] length Number of bytes to be printed
//! @param[in] format Format string (printf style)
//! @param[in] ... Optional format arguments

void log_dump(const void *buffer, size_t length, const char *format, ...) __attribute__ ((format (printf, 3, 4)));

//! @brief Log DEBUG message (annotated in log as <D>)
//! @param[in] format Format string (printf style)
//! @param[in] ... Optional format arguments

void log_debug(const char *format, ...) __attribute__ ((format (printf, 1, 2)));

//! @brief Log INFO message (annotated in log as <I>)
//! @param[in] format Format string (printf style)
//! @param[in] ... Optional format arguments

void log_info(const char *format, ...) __attribute__ ((format (printf, 1, 2)));

//! @brief Log WARNING message (annotated in log as <W>)
//! @param[in] format Format string (printf style)
//! @param[in] ... Optional format arguments

void log_warning(const char *format, ...) __attribute__ ((format (printf, 1, 2)));

//! @brief Log ERROR message (annotated in log as <E>)
//! @param[in] format Format string (printf style)
//! @param[in] ... Optional format arguments

void log_error(const char *format, ...) __attribute__ ((format (printf, 1, 2)));

//! @brief Start a log line composed via repeated calls to log_*
void log_compose(void);

//! @brief Finish the log line previously started via log_compose
void log_finish();

#endif // _LOG_H
