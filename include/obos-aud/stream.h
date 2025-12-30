/*
 * obos-aud/stream.h
 *
 * This file is a part of the obos-aud project.
 *
 * Copyright (c) 2025 Omar Berrow
 * SPDX License Identifier: MIT
 */

#pragma once

#include <pthread.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

enum {
    OBOS_AUD_STREAM_FLAGS_ULAW_DECODE = (1<<0),
    OBOS_AUD_STREAM_FLAGS_PCM32_DECODE = (1<<1),
    OBOS_AUD_STREAM_FLAGS_PCM24_DECODE = (1<<2),
    OBOS_AUD_STREAM_FLAGS_ALAW_DECODE = (1<<3),
    OBOS_AUD_STREAM_FLAGS_F32_DECODE = (1<<4),
    OBOS_AUD_STREAM_DECODE_MASK = 0x1f,
    OBOS_AUD_STREAM_VALID_FLAG_MASK = 0x1f,
};

typedef struct aud_stream {
    void* buffer;
    size_t ptr;
    size_t in_ptr;
    size_t size;
    pthread_mutex_t mut;
    pthread_cond_t write_event; /* only set when the stream is empty! */
    int sample_rate;
    int channels;
    float volume;
    uint32_t flags;
    struct mixer_output_device* dev;
} aud_stream;

void aud_stream_initialize(aud_stream* stream, int sample_rate, int dev_sample_rate, int channels);
/* decoded_data is only returned if blocking is true and the operation would block */
bool aud_stream_push(aud_stream* stream, const void* data, size_t len, bool blocking, const void** decoded_data, size_t* decoded_data_len);
bool aud_stream_push_no_decode(aud_stream* stream, const void* data, size_t len, bool blocking);
bool aud_stream_read(aud_stream* stream, void* data, size_t len, bool peek, bool blocking);

void aud_stream_lock(aud_stream* stream);
void aud_stream_unlock(aud_stream* stream);
