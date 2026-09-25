/* Internal SHA-256 and SHA-512. */
#ifndef CITADEL_SHA2_H
#define CITADEL_SHA2_H
#include <stddef.h>
#include <stdint.h>
#include "internal.h"

struct sha256 { uint32_t h[8]; uint64_t len; unsigned char buf[64]; };
struct sha512 { uint64_t h[8]; uint64_t len; unsigned char buf[128]; };

hidden void __sha256_init(struct sha256 *);
hidden void __sha256_update(struct sha256 *, const void *, size_t);
hidden void __sha256_final(struct sha256 *, unsigned char out[32]);
hidden void __sha512_init(struct sha512 *);
hidden void __sha512_update(struct sha512 *, const void *, size_t);
hidden void __sha512_final(struct sha512 *, unsigned char out[64]);
#endif
