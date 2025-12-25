/*
 * libaustream/stream.c
 *
 * This file is a part of the obos-aud project.
 *
 * Copyright (c) 2025 Omar Berrow
 * SPDX License Identifier: MIT
 */

#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <obos-aud/stream.h>

void aud_stream_initialize(aud_stream* stream)
{
    stream->mut = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
}

void aud_stream_push(aud_stream* stream, const void* data, size_t len)
{
    aud_stream_lock(stream);
    stream->buffer = realloc(stream->buffer, stream->ptr += len);
    memcpy(((char*)stream->buffer) + stream->ptr-len, data, len);
    aud_stream_unlock(stream);
}

void aud_stream_read(aud_stream* stream, void* data, size_t len)
{
    aud_stream_lock(stream);
    memcpy(stream->buffer, data, len);
    aud_stream_mark_read(stream, len);
    aud_stream_unlock(stream);
}

void aud_stream_mark_read(aud_stream* stream, size_t len)
{
    stream->ptr -= len;
    void* buf = malloc(stream->ptr);
    memcpy(buf, ((char*)stream->buffer) + len, stream->ptr);
    free(stream->buffer);
    stream->buffer = buf;
}

void aud_stream_lock(aud_stream* stream)
{
    pthread_mutex_lock(&stream->mut);
}

void aud_stream_unlock(aud_stream* stream)
{
    pthread_mutex_unlock(&stream->mut);
}
