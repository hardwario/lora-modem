#ifndef _LRW_H
#define _LRW_H

#include <loramac-node/src/mac/LoRaMac.h>
#include <loramac-node/src/mac/region/Region.h>
#include "part.h"


typedef struct
{
    ChannelParams_t *channels;
    uint8_t length;
    uint16_t* chmask;
    uint8_t chmask_length;
    uint16_t* chmask_default;
} lrw_channel_list_t;


extern bool lrw_irq;


void lrw_init(const part_block_t *nvm);

LoRaMacNvmData_t *lrw_get_state(void);

//! @brief Lora process
void lrw_process(void);

//! @brief run Lora send data
//! @param[in] port Port
//! @param[in] buffer Pointer to source buffer
//! @param[in] length Number of bytes to be send
//! @param[in] confirmed set true for confirmed message
bool lrw_send(uint8_t port, void *buffer, uint8_t length, bool confirmed);

//! @brief  Get whether or not the last sent data were acknowledged
//! @retval ENABLE if so, DISABLE otherwise
int lrw_isack_get(void);

void lrw_process(void);

int lrw_activate();

int lrw_set_region(unsigned int region);

int lrw_set_mode(unsigned int value);
unsigned int lrw_get_mode(void);

#endif // _LRW_H
