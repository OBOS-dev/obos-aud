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

// source: just trust me bro
static int16_t ulaw_decode_table[256] = {
    -32124, -31100, -30076, -29052, -28028, -27004, -25980, -24956,
    -23932, -22908, -21884, -20860, -19836, -18812, -17788, -16764,
    -15996, -15484, -14972, -14460, -13948, -13436, -12924, -12412,
    -11900, -11388, -10876, -10364,  -9852,  -9340,  -8828,  -8316,
     -7932,  -7676,  -7420,  -7164,  -6908,  -6652,  -6396,  -6140,
     -5884,  -5628,  -5372,  -5116,  -4860,  -4604,  -4348,  -4092,
     -3900,  -3772,  -3644,  -3516,  -3388,  -3260,  -3132,  -3004,
     -2876,  -2748,  -2620,  -2492,  -2364,  -2236,  -2108,  -1980,
     -1884,  -1820,  -1756,  -1692,  -1628,  -1564,  -1500,  -1436,
     -1372,  -1308,  -1244,  -1180,  -1116,  -1052,   -988,   -924,
      -876,   -844,   -812,   -780,   -748,   -716,   -684,   -652,
      -620,   -588,   -556,   -524,   -492,   -460,   -428,   -396,
      -372,   -356,   -340,   -324,   -308,   -292,   -276,   -260,
      -244,   -228,   -212,   -196,   -180,   -164,   -148,   -132,
      -120,   -112,   -104,    -96,    -88,    -80,    -72,    -64,
       -56,    -48,    -40,    -32,    -24,    -16,     -8,      0,
     32124,  31100,  30076,  29052,  28028,  27004,  25980,  24956,
     23932,  22908,  21884,  20860,  19836,  18812,  17788,  16764,
     15996,  15484,  14972,  14460,  13948,  13436,  12924,  12412,
     11900,  11388,  10876,  10364,   9852,   9340,   8828,   8316,
      7932,   7676,   7420,   7164,   6908,   6652,   6396,   6140,
      5884,   5628,   5372,   5116,   4860,   4604,   4348,   4092,
      3900,   3772,   3644,   3516,   3388,   3260,   3132,   3004,
      2876,   2748,   2620,   2492,   2364,   2236,   2108,   1980,
      1884,   1820,   1756,   1692,   1628,   1564,   1500,   1436,
      1372,   1308,   1244,   1180,   1116,   1052,    988,    924,
       876,    844,    812,    780,    748,    716,    684,    652,
       620,    588,    556,    524,    492,    460,    428,    396,
       372,    356,    340,    324,    308,    292,    276,    260,
       244,    228,    212,    196,    180,    164,    148,    132,
       120,    112,    104,     96,     88,     80,     72,     64,
        56,     48,     40,     32,     24,     16,      8,      0,
};

void aud_stream_initialize(aud_stream* stream, int sample_rate, int channels)
{
    stream->mut = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
    stream->sample_rate = sample_rate;
    stream->channels = channels;
    stream->size = sizeof(int16_t)*sample_rate*channels;
    stream->buffer = malloc(stream->size);
}

void aud_stream_push_no_decode(aud_stream* stream, const void* data, size_t len)
{
    if (len > (stream->size - stream->ptr))
    {
        while (stream->ptr > 0)
            sched_yield();
        size_t initial_len = len;
        while (len)
        {
            size_t nToWrite = len > stream->size ? stream->size : len;
            aud_stream_push(stream, (const char*)data + (initial_len-len), nToWrite);
            len -= nToWrite;
        }
        return;
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

void aud_stream_push(aud_stream* stream, const void* buf, size_t len)
{
    if (~stream->flags & OBOS_AUD_STREAM_FLAGS_ULAW_DECODE)
        return aud_stream_push_no_decode(stream, buf, len); // lol returning from a void function
    const int8_t* data = buf;
    int16_t* decoded = malloc(len*sizeof(int16_t));
    for (size_t i = 0; i < len; i++)
        decoded[i] = ulaw_decode_table[data[i]];
    aud_stream_push_no_decode(stream, decoded, len*sizeof(int16_t));
    free(decoded);
}

bool aud_stream_read(aud_stream* stream, void* data, size_t len, bool peek, bool blocking)
{
    up:
    if (blocking)
    {
        while ((stream->ptr - stream->in_ptr) < len)
            sched_yield();
    }
    else
    {
        if ((stream->ptr - stream->in_ptr) < len)
            return false;
    }
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
    return true;
}

void aud_stream_lock(aud_stream* stream)
{
    pthread_mutex_lock(&stream->mut);
}

void aud_stream_unlock(aud_stream* stream)
{
    pthread_mutex_unlock(&stream->mut);
}
