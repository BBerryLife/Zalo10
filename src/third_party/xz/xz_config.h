/* SPDX-License-Identifier: 0BSD */
/*
 * Cấu hình XZ Embedded (https://github.com/tukaani-project/xz-embedded, Lasse Collin, 0BSD)
 * cho Zalo10: chỉ cần giải nén stream .xz (LZMA2) của file đồng bộ tin nhắn từ điện thoại.
 * Không dùng BCJ filter; bật CRC64 (check mặc định của xz) và chấp nhận mọi loại check khác.
 */
#ifndef XZ_CONFIG_H
#define XZ_CONFIG_H

#define XZ_USE_CRC64
#define XZ_DEC_ANY_CHECK

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "xz.h"

#define kmalloc(size, flags) malloc(size)
#define kfree(ptr) free(ptr)
#define vmalloc(size) malloc(size)
#define vfree(ptr) free(ptr)

#define memeq(a, b, size) (memcmp(a, b, size) == 0)
#define memzero(buf, size) memset(buf, 0, size)

#ifndef min
#	define min(x, y) ((x) < (y) ? (x) : (y))
#endif
#define min_t(type, x, y) min(x, y)

#ifndef fallthrough
#	define fallthrough do {} while (0)
#endif

#ifndef __always_inline
#	ifdef __GNUC__
#		define __always_inline inline __attribute__((__always_inline__))
#	else
#		define __always_inline inline
#	endif
#endif

#ifndef get_unaligned_le32
static inline uint32_t get_unaligned_le32(const uint8_t *buf)
{
	return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}
#endif
#ifndef get_unaligned_be32
static inline uint32_t get_unaligned_be32(const uint8_t *buf)
{
	return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) | ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
}
#endif
#ifndef put_unaligned_le32
static inline void put_unaligned_le32(uint32_t val, uint8_t *buf)
{
	buf[0] = (uint8_t)val; buf[1] = (uint8_t)(val >> 8); buf[2] = (uint8_t)(val >> 16); buf[3] = (uint8_t)(val >> 24);
}
#endif
#ifndef put_unaligned_be32
static inline void put_unaligned_be32(uint32_t val, uint8_t *buf)
{
	buf[0] = (uint8_t)(val >> 24); buf[1] = (uint8_t)(val >> 16); buf[2] = (uint8_t)(val >> 8); buf[3] = (uint8_t)val;
}
#endif
#ifndef get_le32
#	define get_le32 get_unaligned_le32
#endif
#ifndef get_be32
#	define get_be32 get_unaligned_be32
#endif

#endif
