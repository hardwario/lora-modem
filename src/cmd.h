#ifndef _CMD_H
#define _CMD_H

#include "atci.h"


enum cmd_event {
    CMD_EVENT_MODULE  = 0,
    CMD_EVENT_JOIN    = 1,
    CMD_EVENT_NETWORK = 2,
    CMD_EVENT_CERT    = 9
};


enum cmd_event_module {
    CMD_MODULE_BOOT       = 0,
    CMD_MODULE_FACNEW     = 1,
    CMD_MODULE_BOOTLOADER = 2,
    CMD_MODULE_HALT       = 3
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


enum cmd_event_cert {
    CMD_CERT_CW_ENDED = 0,
    CMD_CERT_CM_ENDED = 1
};


extern bool schedule_reset;

void cmd_init(unsigned int baudrate);

void cmd_event(unsigned int type, unsigned subtype);

void cmd_ans(unsigned int margin, unsigned int gwcnt);

#define cmd_process atci_process
#define cmd_print atci_print
#define cmd_printf atci_printf

#if MKR1310 == 1
void process_uart_wakeup(void);
#endif

#endif // _CMD_H
