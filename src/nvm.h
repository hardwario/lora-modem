#ifndef _NVM_H_
#define _NVM_H_

#include "part.h"


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

    /* The maximum time (in milliseconds) for payload (data) uploads over the
     * ATCI. If the client does not upload all data within this time, the upload
     * will be terminated, and the ATCI will wait for further AT commands.
     */
    uint16_t uart_timeout;

    /* The default port number to be used with AT+UTX and AT+CXT commands. */
    uint8_t default_port;

    /* The data encoding of payload data expected by the firmware. The value 0
     * indicates that the client submits payloads in binary form. The value 1
     * indicates that the client submits payloads in a hex form.
     */
    uint8_t data_format : 1;

    /* This parameter controls whether the firmware enters low-power modes when
     * idle. Set to 0 to disable all low-power modes, set to 1 to enable
     * low-power modes.
     */
    uint8_t sleep : 1;

    /* We need to keep LoRa device class here, in addition to the MIB, because
     * the MIB variable is reset to class A during Join. Having a separate copy
     * here allows us to restore the class after Join.
     */
    uint8_t device_class : 2;

    /* When this flag is set to 1, the AT command interface will prevent the
     * application from reading the various LoRaWAN security keys. The
     * corresponding AT commands will return an error. This flag can be only
     * reset back to 0 with a factory reset.
     */
    uint8_t lock_keys : 1;

    /* When this flag is set to 1 (default), the AT command interface will send
     * event notifications asynchronously. When set to 0, the asynchronous event
     * notifications will be buffered and will be only delivered to the host
     * between an AT command and its response (e.g., +OK or +ERR). This allows
     * the host to implement a polling mode where the UART can be powered down
     * between AT commands.
     */
    uint8_t async_uart : 1;

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


struct nvm_parts {
    part_t sysconf;
    part_t crypto;
    part_t mac1;
    part_t mac2;
    part_t se;
    part_t region1;
    part_t region2;
    part_t classb;
    part_t user;
};


#define USER_NVM_MAX_SIZE 64   // The maximum number of NVM user data registers
#define USER_NVM_MAGIC    0xD15C9101

typedef struct user_nvm_s {
    uint32_t    magic;
    uint8_t     values[USER_NVM_MAX_SIZE];
    uint32_t    crc32;
} user_nvm_t;


extern struct nvm_parts nvm_parts;
extern sysconf_t sysconf;
extern bool sysconf_modified;
extern uint16_t nvm_flags;
extern user_nvm_t user_nvm;

void nvm_init(void);

int nvm_erase(void);

void sysconf_process(void);

void nvm_update_user_data(void);

#endif // _NVM_H_
