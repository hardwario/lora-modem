#ifndef __LPUART_H__
#define __LPUART_H__

#include <stddef.h>
#include <stdbool.h>
#include "cbuf.h"
#include "gpio.h"


extern volatile cbuf_t lpuart_tx_fifo;
extern volatile cbuf_t lpuart_rx_fifo;

#if DETACHABLE_LPUART == 1

/*! @brief Detach from the ATCI LPUART port
 *
 * Detach (disconnect) from the LPUART port used by the AT command interface.
 * Detaching pauses active DMA transfer (if any) and reconfigures the GPIO ports
 * used by the LPUART port in analog mode.
 *
 * This function is intended for use on boards that share the LPUART lines with
 * some other peripheral. This is the case, e.g., on MKRWAN boards. Forcing the
 * modem to detach allows the host to use the lines to communicate temporarily
 * with the other peripheral.
 */
void lpuart_detach(void);

/*! @brief Attach the ATCI LPUART port
 *
 * Attach to the LPUART port used by the AT command interface. Calling this
 * function reconfigures the GPIO ports used by LPUART1, and if there is an
 * active DMA transfer, it is resumed.
 *
 * This function is intended to restore ATCI functionality after the modem has
 * detached from LPUART1 (used by the ATCI). The function should be invoked
 * after an interrupt on a preconfigured GPIO pin or timeout.
 */
void lpuart_attach(void);

#endif

/*! @brief Initialize LPUART1
 *
 * Initialize the LPUART1 port for buffered DMA-based I/O. Both transmission and
 * reception will use DMA. Two fixed-size FIFOs backed by circular buffers are
 * used to enqueue outgoing and incoming data.
 *
 * @param[in] baudrate The baudrate to be configured
 */
void lpuart_init(unsigned int baudrate);


/*! @brief Write up to @p bytes to LPUART1
 *
 * Schedule up to @p length bytes of data from @p buffer for transmission over
 * LPUART1. The data is copied into an internal queue and will be transmitted as
 * soon as possible. If there is not enough space to store @p length bytes in
 * the internal queue, the function enqueues as many bytes as possible.
 *
 * This is a non-blocking function.
 *
 * @param[in] buffer A pointer to a memory buffer with data to be sent
 * @param[in] length The number of bytes from @p buffer to be sent
 * @return Number of bytes from @p buffer enqueued (less than or equal to @p length )
 */
size_t lpuart_write(const char *buffer, size_t length);


/*! @brief Write @p bytes to LPUART1
 *
 * Schedule @p length bytes of data from @p buffer for transmission over
 * LPUART1. This is a blocking version of lpuart_write. This function blocks
 * until all data have been written into the internal memory queue.
 *
 * Note: If you want to wait until all data have been transmitted over the port,
 * invoke lpuart_flush after this function.
 *
 * @param[in] buffer A pointer to a memory buffer with data to be sent
 * @param[in] length The number of bytes from @p buffer to be sent
 */
void lpuart_write_blocking(const char *buffer, size_t length);


/*! @brief Read up to @p length bytes from LPUART1
 *
 * This function reads up to @p length bytes from the LPUART1 port and copies
 * the data into the destination buffer @p buffer . If there is not enough data
 * in the internal queue, the function will read fewer than @p length bytes.
 * Number of bytes read is returned.
 *
 * This is a non-blocking function.
 *
 * @param[in] buffer A pointer to the destination buffer
 * @param[in] length The maximum number of bytes to read
 * @return The number of bytes read (less than or equal to @p length )
 */
size_t lpuart_read(char *buffer, size_t length);


/*! @brief Wait for all data from the internal queue to be sent
 *
 * This function blocks until all data from the internal queue have been
 * transmitted.
 */
void lpuart_flush(void);


/*! @brief Pause DMA and enable the WKUP interrupt on LPUART1
 *
 * This function is meant to be invoked by the system before it enters the Stop
 * low-power mode. In this mode, DMA is paused, but its registers are retained.
 * To be able to receive data in Stop mode, we pause DMA and enable the WKUP
 * interrupt, which will wake the MCU once a start bit has been detected on the
 * line.
 */
void lpuart_before_stop(void);


/*! @brief Disable WKUP interrupt and resume DMA on LPUART1
 *
 * This function is meant to be invoked after the system has left the low-power
 * Stop mode. It disables the WKUP interrupt on LPUART1 and resumes DMA-based
 * receive operation. This function can only be used with low-power modes that
 * retain DMA register values, e.g., the Stop mode.
 */
void lpuart_after_stop(void);


/*! @brief Pause modem->host transmissions over LPUART1
 *
 * Calling this function pauses modem->host transmissions over LPUART1 until
 * lpuart_resume_tx is called again. Previous and existing data will still
 * finish transmitting, but any data written to the port after calling this
 * function will only be written into an internal buffer. The data will begin
 * transmitting after the next call to lpuart_resume_tx.
 *
 * This function allows the implementation of a polling mode of communication,
 * where the host polls the modem for asynchronous messages. This is useful,
 * e.g., if the host needs to save power by shutting down its UART peripheral
 * between interactions with the modem over the ATCI.
 */
void lpuart_pause_tx();


/*! @brief Resume modem->host transmissions over LPUART1
 *
 * Invoke this function to resume modem->host transmissions over LPUART1 after
 * calling lpuart_pause_tx(). All data stored in the internal transmission
 * buffer will be transmitted over the UART port.
 */
void lpuart_resume_tx();

#endif /* __LPUART_H__ */
