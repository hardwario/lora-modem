#ifndef _ATCI_H
#define _ATCI_H

#include "common.h"

#define ATCI_COMMANDS_LENGTH(COMMANDS) (sizeof(COMMANDS) / sizeof(COMMANDS[0]))

#define ATCI_COMMAND_CLAC {"+CLAC", atci_clac_action, NULL, NULL, NULL, ""}
#define ATCI_COMMAND_HELP {"$HELP", atci_help_action, NULL, NULL, NULL, "This help"}

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

void atci_process(void);

size_t atci_print(const char *buffer);

size_t atci_printf(const char *format, ...);

size_t atci_print_buffer_as_hex(const void *buffer, size_t length);

size_t atci_get_buffer_from_hex(atci_param_t *param, void *buffer, size_t length);

bool atci_get_uint(atci_param_t *param, uint32_t *value);

bool atci_is_comma(atci_param_t *param);

void atci_set_read_next_data(size_t length, void (*callback)(atci_param_t *param));

//! @brief Helper for clac action

void atci_clac_action(atci_param_t *param);

//! @brief Helper for help action

void atci_help_action(atci_param_t *param);

#endif //_ATCI_H
