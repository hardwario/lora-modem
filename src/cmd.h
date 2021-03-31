#ifndef _AT_H
#define _AT_H

#include "atci.h"

void cmd_init(void);

void cmd_event(const uint8_t type, const uint8_t no);

#define cmd_process atci_process
#define cmd_print atci_print
#define cmd_printf atci_printf

#endif // _AT_H
