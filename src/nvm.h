#ifndef _NVM_H_
#define _NVM_H_

#include "part.h"

#define SYSCONF_PART_SIZE 128

/* The sysconf data structure is meant to be used for platform configuration
 * (UART parameters, etc.) and for configuration that cannot be stored
 * elsewhere, e.g., the LoRaMAC MIB. Some of the parameters, e.g., device_class,
 * need to be kept synchronized with the MIB.
 */
typedef struct sysconf
{
    /* The baud rate to be used by the ATCI UART interface. The following values
     * are supported: 1200, 2400, 4800, 9600, 19200, 38400.
     */
    unsigned int uart_baudrate;

    /* The maximum time (in millseconds) for payload (data) uploads over the
     * ATCI. If the client does not upload all data within this time, the upload
     * will be terminated and the ATCI will wait for further AT commands.
     */
    uint16_t uart_timeout;

    /* The default port number to be used with AT+UTX and AT+CXT commands. */
    uint8_t default_port;

    /* The data encoding of payload data expected by the firmware. The value 0
     * indicates that the client submits payloads in binary form. The value 1
     * indicates that the client submits payloads in a hex form.
     */
    uint8_t data_format;

    /* This parameter controls whether the firmware enters low-power modes when
     * idle. Set to 0 to disable all low-power modes, set to 1 to enable
     * low-power modes.
     */
    uint8_t sleep;

    /* We need to keep LoRa device class here, in addition to the MIB, because
     * the MIB variable is reset to class A during Join. Having a separate copy
     * here allows us to restore the class after Join.
     */
    uint8_t device_class;

    /* The maximum number of retransmissions of unconfirmed uplink messages.
     * Receiving a downlink message from the network stops retransmissions.
     */
    uint8_t unconfirmed_retransmissions;

    /* The maximum number of retransmissions of confirmed uplink messages.
     * Receiving a downlink message from the network stops retransmissions.
     */
    uint8_t confirmed_retransmissions;

    uint32_t crc32;
} sysconf_t;


extern part_block_t nvm;

extern sysconf_t sysconf;
extern bool sysconf_modified;

void nvm_init(void);

int nvm_erase(void);

void sysconf_process(void);

#endif // _NVM_H_