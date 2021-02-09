#ifndef __BOARD_H__
#define __BOARD_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include "utilities.h"

/*!
 * \brief Gets the board 64 bits unique ID
 *
 * \param [IN] id Pointer to an array that will contain the Unique ID
 */
void BoardGetUniqueId( uint8_t *id );

#ifdef __cplusplus
}
#endif

#endif // __BOARD_H__
