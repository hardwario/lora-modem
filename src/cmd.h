#ifndef _CMD_H
#define _CMD_H

#include "atci.h"


enum cmd_event {
    CMD_EVENT_MODULE  = 0,
    CMD_EVENT_JOIN    = 1,
    CMD_EVENT_NETWORK = 2
};


enum cmd_event_module {
    CMD_MODULE_BOOT       = 0,
    CMD_MODULE_FACNEW     = 1,
    CMD_MODULE_BOOTLOADER = 2
};


enum cmd_event_join {
    CMD_JOIN_FAILED    = 0,
    CMD_JOIN_SUCCEEDED = 1
};


enum cmd_event_net {
    CMD_NET_NOANSWER       = 0,
    CMD_NET_ANSWER         = 1,
    CMD_NET_RETRANSMISSION = 2
};


void cmd_init(void);

void cmd_event(unsigned int type, unsigned subtype);

#define cmd_process atci_process
#define cmd_print atci_print
#define cmd_printf atci_printf

#endif // _CMD_H
