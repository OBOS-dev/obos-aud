/*
 * obos-aud/compiler.h
 *
 * This file is a part of the obos-aud project.
 *
 * Copyright (c) 2025 Omar Berrow
 * SPDX License Identifier: MIT
 */

#pragma once

#include <stdint.h>

#ifndef __GNUC__
#   error GNU C Compiler required for obos-aud headers
#endif

/* All fields are in little-endian unless otherwise specified. */

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define aud_ntoh8(x) (__builtin_bswap8(x))
#define aud_ntoh16(x) (__builtin_bswap16(x))
#define aud_ntoh32(x) (__builtin_bswap32(x))
#define aud_ntoh64(x) (__builtin_bswap64(x))
#elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define aud_ntoh8(x) (x)
#define aud_ntoh16(x) (x)
#define aud_ntoh32(x) (x)
#define aud_ntoh64(x) (x)
#else
#   error Invalid or unsupported byte order
#endif

#define aud_hton8(x) aud_ntoh8(x)
#define aud_hton16(x) aud_ntoh16(x)
#define aud_hton32(x) aud_ntoh32(x)
#define aud_hton64(x) aud_ntoh64(x)

#define PACK __attribute__((packed))
#define WEAK __attribute__((weak))

#define STRINGIFY_PROTO(x) #x
#define STRINGIFY(x) STRINGIFY_PROTO(x)

#if !defined(TEMP_FAILURE_RETRY) && _GNU_SOURCE
#   include <errno.h>
#   define TEMP_FAILURE_RETRY(expression) \
    ({\
        int result = 0;\
        do {\
            result = (int)(expression);\
        } while(result < 0 && errno == EINTR);\
        (result);\
    })
#endif