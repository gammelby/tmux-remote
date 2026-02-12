#include "nabtoshell_rolling_buffer.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Skip leading UTF-8 continuation bytes (10xxxxxx) so the buffer
 * never starts mid-character. Returns number of bytes to skip. */
static size_t skip_utf8_continuation(const char *data, size_t len)
{
    size_t skip = 0;
    while (skip < len && skip < 3 && ((uint8_t)data[skip] & 0xC0) == 0x80)
        skip++;
    return skip;
}

void nabtoshell_rolling_buffer_init(nabtoshell_rolling_buffer *b, size_t capacity)
{
    b->capacity = capacity;
    b->data = malloc(capacity);
    b->len = 0;
    b->total_appended = 0;
}

void nabtoshell_rolling_buffer_free(nabtoshell_rolling_buffer *b)
{
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->capacity = 0;
}

void nabtoshell_rolling_buffer_reset(nabtoshell_rolling_buffer *b)
{
    b->len = 0;
    b->total_appended = 0;
}

void nabtoshell_rolling_buffer_append(nabtoshell_rolling_buffer *b, const char *text, size_t len)
{
    if (len == 0) return;
    b->total_appended += len;

    if (len >= b->capacity) {
        // Text is larger than buffer: just keep the last capacity bytes
        memcpy(b->data, text + len - b->capacity, b->capacity);
        b->len = b->capacity;
        // Skip leading UTF-8 continuation bytes
        size_t skip = skip_utf8_continuation(b->data, b->len);
        if (skip > 0) {
            memmove(b->data, b->data + skip, b->len - skip);
            b->len -= skip;
        }
        return;
    }

    size_t new_len = b->len + len;
    if (new_len > b->capacity) {
        size_t excess = new_len - b->capacity;
        memmove(b->data, b->data + excess, b->len - excess);
        b->len -= excess;
        // Skip leading UTF-8 continuation bytes
        size_t skip = skip_utf8_continuation(b->data, b->len);
        if (skip > 0) {
            memmove(b->data, b->data + skip, b->len - skip);
            b->len -= skip;
        }
    }

    memcpy(b->data + b->len, text, len);
    b->len += len;
}

const char *nabtoshell_rolling_buffer_tail(const nabtoshell_rolling_buffer *b, size_t count, size_t *out_len)
{
    if (b->len <= count) {
        *out_len = b->len;
        return b->data;
    }
    *out_len = count;
    return b->data + b->len - count;
}
