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
    OBOS_AUD_STREAM_VALID_FLAG_MASK = (1<<0),
};

typedef struct aud_stream {
    void* buffer;
    size_t ptr;
    size_t in_ptr;
    size_t size;
    pthread_mutex_t mut;
    int sample_rate;
    int channels;
    float volume;
    uint32_t flags;
} aud_stream;

void aud_stream_initialize(aud_stream* stream, int sample_rate, int channels);
void aud_stream_push(aud_stream* stream, const void* data, size_t len);
void aud_stream_push_no_decode(aud_stream* stream, const void* data, size_t len);
bool aud_stream_read(aud_stream* stream, void* data, size_t len, bool peek, bool blocking);

void aud_stream_lock(aud_stream* stream);
void aud_stream_unlock(aud_stream* stream);
