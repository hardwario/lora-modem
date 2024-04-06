#ifndef __CBUF_H__
#define __CBUF_H__

#include <stddef.h>


/*! @brief A fixed-size circular buffer backed by a contiguous memory block
 *
 * This data structure can be used to implement a fixed-size first-in, first-out
 * (FIFO) or queue that can store up to @p max_length bytes.
 */
typedef struct cbuf {
    char *buffer;
    size_t max_length;  //! Maximum length of the circular buffer in bytes
    size_t length;      //! The number of bytes stored in the circular buffer
    size_t read;        //! The index of the first byte
    size_t write;       //! The index of the first empty element (if length > 0)
} cbuf_t;


/*! @brief A view into the circular buffer
 *
 * This is an auxiliary data structure accepted or returned by a couple of
 * functions below that can be used to obtain a reference to data or empty space
 * within a circular buffer. Since a circular wrapper can wrap around, the data
 * or empty space is represented by two pointers and two length fields. The
 * application needs to handle this correctly.
 */
typedef struct cbuf_view {
    char *ptr[2];
    size_t len[2];
} cbuf_view_t;


/*! @brief Initialize @p cbuf with the memory given in @p buffer
 *
 * @param[in] queue A pointer to the circular buffer to be initialized
 * @param[in] buffer A pointer to the memory buffer to back the circular buffer
 * @param[in] size The size of the memory buffer in bytes
 */
void cbuf_init(volatile cbuf_t *cbuf, void *buffer, size_t size);


/*! @brief Return a view representing free space at the end of @p cbuf
 *
 * This function can be used to obtain a view of the empty space (if any) at the
 * end of the circular buffer. The view can be used to append data. The function
 * returns the same pointer that is passed to it via @p tail .
 *
 * Thread-safe: no Running time: constant
 *
 * @param[in] cbuf A pointer to the circular buffer
 * @param[in] tail A pointer to a view variable to be filled
 * @return The pointer passed to the function via @p tail
 */
cbuf_view_t *cbuf_tail(const volatile cbuf_t *cbuf, cbuf_view_t *tail);


/*! @brief Copy data from @p data into circular buffer memory represented by @p tail
 *
 * This function can be used to copy data into a circular buffer. The view in @p
 * tail must have been obtained with cbuf_tail.
 *
 * Thread-safe: yes
 * Running time: linear with @p len
 *
 * @param[in] tail A view into the empty space in the circular buffer
 * @param[in] data A pointer to the data to be copied
 * @param[in] len Number of bytes to be copied
 * @return The number of bytes copied (less than or equal to @p len )
 */
size_t cbuf_copy_in(const cbuf_view_t *tail, const void *data, size_t len);


/*! @brief Increase the number of bytes stored in @p cbuf by up to @p len bytes
 *
 * This function increases the number of data bytes in @p cbuf by up to @p len
 * bytes. If there is not enough space in the internal buffer for @p len
 * additional bytes, the length will be extended by the number of bytes that
 * fit. The application is responsible for copying the data into the circular
 * buffer before calling this function using cbuf_copy_in to copy data into the
 * memory buffer returned by cbuf_tail. The function returns the actual number
 * of bytes by which the circular buffer data was extended.
 *
 * Thread-safe: no Running time: constant
 *
 * @param[in] cbuf A pointer to the circular buffer
 * @param[in] len The desired number of bytes to extend the queue with
 * @return The number of bytes with which the queue was extended
 */
size_t cbuf_produce(volatile cbuf_t *cbuf, size_t len);


/*! @brief Put up to @p len bytes of @p data into @p cbuf
 *
 * Put up to @p len bytes from the memory buffer @p data to the circular buffer.
 * The data is appended at the end of any existing data. If there is not enough
 * space left for @p len bytes, a smaller number of bytes will be appended. The
 * function returns the number of appended bytes. The data from @p data is
 * copied into the internal buffer.
 *
 * Thread-safe: no
 * Running time: linear with @p size
 *
 * @param[in] cbuf A pointer to the circular buffer
 * @param[in] data A pointer to the source memory buffer
 * @param[in] len The number of bytes from @p data to be appended
 * @return The number of bytes appended (less than or equal to @p len )
 */
size_t cbuf_put(volatile cbuf_t *cbuf, const void *data, size_t len);


/*! @brief Return a view to the data stored in @p cbuf
 *
 * This function can be used to obtain a view into the data stored in the
 * circular buffer. The function returns the pointer passed to it via @p head .
 *
 * Thread-safe: no
 * Running time: constant
 *
 * @param[in] cbuf A pointer to the circular buffer
 * @param[out] head A pointer to the view data structure to be initialized
 * @return The pointer from @p head .
 */
cbuf_view_t *cbuf_head(const volatile cbuf_t *cbuf, cbuf_view_t *head);


/*! @brief Copy data from circular buffer memory represented by @p head into @p buffer
 *
 * This function can be used to copy data out from a circular buffer. The view in @p head
 * must have been obtained with cbuf_head.
 *
 * Thread-safe: yes
 * Running time: linear with @p len
 *
 * @param[in] buffer A pointer to the destination buffer
 * @param[in] head A view into the data in the circular buffer
 * @param[in] max_len Maximum number of bytes to be copied
 * @return The number of bytes copied (less than or equal to @p max_len )
 */
size_t cbuf_copy_out(void *buffer, const cbuf_view_t *head, size_t max_len);


/*! @brief Decrease the number of bytes stored in @p cbuf by up to @p len bytes
 *
 * This function decreases the number of bytes stored in @p cbuf by consuming up
 * to @p len bytes from the beginning of the circular buffer. If there is not
 * enough data in the circular buffer, the length is decreased by a smaller
 * number. The application is responsible for copying the data out of the
 * circular buffer using cbuf_head and cbuf_copy_out before calling this
 * function. The function returns the real number of bytes consumed from the
 * circular buffer.
 *
 * Thread-safe: no Running time: constant
 *
 * @param[in] cbuf A pointer to the circular buffer
 * @param[in] len The desired number of bytes to consume
 * @return The number of bytes consumed
 */
size_t cbuf_consume(volatile cbuf_t *cbuf, size_t len);


/*! @brief Get up to @p max_len bytes into @p buffer from @p cbuf
 *
 * This function retrieves up to @p max_len bytes of data from the beginning of
 * the circular buffer and copies the data into @p buffer. The buffer must be
 * large enough to hold @p max_len bytes. A smaller number of bytes may be
 * retrieved if there is not enough data in the circular buffer. The function
 * returns the number of bytes retrieved.
 *
 * Thread-safe: no
 * Running time: linear with @p size
 *
 * @param[in] cbuf A pointer to the circular buffer
 * @param[in] buffer A pointer to the destination memory buffer
 * @param[in] max_len The maximum number of bytes to be copied into @p buffer
 * @return The number of bytes copied (less than or equal to @p max_len )
 */
size_t cbuf_get(volatile cbuf_t *cbuf, void *buffer, size_t max_len);


#endif /* __CBUF_H__ */