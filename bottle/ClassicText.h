#pragma once
#include <stddef.h>
#include <stdint.h>

// Copy complete, valid UTF-8 code points only. Malformed input terminates the
// display name rather than emitting invalid JSON or splitting a multibyte name.
inline size_t classicNameCopy(char out[49], const uint8_t *src, size_t len) {
  size_t i = 0;
  while (src && i < len && i < 48 && src[i]) {
    const uint8_t c = src[i];
    const size_t n = c < 0x80 ? 1 : c >= 0xc2 && c <= 0xdf ? 2 :
                     c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
    if (!n || i + n > len || i + n > 48) break;
    bool valid = true;
    for (size_t j = 1; j < n; ++j) if ((src[i+j] & 0xc0) != 0x80) valid = false;
    if (n >= 3 && ((c == 0xe0 && src[i+1] < 0xa0) || (c == 0xed && src[i+1] >= 0xa0))) valid = false;
    if (n == 4 && ((c == 0xf0 && src[i+1] < 0x90) || (c == 0xf4 && src[i+1] >= 0x90))) valid = false;
    if (!valid) break;
    for (size_t j = 0; j < n; ++j) out[i+j] = static_cast<char>(src[i+j]);
    i += n;
  }
  out[i] = 0;
  return i;
}
