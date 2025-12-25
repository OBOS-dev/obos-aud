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

typedef struct aud_stream {
    void* buffer;
    size_t ptr;
    pthread_mutex_t mut;   
} aud_stream;

void aud_stream_initialize(aud_stream* stream);
void aud_stream_push(aud_stream* stream, const void* data, size_t len);
void aud_stream_read(aud_stream* stream, void* data, size_t len);
/* does not lock the stream */
void aud_stream_mark_read(aud_stream* stream, size_t len);
void aud_stream_lock(aud_stream* stream);
void aud_stream_unlock(aud_stream* stream);
