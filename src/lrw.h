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


//! @brief Initialize LoRaMac stack
//! @param[in] nvm NVM memory block to store persistent LoRaMac data
void lrw_init(const part_block_t *nvm);

//! @brief Return a pointer to the internal LoRaMac stack state
//! @retval A pointer to the internal LoRaMac state
LoRaMacNvmData_t *lrw_get_state(void);

//! @brief Lora process
void lrw_process(void);

//! @brief run Lora send data
//! @param[in] port Port
//! @param[in] buffer Pointer to source buffer
//! @param[in] length Number of bytes to be send
//! @param[in] confirmed set true for confirmed message
LoRaMacStatus_t lrw_send(uint8_t port, void *buffer, uint8_t length, bool confirmed);

//! @brief  Get whether or not the last sent data were acknowledged
//! @retval ENABLE if so, DISABLE otherwise
int lrw_isack_get(void);

//! @brief Activate LoRaMac according to the selected mode (OTAA or ABP)
//! @retval Negative number on error, 0 on success
int lrw_activate();

//! @brief Activate given LoRaWAN region
//! @param[in] region LoRaWAN region identifier
//! @retval Zero on success, negative number on error
int lrw_set_region(unsigned int region);

//! @brief Return currently selected LoRaWAN activation mode
//! @retval 1 for OTAA, 0 for ABP
unsigned int lrw_get_mode(void);

//! @brief Select a LoRaWAN activation mode
//! @param[in] mode 1 for OTAA, 0 for ABP
//! @retval 0 on success, negative number on error
int lrw_set_mode(unsigned int mode);

#endif // _LRW_H
