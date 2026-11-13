#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "mbedtls/md5.h"

/* Isolate IDF's ROM MD5Context from esp-serial-flasher's context type. */
bool bytes_md5(const uint8_t *data, size_t len, uint8_t digest[16])
{
    return mbedtls_md5(data, len, digest) == 0;
}
