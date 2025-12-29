/*
 * src/mixer.c
 *
 * Copyright (c) 2025 Omar Berrow
 */

#include <obos-aud/priv/mixer.h>
#include <obos-aud/priv/backend.h>
#include <obos-aud/priv/con.h>

#include <obos-aud/stream.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <math.h>

mixer_output_device* g_outputs;
size_t g_output_count;
mixer_output_device* g_default_output;

static float normalize(float input, float min, float max)
{
    float average = (min + max) / 2.0f;
    float half_range = (max - min) / 2.0f;
    assert(half_range);
    return (input - average) / half_range;
}

static float unnormalize(float input, float min, float max)
{
    float average = (min + max) / 2.0f;
    float half_range = (max - min) / 2.0f;
    assert(half_range);
    return input * half_range + average;
}

static float normalize_pos(float input, float min, float max)
{
    assert(max - min);
    return (input - min) / (max - min);
}

static float unnormalize_pos(float input, float min, float max)
{
    assert(max - min);
    return (input+min) * (max-min);
}

static float clamp(float value, float min, float max)
{
    return value < min ? min : ((value > max) ? max : value);
}

static void* mixer_worker(void* arg);

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
        if (g_outputs[i].info.type == OBOS_AUD_OUTPUT_TYPE_LINE_OUT && (!g_default_output || g_default_output->info.type != OBOS_AUD_OUTPUT_TYPE_SPEAKER))
            g_default_output = &g_outputs[i];
        if (g_outputs[i].info.type == OBOS_AUD_OUTPUT_TYPE_HEADPHONE && !g_default_output)
            g_default_output = &g_outputs[i];
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
    dev->streams.evnt = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
    int sample_rates[8] = {
        44100,
        22050,
        88200,
        96000,
        48000,
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
        dev->format_size = 16;
        break;
    }
    mixer_output_set_volume(dev, 100);
    
    pthread_create(&dev->worker, NULL, mixer_worker, dev);

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

aud_stream_node* mixer_output_add_stream_dev(mixer_output_device* dev, int sample_rate, int channels, float volume, struct obos_aud_connection* owner)
{
    if (!dev)
        return NULL;
    aud_stream_node* node = calloc(1, sizeof(*node));
    pthread_mutex_lock(&dev->streams.lock);
    aud_stream_initialize(&node->data, sample_rate, dev->sample_rate, channels);
    node->data.dev = dev;
    node->data.volume = mixer_normalize_volume(volume);
    node->owner = owner;
    if (!dev->streams.head)
        dev->streams.head = node;
    if (dev->streams.tail)
        dev->streams.tail->next = node;
    node->prev = dev->streams.tail;
    dev->streams.tail = node;
    dev->input_channels += channels;
    if (!dev->streams.nNodes)
        pthread_cond_signal(&dev->streams.evnt);
    dev->streams.nNodes++;
    pthread_mutex_unlock(&dev->streams.lock);
    return node;
}

void mixer_output_remove_stream_dev(mixer_output_device* dev, aud_stream_node* stream)
{
    pthread_mutex_lock(&dev->streams.lock);
    mixer_output_remove_stream_dev_unlocked(dev, stream);
    pthread_mutex_unlock(&dev->streams.lock);
}
void mixer_output_remove_stream_dev_unlocked(mixer_output_device* dev, aud_stream_node* stream)
{
    if (!dev)
        return;
    if (stream->next)
        stream->next->prev = stream->prev;
    if (stream->prev)
        stream->prev->next = stream->next;
    if (dev->streams.head == stream)
        dev->streams.head = stream->next;
    if (dev->streams.tail == stream)
        dev->streams.tail = stream->prev;
    dev->streams.nNodes--;
    if (stream->input_samples_arr)
        free(stream->input_samples_arr);
    dev->input_channels -= stream->data.channels;
    free(stream->data.buffer);
    free(stream);
}

