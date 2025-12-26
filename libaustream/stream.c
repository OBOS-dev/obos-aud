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
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include <sys/param.h>

#include <obos-aud/stream.h>

void aud_stream_initialize(aud_stream* stream, int sample_rate, int channels)
{
    stream->mut = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
    stream->sample_rate = sample_rate;
    stream->channels = channels;
    stream->size = sizeof(int16_t)*sample_rate*channels;
    stream->buffer = malloc(stream->size);
}

void aud_stream_push(aud_stream* stream, const void* data, size_t len)
{
    if (len > stream->size)
    {
        while (len)
        {
            aud_stream_push(stream, data, (len % stream->size) == 0 ? stream->size : len % stream->size);
            len -= MIN(len, stream->size);
        }
    }
    up:
    while (stream->ptr == stream->size)
        sched_yield();
    if (pthread_mutex_trylock(&stream->mut) == EBUSY)
        goto up;
    memcpy((char*)stream->buffer + stream->ptr, data, len);
    stream->ptr += len;
    pthread_mutex_unlock(&stream->mut);
}

void aud_stream_read(aud_stream* stream, void* data, size_t len, bool peek)
{
    up:
    while ((stream->ptr - stream->in_ptr) < len)
        sched_yield();
    if (pthread_mutex_trylock(&stream->mut) == EBUSY)
        goto up;
    if (data)
        memcpy(data, (char*)stream->buffer + stream->in_ptr, len);
    if (!peek)
    {
        stream->in_ptr += len;
        if (stream->in_ptr == stream->ptr)
            stream->in_ptr = stream->ptr = 0;
    }
    aud_stream_unlock(stream);
}

void aud_stream_lock(aud_stream* stream)
{
    pthread_mutex_lock(&stream->mut);
}

void aud_stream_unlock(aud_stream* stream)
{
    pthread_mutex_unlock(&stream->mut);
}
