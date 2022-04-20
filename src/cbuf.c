#include "cbuf.h"
#include <string.h>


static inline size_t min(size_t a, size_t b)
{
    return a < b ? a : b;
}


static inline size_t space_left(volatile const cbuf_t *c)
{
    return c->max_length - c->length;
}


void cbuf_init(volatile cbuf_t *c, void *buffer, size_t size)
{
    c->buffer = buffer;
    c->max_length = size;
    c->length = 0;
    c->read = 0;
    c->write = 0;
}


cbuf_view_t *cbuf_tail(const volatile cbuf_t *c, cbuf_view_t *v)
{
    size_t l = space_left(c);
    v->ptr[0] = c->buffer + c->write;
    v->len[0] = min(c->max_length - c->write, l);
    v->ptr[1] = c->buffer;
    v->len[1] = l - v->len[0];
    return v;
}


size_t cbuf_copy_in(const cbuf_view_t *v, const void *data, size_t len)
{
    len = min(len, v->len[0] + v->len[1]);

    size_t a = min(len, v->len[0]);
    size_t b = len - a;

    memcpy(v->ptr[0], data, a);
    memcpy(v->ptr[1], (char *)data + a, b);
    return a + b;
}


size_t cbuf_produce(volatile cbuf_t *c, size_t len)
{
    len = min(len, space_left(c));
    c->write = (c->write + len) % c->max_length;
    c->length += len;
    return len;
}


size_t cbuf_put(volatile cbuf_t *c, const void *data, size_t len)
{
    cbuf_view_t t;
    return cbuf_produce(c, cbuf_copy_in(cbuf_tail(c, &t), data, len));
}


cbuf_view_t *cbuf_head(const volatile cbuf_t *c, cbuf_view_t *h)
{
    h->ptr[0] = c->buffer + c->read;
    h->len[0] = min(c->max_length - c->read, c->length);
    h->ptr[1] = c->buffer;
    h->len[1] = c->length - h->len[0];
    return h;
}


size_t cbuf_copy_out(void *buffer, const cbuf_view_t *v, size_t max_len)
{
    size_t len = min(max_len, v->len[0] + v->len[1]);

    size_t a = min(len, v->len[0]);
    size_t b = len - a;

    memcpy(buffer, v->ptr[0], a);
    memcpy((char *)buffer + a, v->ptr[1], b);
    return a + b;
}


size_t cbuf_consume(volatile cbuf_t *c, size_t len)
{
    len = min(len, c->length);
    c->read = (c->read + len) % c->max_length;
    c->length -= len;
    return len;
}


size_t cbuf_get(volatile cbuf_t *c, void *buffer, size_t max_len)
{
    cbuf_view_t h;
    return cbuf_consume(c, cbuf_copy_out(buffer, cbuf_head(c, &h), max_len));
}