aud_stream_node* mixer_output_add_stream(int output_id, int sample_rate, int channels, float volume, struct obos_aud_connection* owner)
{
    return mixer_output_add_stream_dev(mixer_output_from_id(output_id), sample_rate, channels, volume, owner);
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

float mixer_normalize_volume(float volume)
{
    return normalize_pos(volume, 0, 100);
}
float mixer_get_volume(float volume)
{
    return unnormalize_pos(volume, 0, 100);
}

void mixer_output_set_volume(mixer_output_device* dev, float volume)
{
    dev->volume = mixer_normalize_volume(volume);
}
float mixer_output_get_volume(mixer_output_device* dev)
{
    return mixer_get_volume(dev->volume);
}

float mixer_output_get_volume(mixer_output_device* dev);

#ifdef __obos__
#   include <obos/syscall.h>
#endif

static void* mixer_worker(void* arg)
{
#ifdef __obos__
    uint32_t prio = 4; // URGENT
    syscall3(Sys_ThreadPriority, HANDLE_CURRENT, &prio, NULL);
#endif
    mixer_output_device* dev = arg;
    size_t buffer_len = dev->sample_rate * dev->channels * sizeof(uint16_t);
    uint16_t* buffer = malloc(buffer_len);
    memset(buffer, 0x00, buffer_len);
    float* condensed_samples = calloc(dev->channels, sizeof(float));
    int input_channels = dev->input_channels;
    float *samples = calloc(input_channels, sizeof(float));
    while (1)
    {
        pthread_mutex_lock(&dev->streams.lock);
        if (!dev->streams.nNodes)
        {
            printf("mixer: idling output device\n");
            aud_backend_output_play(dev->info.output_id, false);
            free(buffer);
            pthread_cond_wait(&dev->streams.evnt, &dev->streams.lock);
            buffer = malloc(buffer_len);
            memset(buffer, 0x00, buffer_len);
        }
        pthread_mutex_unlock(&dev->streams.lock);
        aud_backend_output_play(dev->info.output_id, true);
        // struct timespec start = {};
        // clock_gettime(1, &start);
        for (int i = 0; i < dev->sample_rate && dev->input_channels; i++)
        {
            pthread_mutex_lock(&dev->streams.lock);
            int j = 0;
            if (dev->input_channels != input_channels)
            {
                input_channels = dev->input_channels;
                if (samples)
                    free(samples);
                samples = calloc(input_channels, sizeof(float));
            }
            else
                memset(samples, 0, sizeof(*samples)*input_channels);
            for (aud_stream_node* node = dev->streams.head; node; node = node->next)
            {
                up:
                (void)0;
                if (!node)
                    break;
                aud_stream* const stream = &node->data;
                aud_stream_lock(stream);
                float volume = stream->volume * node->owner->volume * dev->volume;
                if (!(stream->ptr - stream->in_ptr))
                {
                    for (int i = 0; i < stream->channels; i++)
                        samples[j++] = normalize(0, -0x10000, 0x10000);
                    aud_stream_unlock(stream);
                    continue;
                }
                aud_stream_unlock(stream);
                int16_t *i_samples = node->input_samples_arr ?
                    node->input_samples_arr :
                    (node->input_samples_arr = calloc(stream->channels, sizeof(int16_t)));
                aud_stream_read(stream, i_samples, stream->channels*sizeof(int16_t), false, false);
                for (int i = 0; i < stream->channels; i++)
                    samples[j++] = normalize(i_samples[i], -0x10000, 0x10000) * volume;
                if (node->dead && !stream->ptr)
                {
                    aud_stream_node* next = node->next;
                    mixer_output_remove_stream_dev_unlocked(dev, node);
                    node = next;
                    goto up;
                }
            }
            if (input_channels <= dev->channels)
                for (int c = 0; c < dev->channels && input_channels != 0; c++)
                    buffer[i*dev->channels+c] = (int16_t)unnormalize(samples[c % input_channels], -0x10000, 0x10000);
            else
            {
                int samples_per_channel = input_channels / dev->channels;
                int extra_samples = input_channels % dev->channels;
                int additional_samples_per_channel = extra_samples / dev->channels;
                if (!additional_samples_per_channel)
                    additional_samples_per_channel = 1;
                int sample_idx = 0;
                for (int c = 0; c < dev->channels; c++)
                {
                    size_t samples_this_channel = samples_per_channel;
                    if (extra_samples)
                    {
                        extra_samples -= additional_samples_per_channel;
                        samples_per_channel += additional_samples_per_channel;
                    }
                    for (int idx = 0; idx < samples_this_channel; idx++)
                        condensed_samples[c] += samples[sample_idx++];
                    condensed_samples[c] /= samples_this_channel;
                    
                    condensed_samples[c] = clamp(condensed_samples[c], -1, 1);
                }
                for (int c = 0; c < dev->channels; c++)
                    buffer[i*dev->channels+c] = unnormalize(condensed_samples[c], -0x10000, 0x10000);
            }
            pthread_mutex_unlock(&dev->streams.lock);
        }
        // struct timespec end = {};
        // clock_gettime(1, &end);
        // double frame_time = (end.tv_nsec - start.tv_nsec) / 1000000000.0;
        aud_backend_queue_data(dev->info.output_id, buffer, buffer_len);
        memset(buffer, 0x00, buffer_len);
    }
    free(samples);
    return NULL;
}