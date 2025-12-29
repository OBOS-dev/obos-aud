/*
 * src/backends/obos-hda/impl.c
 *
 * Copyright (c) 2025 Omar Berrow
 */

#define _GNU_SOURCE 1

#include <obos-aud/compiler.h>

#include <obos-aud/priv/backend.h>
#include <obos-aud/priv/obos-ioctls.h>

#include <obos/syscall.h>
#include <obos/error.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <math.h>

#include <sys/ioctl.h>
#include <sys/param.h>

struct output {
    handle dev;
    int dev_idx;
    struct {
        size_t codec;
        size_t codec_output_group;
        size_t output;

        size_t output_stream;
        
        uintptr_t path;
    } location;
    bool stream_configured;
    aud_output_dev info;
    stream_parameters stream_parameters;
    int format_size;

    pthread_cond_t playing_event;
    pthread_mutex_t playing_mut;
    bool is_playing;

    pthread_mutex_t buffer_lock;
    struct {
        char* buf;
        size_t len;
    } buffer;

    bool selected;

    pthread_t processor_thread;
};

enum
{
    FD_OFLAGS_READ = 1,
    FD_OFLAGS_WRITE = 2,
    FD_OFLAGS_UNCACHED = 4,
    FD_OFLAGS_NOEXEC = 8,
    FD_OFLAGS_CREATE = 16,
    FD_OFLAGS_EXECUTE = 32,
};

static size_t s_device_count = 0;
static size_t s_output_count = 0;

static handle *s_devices;
static pthread_mutex_t *s_mutexes;

struct output* s_outputs;
struct output* s_selected;

static int select_output_dev(struct output* output);

static void push_audio(struct output* output, const void* buffer, size_t size)
{
    pthread_mutex_lock(&s_mutexes[output->dev_idx]);
    int ret = select_output_dev(output);
    if (ret < 0)
    {
        pthread_mutex_unlock(&s_mutexes[output->dev_idx]);
        return;
    }

    // int remaining = 0;
    // do {
    //     ioctl(output->dev, IOCTL_HDA_STREAM_GET_REMAINING, &remaining);
    // } while (remaining != 0);

    ioctl(output->dev, IOCTL_HDA_STREAM_QUEUE_DATA);
    ret = write(output->dev, buffer, size);
    pthread_mutex_unlock(&s_mutexes[output->dev_idx]);
}

static void* process_audio(void* arg)
{
    struct output* output = arg;
    while (1)
    {
        pthread_mutex_lock(&output->playing_mut);
        if (!output->is_playing)
            pthread_cond_wait(&output->playing_event, &output->playing_mut);
        pthread_mutex_unlock(&output->playing_mut);
        
        pthread_mutex_lock(&output->buffer_lock);
        char* buf = output->buffer.buf;
        size_t len = output->buffer.len;
        output->buffer.buf = NULL;
        output->buffer.len = 0;
        pthread_mutex_unlock(&output->buffer_lock);

        size_t len_per_push = output->stream_parameters.channels * output->stream_parameters.sample_rate * output->format_size/8;
        size_t off = 0;
        while (len > len_per_push)
        {
            push_audio(output, buf + off, len_per_push);
            off += len_per_push;
            len -= len_per_push;
        }
        if (len)
            push_audio(output, buf + off, len);
        free(buf);
    }
    return NULL;
}

