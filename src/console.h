#ifndef _CONSOLE_H
#define _CONSOLE_H

#include "common.h"

void console_init(void);
size_t console_write(const char *buffer, size_t length);
size_t console_read(char *buffer, size_t length);

#endif
