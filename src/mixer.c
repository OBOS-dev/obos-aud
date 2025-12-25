/*
 * src/mixer.c
 *
 * Copyright (c) 2025 Omar Berrow
 */

#include <obos-aud/priv/mixer.h>
#include <obos-aud/priv/backend.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

mixer_output_device* g_outputs;
size_t g_output_count;
mixer_output_device* g_default_output;

void mixer_initialize()
{
    if (!aud_backend_initialize)
    {
        fprintf(stderr, "No backend compiled!\n");
        abort();
    }
    if (aud_backend_initialize() < 0)
    {
        fprintf(stderr, "aud_backend_initialize failed! errno=%d\n", errno);
        abort();
    }

    g_output_count = aud_backend_get_outputs(NULL, 0);
    g_outputs = calloc(g_output_count, sizeof(*g_outputs));
    aud_output_dev *devs = calloc(g_output_count, sizeof(*g_outputs));
    aud_backend_get_outputs(devs, g_output_count);
    for (size_t i = 0; i < g_output_count; i++)
        memcpy(&g_outputs[i].info, &devs[i], sizeof(devs[i]));
    free(devs);

    if (!g_output_count)
    {
        fprintf(stderr, "No outputs! Abort.\n");
        abort();
    }

    for (size_t i = 0; i < g_output_count; i++)
        mixer_output_initialize(&g_outputs[i]);

    // Choose default output.
    for (size_t i = 0; i < g_output_count; i++)
    {
        if (g_outputs[i].info.type == OBOS_AUD_OUTPUT_TYPE_SPEAKER && (!g_default_output || g_default_output->info.type != OBOS_AUD_OUTPUT_TYPE_SPEAKER))
        {
            g_default_output = &g_outputs[i];
            break;
        }
        if (g_outputs[i].info.type == OBOS_AUD_OUTPUT_TYPE_HEADPHONE && !g_default_output)
        {
            g_default_output = &g_outputs[i];
            break;
        }
    }
    if (!g_default_output)
        g_default_output = &g_outputs[0];
    g_default_output->info.flags |= OBOS_AUD_OUTPUT_FLAGS_DEFAULT;
    printf("Chose output %ld as default.\n", g_default_output - g_outputs);
}

static bool settings_match(int output_id, int sample_rate, int channels, int format_size)
{
    int real_sample_rate = 0;
    int real_channels = 0;
    int real_format_size = 0;
    aud_backend_query_output_params(output_id, &real_sample_rate, &real_channels, &real_format_size);
    return real_sample_rate == sample_rate && 
           real_channels == channels && 
           real_format_size == format_size;
}

void mixer_output_initialize(mixer_output_device* dev)
{
    dev->streams.lock = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
    int sample_rates[8] = {
        96000,
        88200,
        48000,
        44100,
        22050,
        16000,
        11025,
         8000,
    };
    for (int i = 0; i < sizeof(sample_rates)/sizeof(*sample_rates); i++)
    {
        int channels = 2;
        int sample_rate = sample_rates[i];

        up:
        aud_backend_configure_output(dev->info.output_id, sample_rate, channels, 16);
        if (!settings_match(dev->info.output_id, sample_rate, channels, 16))
        {
            if (channels == 1)
                continue;
            channels = 1;
            goto up;
        }
        dev->channels = channels;
        dev->sample_rate = sample_rate;
        break;
    }
    printf("Configured output device #%ld with %d channel%c at a sample rate of %dhz\n", 
        dev-g_outputs,
        dev->channels,
        dev->channels == 1 ? '\0' : 's',
        dev->sample_rate
    );
}

mixer_output_device* mixer_output_from_id(int output_id)
{
    if (output_id == OBOS_AUD_DEFAULT_OUTPUT_DEV)
        return g_default_output;
    for (size_t i = 0; i < g_output_count; i++)
        if (output_id == g_outputs[i].info.output_id)
            return &g_outputs[i];
    return NULL;
}

aud_stream_node* mixer_output_add_stream_dev(mixer_output_device* dev)
{
    if (!dev)
        return NULL;
    aud_stream_node* node = calloc(1, sizeof(*node));
    pthread_mutex_lock(&dev->streams.lock);
    if (!dev->streams.head)
        dev->streams.head = node;
    if (dev->streams.tail)
        dev->streams.tail->next = node;
    node->prev = dev->streams.tail;
    dev->streams.tail = node;
    dev->streams.nNodes++;
    pthread_mutex_unlock(&dev->streams.lock);
    return node;
}

void mixer_output_remove_stream_dev(mixer_output_device* dev, aud_stream_node* stream)
{
    if (!dev)
        return;
    pthread_mutex_lock(&dev->streams.lock);
    if (stream->next)
        stream->next->prev = stream->prev;
    if (stream->prev)
        stream->prev->next = stream->next;
    if (dev->streams.head == stream)
        dev->streams.head = stream->next;
    if (dev->streams.tail == stream)
        dev->streams.tail = stream->prev;
    dev->streams.nNodes--;
    pthread_mutex_unlock(&dev->streams.lock);
}

aud_stream_node* mixer_output_add_stream(int output_id)
{
    mixer_output_add_stream_dev(mixer_output_from_id(output_id));
}

void mixer_output_remove_stream(int output_id, aud_stream_node* stream)
{
    mixer_output_remove_stream_dev(mixer_output_from_id(output_id), stream);
}

void mixer_output_set_default(int output_id)
{
    mixer_output_device* dev = mixer_output_from_id(output_id);
    if (!dev)
        return;
    dev->info.flags |= OBOS_AUD_OUTPUT_FLAGS_DEFAULT;
    g_default_output = dev;
}