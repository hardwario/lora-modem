#include <board.h>
#include "hw.h"

void BoardGetUniqueId( uint8_t *id )
{
    HW_GetUniqueId(id);
}
