#ifndef _NVM_H_
#define _NVM_H_

#include "part.h"


typedef struct sysconf
{
    unsigned int uart_baudrate;
    uint8_t default_port;
    uint32_t crc32;
} sysconf_t;


extern part_block_t nvm;

extern sysconf_t sysconf;
extern bool sysconf_modified;

void nvm_init(void);

int nvm_erase(void);

void sysconf_process(void);

#endif // _NVM_H_