static int enumerate_outputs_dev(int idx)
{
    handle hnd = s_devices[idx];
    int ret = 0;
    size_t codec_count = 0, output_stream_count = 0, output_group_count = 0, output_count = 0;

    // codec -> output group -> output

    ret = ioctl(hnd, IOCTL_HDA_CODEC_COUNT, &codec_count);
    if (ret < 0)
        return ret;

    ret = ioctl(hnd, IOCTL_HDA_OUTPUT_STREAM_COUNT, &output_stream_count);
    if (ret < 0)
        return ret;

    size_t next_stream_idx = 0;

    for (size_t codec = 0; codec < codec_count; codec++)
    {
        ret = ioctl(hnd, IOCTL_HDA_CODEC_SELECT, &codec);
        if (ret < 0)
            return ret;

        ret = ioctl(hnd, IOCTL_HDA_CODEC_OUTPUT_GROUP_COUNT, &output_group_count);
        if (ret < 0)
            return ret;

        for (size_t output_group = 0; output_group < output_group_count; output_group++)
        {
            ret = ioctl(hnd, IOCTL_HDA_CODEC_SELECT_OUTPUT_GROUP, &output_group);
            if (ret < 0)
                return ret;

            ret = ioctl(hnd, IOCTL_HDA_OUTPUT_GROUP_OUTPUT_COUNT, &output_count);
            if (ret < 0)
                return ret;
            
            for (size_t output = 0; output < output_count; output++)
            {
                ret = ioctl(hnd, IOCTL_HDA_OUTPUT_GROUP_SELECT_OUTPUT, &output);
                if (ret < 0)
                    return ret;

                UhdaOutputInfo info = {};
                ret = ioctl(hnd, IOCTL_HDA_OUTPUT_GET_INFO, &info);
                if (ret < 0)
                    return ret;

                size_t stream_id = next_stream_idx++;

                if (stream_id >= output_stream_count)
                {
                    printf("obos-hda: Ran out of output streams (stream_id=%zu, output_id=%zu)!\n", stream_id, s_output_count);
                    codec = codec_count;
                    output_group = output_group_count;
                    break;
                }

                ret = ioctl(hnd, IOCTL_HDA_OUTPUT_STREAM_SELECT, &stream_id);
                if (ret < 0)
                    return ret;
    
                struct hda_path_find_parameters path_find = {};
                path_find.same_stream = false;
                path_find.other_path_count = 0;

                ret = ioctl(hnd, IOCTL_HDA_PATH_FIND, &path_find);
                if (ret < 0)
                    return ret;

                s_outputs = realloc(s_outputs, ++s_output_count * sizeof(*s_outputs));
                s_outputs[s_output_count-1].dev = hnd;
                s_outputs[s_output_count-1].dev_idx = idx;
                s_outputs[s_output_count-1].location.codec = codec;
                s_outputs[s_output_count-1].location.codec_output_group = output_group;
                s_outputs[s_output_count-1].location.output = output;
                s_outputs[s_output_count-1].location.output_stream = stream_id;
                s_outputs[s_output_count-1].location.path = path_find.found_path;
                
                // How convinient...
                // The uHDA enums are the same as the ones here.
                // (i wonder why...)
                s_outputs[s_output_count-1].info.location = info.location;
                s_outputs[s_output_count-1].info.color = info.color;
                s_outputs[s_output_count-1].info.type = info.type;

                s_outputs[s_output_count-1].playing_event = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
                s_outputs[s_output_count-1].playing_mut = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
                s_outputs[s_output_count-1].buffer_lock = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;

                // the current index + 1
                s_outputs[s_output_count-1].info.output_id = s_output_count;

                // pthread_create(&s_outputs[s_output_count-1].processor_thread, NULL, process_audio, &s_outputs[s_output_count-1]);
            }
        }
    }

    return 0;
}

int aud_backend_initialize()
{
    obos_status status = syscall3(Sys_GetHDADevices, NULL, &s_device_count, 0);
    if (obos_is_error(status))
    {
        fprintf(stderr, "Sys_GetHDADevices: %d\n", status);
        return -1;
    }

    s_devices = calloc(s_device_count, sizeof(handle));
    s_mutexes = calloc(s_device_count, sizeof(pthread_mutex_t));
    for (size_t i = 0; i < s_device_count; i++)
        s_mutexes[i] = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;

    status = syscall3(Sys_GetHDADevices, s_devices, &s_device_count, FD_OFLAGS_READ|FD_OFLAGS_WRITE|FD_OFLAGS_UNCACHED);
    if (obos_is_error(status))
    {
        fprintf(stderr, "Sys_GetHDADevices: %d\n", status);
        return -1;
    }

    for (size_t i = 0; i < s_device_count; i++)
        if (enumerate_outputs_dev(i) != 0)
            return -1;

    return 0;
}

int aud_backend_get_outputs(aud_output_dev* arr, int count)
{
    for (int i = 0; i < MIN(count, s_output_count); i++)
        memcpy(&arr[i], &s_outputs[i].info, sizeof(*arr));
    return s_output_count;
}

static int select_output_dev(struct output* output)
{
    if (output->selected)
        return 0;
    int ret = ioctl(output->dev, IOCTL_HDA_CODEC_SELECT, &output->location.codec);
    if (ret < 0)
        return ret;
    ret = ioctl(output->dev, IOCTL_HDA_CODEC_SELECT_OUTPUT_GROUP, &output->location.codec_output_group);
    if (ret < 0)
        return ret;
    ret = ioctl(output->dev, IOCTL_HDA_OUTPUT_GROUP_SELECT_OUTPUT, &output->location.output);
    if (ret < 0)
        return ret;
    ret = ioctl(output->dev, IOCTL_HDA_OUTPUT_STREAM_SELECT, &output->location.output_stream);
    if (ret < 0)
        return ret;
    if (s_selected)
        s_selected->selected = false;
    s_selected = output;
    output->selected = true;
    return ret;
}

