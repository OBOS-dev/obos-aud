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
#include <math.h>

#include <sys/param.h>

#include <obos-aud/stream.h>
#include <obos-aud/priv/mixer.h>

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
// source: bro i said just trust me..
static int16_t alaw_decode_table[256] = {
     -5504,      -5248,      -6016,      -5760,      -4480,      -4224,      -4992,      -4736,
     -7552,      -7296,      -8064,      -7808,      -6528,      -6272,      -7040,      -6784,
     -2752,      -2624,      -3008,      -2880,      -2240,      -2112,      -2496,      -2368,
     -3776,      -3648,      -4032,      -3904,      -3264,      -3136,      -3520,      -3392,
    -22016,     -20992,     -24064,     -23040,     -17920,     -16896,     -19968,     -18944,
    -30208,     -29184,     -32256,     -31232,     -26112,     -25088,     -28160,     -27136,
    -11008,     -10496,     -12032,     -11520,      -8960,      -8448,      -9984,      -9472,
    -15104,     -14592,     -16128,     -15616,     -13056,     -12544,     -14080,     -13568,
      -344,       -328,       -376,       -360,       -280,       -264,       -312,       -296,
      -472,       -456,       -504,       -488,       -408,       -392,       -440,       -424,
       -88,        -72,       -120,       -104,        -24,         -8,        -56,        -40,
      -216,       -200,       -248,       -232,       -152,       -136,       -184,       -168,
     -1376,      -1312,      -1504,      -1440,      -1120,      -1056,      -1248,      -1184,
     -1888,      -1824,      -2016,      -1952,      -1632,      -1568,      -1760,      -1696,
      -688,       -656,       -752,       -720,       -560,       -528,       -624,       -592,
      -944,       -912,      -1008,       -976,       -816,       -784,       -880,       -848,
      5504,       5248,       6016,       5760,       4480,       4224,       4992,       4736,
      7552,       7296,       8064,       7808,       6528,       6272,       7040,       6784,
      2752,       2624,       3008,       2880,       2240,       2112,       2496,       2368,
      3776,       3648,       4032,       3904,       3264,       3136,       3520,       3392,
     22016,      20992,      24064,      23040,      17920,      16896,      19968,      18944,
     30208,      29184,      32256,      31232,      26112,      25088,      28160,      27136,
     11008,      10496,      12032,      11520,       8960,       8448,       9984,       9472,
     15104,      14592,      16128,      15616,      13056,      12544,      14080,      13568,
       344,        328,        376,        360,        280,        264,        312,        296,
       472,        456,        504,        488,        408,        392,        440,        424,
        88,         72,        120,        104,         24,          8,         56,         40,
       216,        200,        248,        232,        152,        136,        184,        168,
      1376,       1312,       1504,       1440,       1120,       1056,       1248,       1184,
      1888,       1824,       2016,       1952,       1632,       1568,       1760,       1696,
       688,        656,        752,        720,        560,        528,        624,        592,
       944,        912,       1008,        976,        816,        784,        880,        848,
};


void aud_stream_initialize(aud_stream* stream, int sample_rate, int dev_sample_rate, int channels)
{
    stream->mut = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
    stream->sample_rate = sample_rate;
    stream->channels = channels;
    stream->size = sizeof(int16_t)*dev_sample_rate*channels;
    stream->buffer = malloc(stream->size);
}

