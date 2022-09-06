#ifndef _ATCI_H
#define _ATCI_H

#include <stdint.h>
#include <stdbool.h>
#include "lpuart.h"

#define ATCI_EOL "\r\n\r\n"

#define ATCI_UNKNOWN_CMD "+ERR=-1" ATCI_EOL
#define ATCI_UKNOWN_CMD_LEN (sizeof(ATCI_UNKNOWN_CMD) - 1)

#define ATCI_OK "+OK" ATCI_EOL
#define ATCI_OK_LEN (sizeof(ATCI_OK) - 1)

#define ATCI_COMMANDS_LENGTH(COMMANDS) (sizeof(COMMANDS) / sizeof(COMMANDS[0]))

#define ATCI_COMMAND_CLAC {"+CLAC", atci_clac_action, NULL, NULL, NULL, "List all supported AT commands"}
#define ATCI_COMMAND_HELP {"$HELP", atci_help_action, NULL, NULL, NULL, "This help"}

#define atci_flush lpuart_flush


//! @brief AT param struct
typedef struct
{
    char *txt;
    size_t length;
    size_t offset;

} atci_param_t;


typedef enum
{
    ATCI_DATA_OK = 0,
    ATCI_DATA_ABORTED = -1,
    ATCI_DATA_ENCODING_ERROR = -2
} atci_data_status_t;


//! @brief AT command struct
typedef struct
{
    const char *command;
    void (*action)(atci_param_t *param);
    void (*set)(atci_param_t *param);
    void (*read)(void);
    void (*help)(void);
    const char *hint;

} atci_command_t;


typedef enum
{
    ATCI_ENCODING_BIN = 0,
    ATCI_ENCODING_HEX = 1
} atci_encoding_t;


//! @brief Initialize
//! @param[in] baudrate The baudrate to configure on the UART interface
//! @param[in] commands
//! @param[in] length Number of commands

void atci_init(unsigned int baudrate, const atci_command_t *commands, int length);


//! @brief
void atci_process(void);


//! @brief Print message
//! @param[in] message Message
size_t atci_print(const char *message);


//! @brief Print format message
//! @param[in] format Format string (printf style)
//! @param[in] ... Optional format arguments
//! @return Number of bytes written
size_t atci_printf(const char *format, ...) __attribute__ ((format (printf, 1, 2)));


//! @brief Print buffer as HEX string
//! @param[in] buffer Pointer to source buffer
//! @param[in] length Number of bytes to be written
//! @return Number of bytes written
size_t atci_print_buffer_as_hex(const void *buffer, size_t length);


//! @brief Write data
//! @param[in] buffer Pointer to source buffer
//! @param[in] length Number of bytes to be written
//! @return Number of bytes written
size_t atci_write(const char *buffer, size_t length);


//! @brief Parse buffer from HEX string
//! @param[in] param Param instance
//! @param[in] buffer Pointer to destination buffer
//! @param[in] length Number of bytes to be read
//! @param[in] param_length Maximum number of bytes to consume from param
//! @return Number of bytes read
size_t atci_param_get_buffer_from_hex(atci_param_t *param, void *buffer, size_t length, size_t param_length);


//! @brief Parse string to uint and move parsing cursor forward
//! @param[in] param Param instance
//! @param[in] value pointer to number
//! @return true On success
//! @return false On failure
bool atci_param_get_uint(atci_param_t *param, uint32_t *value);


//! @brief Parse string to int and move parsing cursor forward
//! @param[in] param Param instance
//! @param[in] value pointer to number
//! @return true On success
//! @return false On failure
bool atci_param_get_int(atci_param_t *param, int32_t *value);


//! @brief Check if the character at cursor is comma and move parsing cursor forward
//! @param[in] param Param instance
//! @return true Is comma
//! @return false No comma
bool atci_param_is_comma(atci_param_t *param);


//! @brief Set callback for next data
bool atci_set_read_next_data(size_t length, atci_encoding_t encoding, void (*callback)(atci_data_status_t status, atci_param_t *param));


//! @brief Abort the reception of next data, e.g., on time out
void atci_abort_read_next_data(void);


//! @brief Helper for clac action
void atci_clac_action(atci_param_t *param);

//! @brief Helper for help action
void atci_help_action(atci_param_t *param);

#endif //_ATCI_H