/* All streams are PCM */
int aud_backend_configure_output(int output_id, int sample_rate, int channels, int format_size)
{
    if ((output_id-1) >= s_output_count)
    {
        errno = EINVAL;
        return -1;
    }
    
    struct output* output = &s_outputs[output_id-1];

    int fmt = 0;
    switch (format_size) {
        case 8: fmt = FORMAT_PCM8; break;
        case 16: fmt = FORMAT_PCM16; break;
        case 20: fmt = FORMAT_PCM20; break;
        case 24: fmt = FORMAT_PCM24; break;
        case 32: fmt = FORMAT_PCM32; break;
        default: errno = EINVAL; return -1;
    }
    
    pthread_mutex_lock(&s_mutexes[output->dev_idx]);
    int ret = select_output_dev(output);
    if (ret < 0)
    {
        pthread_mutex_unlock(&s_mutexes[output->dev_idx]);
        return ret;
    }

    struct hda_path_setup_parameters path_setup = {};
    path_setup.path = output->location.path;
    path_setup.stream_parameters.channels = channels;
    path_setup.stream_parameters.format = fmt;
    path_setup.stream_parameters.sample_rate = sample_rate;
    ret = ioctl(output->dev, IOCTL_HDA_PATH_SETUP, &path_setup);
    if (ret < 0)
    {
        pthread_mutex_unlock(&s_mutexes[output->dev_idx]);
        return ret;
    }
    
    if (path_setup.stream_parameters.channels != channels ||
        path_setup.stream_parameters.format != fmt ||
        path_setup.stream_parameters.sample_rate != sample_rate)
    {
        pthread_mutex_unlock(&s_mutexes[output->dev_idx]);
        errno = EOPNOTSUPP;
        return -1;
    }
        
    struct hda_stream_setup_user_parameters stream_setup = {};
    stream_setup.stream_params = path_setup.stream_parameters;
    stream_setup.ring_buffer_pipe = HANDLE_INVALID;
    stream_setup.ring_buffer_size = sample_rate * channels * (format_size/8);
    ret = ioctl(output->dev, IOCTL_HDA_STREAM_CLEAR_QUEUE, NULL);
    if (ret < 0)
    {
        pthread_mutex_unlock(&s_mutexes[output->dev_idx]);
        return ret;
    }

    ret = ioctl(output->dev, IOCTL_HDA_STREAM_SHUTDOWN, NULL);
    if (ret < 0)
    {
        pthread_mutex_unlock(&s_mutexes[output->dev_idx]);
        return ret;
    }

    ret = ioctl(output->dev, IOCTL_HDA_STREAM_SETUP_USER, &stream_setup);
    if (ret < 0)
    {
        pthread_mutex_unlock(&s_mutexes[output->dev_idx]);
        return ret;
    }

    pthread_mutex_unlock(&s_mutexes[output->dev_idx]);

    memcpy(&output->stream_parameters, &path_setup.stream_parameters, sizeof(stream_parameters));
    output->format_size = format_size;

    return 0;
}

int aud_backend_query_output_params(int output_id, int *sample_rate, int *channels, int *format_size)
{
    if ((output_id-1) >= s_output_count)
    {
        errno = EINVAL;
        return -1;
    }
    
    struct output* output = &s_outputs[output_id-1];

    *sample_rate = output->stream_parameters.sample_rate;
    *channels = output->stream_parameters.channels;
    *format_size = output->format_size;

    return 0;
}

int aud_backend_queue_data(int output_id, const void* buffer)
{
    if ((output_id-1) >= s_output_count)
    {
        errno = EINVAL;
        return -1;
    }

    struct output* output = &s_outputs[output_id-1];

    push_audio(output, buffer, (output->stream_parameters.sample_rate*output->stream_parameters.channels*(output->format_size/8)));

    // pthread_mutex_lock(&output->buffer_lock);
    // size_t new_len = output->buffer.len + (output->stream_parameters.sample_rate*output->stream_parameters.channels*(output->format_size/8));
    // output->buffer.buf = realloc(output->buffer.buf, new_len);
    // memcpy(output->buffer.buf+output->buffer.len, buffer, new_len - output->buffer.len);
    // output->buffer.len = new_len;
    // pthread_mutex_unlock(&output->buffer_lock);
    // while (output->buffer.len)
    //     sched_yield();
    
    return 0;
}

int aud_backend_output_play(int output_id, bool play)
{
    if ((output_id-1) >= s_output_count)
    {
        errno = EINVAL;
        return -1;
    }
    
    struct output* output = &s_outputs[output_id-1];

    if (output->is_playing == play)
        return 0;

    pthread_mutex_lock(&output->playing_mut);
    output->is_playing = play;
    if (output->is_playing)
        pthread_cond_signal(&output->playing_event);
    pthread_mutex_unlock(&output->playing_mut);

    pthread_mutex_lock(&s_mutexes[output->dev_idx]);
    int ret = select_output_dev(output);
    if (ret < 0)
    {
        pthread_mutex_unlock(&s_mutexes[output->dev_idx]);
        return ret;
    }

    ret = ioctl(output->dev, IOCTL_HDA_STREAM_PLAY, &play);

    pthread_mutex_unlock(&s_mutexes[output->dev_idx]);
    return ret;
}

int aud_backend_set_output_volume(int output_id, float volumef /* out of 100 */)
{
    if ((output_id-1) >= s_output_count)
    {
        errno = EINVAL;
        return -1;
    }
    
    struct output* output = &s_outputs[output_id-1];

    struct hda_path_byte_parameter volume = {.path=output->location.path,.par1=volumef};
    return ioctl(output->dev, IOCTL_HDA_PATH_VOLUME, &volume);
}