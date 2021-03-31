#include "board.h"

void BoardGetUniqueId( uint8_t *id )
{
    system_get_unique_id(id);
}