void aud_stream_push_no_decode(aud_stream* stream, const void* data, size_t len)
{
    while (stream->ptr > 0)
        sched_yield();
    if (len > (stream->size - stream->ptr))
    {
        size_t initial_len = len;
        while (len)
        {
            size_t nToWrite = len > stream->size ? stream->size : len;
            aud_stream_push_no_decode(stream, (const char*)data + (initial_len-len), nToWrite);
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

static int16_t read_sample(const int16_t* buffer, int channel, int channels, size_t buffer_len, float idx)
{
    return buffer[((int)idx)*channels + channel];
}

static float clamp(float value, float min, float max)
{
    return value < min ? min : ((value > max) ? max : value);
}

void aud_stream_push(aud_stream* stream, const void* buf, size_t len)
{
    if (((stream->flags & OBOS_AUD_STREAM_DECODE_MASK) == 0) && stream->dev->sample_rate == stream->sample_rate)
        return aud_stream_push_no_decode(stream, buf, len); // lol returning from a void function
    size_t newlen = len;
    if (stream->flags & OBOS_AUD_STREAM_FLAGS_ULAW_DECODE)
        newlen /=  (8.f/16.f);
    else if (stream->flags & OBOS_AUD_STREAM_FLAGS_ALAW_DECODE)
        newlen /=  (8.f/16.f);
    else if (stream->flags & OBOS_AUD_STREAM_FLAGS_PCM32_DECODE)
        newlen /= (32.f/16.f);
    else if (stream->flags & OBOS_AUD_STREAM_FLAGS_PCM24_DECODE)
        newlen /= (24.f/16.f);
    else if (stream->flags & OBOS_AUD_STREAM_FLAGS_F32_DECODE)
        newlen /= (32.f/16.f);
    int16_t* decoded = malloc(newlen);
    if (stream->flags & OBOS_AUD_STREAM_FLAGS_ULAW_DECODE)
    {
        const int8_t* data = buf;
        for (size_t i = 0; i < (len / sizeof(*data)); i++)
            decoded[i] = ulaw_decode_table[data[i]];
    }
    else if (stream->flags & OBOS_AUD_STREAM_FLAGS_ALAW_DECODE)
    {
        const int8_t* data = buf;
        for (size_t i = 0; i < (len / sizeof(*data)); i++)
            decoded[i] = alaw_decode_table[data[i]];
    }
    else if (stream->flags & OBOS_AUD_STREAM_FLAGS_PCM32_DECODE)
    {
        const int32_t* data = buf;

        for (size_t i = 0; i < (len / sizeof(*data)); i++)
            decoded[i] = (int16_t)(data[i] >> 16);
    }
    else if (stream->flags & OBOS_AUD_STREAM_FLAGS_PCM24_DECODE)
    {
        struct int24
        {
            int32_t data : 24;
        } PACK;

        const struct int24* data = buf;

        for (size_t i = 0; i < (len / sizeof(*data)); i++)
            decoded[i] = (int16_t)(data[i].data >> 8);
    }
    else if (stream->flags & OBOS_AUD_STREAM_FLAGS_F32_DECODE)
    {
        const float* data = buf;

        for (size_t i = 0; i < (len / sizeof(*data)); i++)
            decoded[i] = (int16_t)(clamp(data[i], -1, 1) * 32767);
    }
    else
        memcpy(decoded, buf, len);
    if (stream->dev->sample_rate != stream->sample_rate)
    {
        float isamples_per_osample = (float)stream->sample_rate / (float)stream->dev->sample_rate;
        float sample_count = floorf((float)newlen / (float)stream->channels / (float)sizeof(int16_t));
        float new_sample_count = ceilf(sample_count / isamples_per_osample);
        size_t new_buf_len = new_sample_count * stream->channels * sizeof(int16_t);
        int16_t* new_buf = malloc(new_buf_len);
        for (int channel = 0; channel < stream->channels; channel++)
        {
            for (float i = 0; (i / isamples_per_osample) < new_sample_count; i += isamples_per_osample)
            {
                int16_t final_sample = 0;
                if (isamples_per_osample < 1)
                    final_sample = read_sample(decoded, channel, stream->channels, newlen, i);
                else
                {
                    float osamples_per_isample = 1/isamples_per_osample;
                    for (float j = 0; j < osamples_per_isample; j++)
                        final_sample += read_sample(decoded, channel, stream->channels, newlen, i+j);
                }
                new_buf[(int)(i / isamples_per_osample)*stream->channels + channel] = final_sample;
            }
        }
        free(decoded);
        decoded = new_buf;
        newlen = new_buf_len;
    }
    len = newlen;
    aud_stream_push_no_decode(stream, decoded, len);
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
