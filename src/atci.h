#ifndef _ATCI_H
#define _ATCI_H

#include "common.h"

#define ATCI_COMMANDS_LENGTH(COMMANDS) (sizeof(COMMANDS) / sizeof(COMMANDS[0]))

#define ATCI_COMMAND_CLAC {"+CLAC", atci_clac_action, NULL, NULL, NULL, ""}
#define ATCI_COMMAND_HELP {"$HELP", atci_help_action, NULL, NULL, NULL, "This help"}


//! @brief AT param struct
typedef struct
{
    char *txt;
    size_t length;
    size_t offset;

} atci_param_t;

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

//! @brief Initialize
//! @param[in] commands
//! @param[in] length Number of commands

void atci_init(const atci_command_t *commands, int length);

//! @brief

void atci_process(void);

//! @brief Print message
//! @param[in] message Message

size_t atci_print(const char *message);

//! @brief Print format message
//! @param[in] format Format string (printf style)
//! @param[in] ... Optional format arguments
//! @return Number of bytes written

size_t atci_printf(const char *format, ...);

//! @brief Print buffer as HEX string
//! @param[in] buffer Pointer to source buffer
//! @param[in] length Number of bytes to be written
//! @return Number of bytes written

size_t atci_print_buffer_as_hex(const void *buffer, size_t length);

//! @brief Parse buffer from HEX string
//! @param[in] param Param instance
//! @param[in] buffer Pointer to destination buffer
//! @param[in] length Number of bytes to be read
//! @return Number of bytes read

size_t atci_param_get_buffer_from_hex(atci_param_t *param, void *buffer, size_t length);

//! @brief Parse string to uint and move parsing cursor forward
//! @param[in] param Param instance
//! @param[in] value pointer to number
//! @return true On success
//! @return false On failure

bool atci_param_get_uint(atci_param_t *param, uint32_t *value);

//! @brief Check if the character at cursor is comma and move parsing cursor forward
//! @param[in] param Param instance
//! @return true Is comma
//! @return false No comma

bool atci_param_is_comma(atci_param_t *param);

//! @brief Set callback for next data

void atci_set_read_next_data(size_t length, void (*callback)(atci_param_t *param));

//! @brief Helper for clac action

void atci_clac_action(atci_param_t *param);

//! @brief Helper for help action

void atci_help_action(atci_param_t *param);

#endif //_ATCI_H
