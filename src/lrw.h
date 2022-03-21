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


extern McpsIndication_t lrw_rx_params;


/** @brief Initialize the LoRaMac stack
 *
 * If previously-saved state is found in NVM, the state will be restored and
 * LoRaMac will continue where it lef off. Otherwise, LoRaMac is initialized
 * with default parameter values.
 */
void lrw_init(void);


/** @brief Obtain a pointer to internal LoRaMac state
 *
 * Use this function to obtain a pointer to the internal state of the LoRaMac
 * library. This could be used to get or set parameters that have not been
 * exposed through LoRaMac's MIB API.
 *
 * @return A pointer to LoRaMac's internal state
 */
LoRaMacNvmData_t *lrw_get_state(void);


/** @brief LoRaMac main processing function. Should be invoked repeatedly as
 *  long as the system is not sleeping.
 */
void lrw_process(void);


/** @brief Send an uplink message
 *
 * This function can be used to send a confirmed or unconfirmed uplink message
 * to the specified LoRaWAN port number (1-223). Set the parameter @c confirmed
 * to true to request an ACK from the network. Note that the maximum size of the
 * message can vary considerably and depends on the currently selected data rate.
 *
 * @param[in] port LoRaWAN port number
 * @param[in] buffer Pointer to source buffer
 * @param[in] length Number of bytes in the source buffer
 * @param[in] confirmed Send as confirmed uplink when true
 * @return Zero on success, a @c LoRaMacStatus_t value on error
 */
int lrw_send(uint8_t port, void *buffer, uint8_t length, bool confirmed);


/** @brief Activate the node according to the mode selected with AT+MODE
 *
 * This activates the node on the network according to the mode previously
 * selected with AT+MODE. If the selected mode is OTAA, a Join message will be
 * sent to the LoRaWAN network server. If the selected mode is ABP, this
 * function will only perform internal re-configuration of the LoRaMac stack for
 * ABP. No messages will be sent to the network in this case.
 *
 * @return Zero on success, a @c LoRaMacState_t value on error
 */
int lrw_join(void);


/** @brief Perform a LoRaWAN link check
 *
 * This function sends a LoRaWAN @c LinkCheckReq message to the network server.
 * The server will return a @c LinkCheckAns back with the measured RSSI and SNR
 * values for the device. This function does not wait (block) for the answer.
 *
 * If the parameter @c piggyback is set to @c true, the @c LinkCheckReq request
 * will be sent together with the next uplink message sent by the client. Set
 * the parameter to @c false to send the request immediately. In that case, it
 * will be sent with an empty payload to port 0.

 * @param[in] piggyback Send with next uplink if @c true, send immediately otherwise
 * @return Zero on success, a @c LoRaMacStatus_t value on error
 */
int lrw_check_link(bool piggyback);


/** @brief Reconfigure LoRaMac for the given region
 *
 * This function reconfigures the LoRaMac library for the specified region. The
 * region must be active, i.e., the corresponding regional files must have been
 * included at compile time.
 *
 * This function will internally shutdown the LoRaMac library and perform a
 * factory reset of most of the persistent state stored in NVM in order to apply
 * new regional defaults. The application needs to invoke lrw_init() to
 * reactivate the stack upon invoking this function. System reboot is also
 * recommended.
 *
 * @param[in] region LoRaWAN region identifier
 * @return 0 on success, -1 if the region is already active, a @c LoRaMacState_t
 * value on error
 */
int lrw_set_region(unsigned int region);


/** @brief Return currently selected LoRaWAN activation mode (ABP or OTAA)
 * @retval 0 Activation by provisioning (ABP) mode is selected
 * @retval 1 Over the air activation (OTAA) mode is selected
 */
unsigned int lrw_get_mode(void);


/** @brief Select LoRaWAN activation mode (ABP or OTAA)
 *
 * Configure the LoRaMac library to use the specified activation mode. The value
 * 0 selects activation by personalization (ABP). The value 1 selects over the
 * air activation (OTAA).
 *
 * @return Zero on success, a @c LoRaMacStatus_t value on error
 */
int lrw_set_mode(unsigned int mode);


/** @brief Return current LoRaWan device class (A, B, or C)
 * @retval 0 if LoRaWAN class A is selected
 * @retval 1 if LoRaWAN class B is selected
 * @retval 2 if LoRaWAN class C is selected
 */
DeviceClass_t lrw_get_class(void);


/** @brief Select a LoRaWAN device class (A, B, or C)
 *
 * This function will configure the LoRaMac stack to use the specified class.
 * The value will be persistently stored in NVM.
 *
 * @param[in] device_class The class to be used (0 for A, 1 for B, 2 for C)
 * @return Zero on success, a @c LoRaMacStatus_t value on error
 */
int lrw_set_class(DeviceClass_t device_class);


/** @brief Configure the maximum effective isotropic radiated power (EIRP)
 * @param[in] maxeirp Maximum EIRP to be used by the transmitter
 */
void lrw_set_maxeirp(unsigned int maxeirp);


/** @brief Configure whether the node respects dwell time (only relevant in AS923)
 * @param[in] uplink Enable or disable dwell time checking on the uplink
 @ @param[in] downlink Enable or disable dwell time checking on the downlink
 * @return Zero on success, a @c LoRaMacStatus_t value on error
 */
int lrw_set_dwell(bool uplink, bool downlink);


/** @brief Return the length in words of the channel mask for the current region
 * @return Current region's channel mask length in words
 */
int lrw_get_chmask_length(void);


/** @brief Return a descriptor structure with information about configured RF channels
 * @return Channel descriptor structure
 */
lrw_channel_list_t lrw_get_channel_list(void);


#endif // _LRW_H
