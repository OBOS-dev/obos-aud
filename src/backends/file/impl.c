/*
 * src/backends/file/impl.c
 *
 * Copyright (c) 2025 Omar Berrow
 */

#define _GNU_SOURCE 1

#include <obos-aud/compiler.h>
#include <obos-aud/output.h>

#include <obos-aud/priv/backend.h>

#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>

#include <sys/param.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>

static int s_backend_file_output = -1;
static pthread_t s_backend_thread = {};
#define OUTPUT_COUNT 1
static aud_output_dev s_backend_outputs[OUTPUT_COUNT] = {
    {
        .type = OBOS_AUD_OUTPUT_TYPE_SPEAKER,
        .color = OBOS_AUD_OUTPUT_COLOR_UNKNOWN,
        .location = OBOS_AUD_OUTPUT_LOCATION_UNKNOWN,
        .pad = {},
        .flags = OBOS_AUD_OUTPUT_FLAGS_DEFAULT,
        .output_id = 1,
    }
};
static int s_sample_rate = 0;
static int s_channels = 0;
static int s_format_size = 0;

static pthread_mutex_t s_buffer_lock = PTHREAD_MUTEX_INITIALIZER;
static struct {
    char* buf;
    size_t len;
} s_buffer;

static void* process_audio(void* arg)
{
    while (1)
    {
        while (!s_sample_rate)
            sched_yield();
        
        pthread_mutex_lock(&s_buffer_lock);
        void* buf = s_buffer.buf;
        size_t len = s_buffer.len;
        s_buffer.buf = NULL;
        s_buffer.len = 0;
        pthread_mutex_unlock(&s_buffer_lock);

        write(s_backend_file_output, buf, len);
    }
    return NULL;
}

int aud_backend_initialize()
{
    s_backend_file_output = mkfifo("obos-aud-output", 777);
    if (s_backend_file_output < 0)
        return s_backend_file_output;
    pthread_create(&s_backend_thread, NULL, process_audio, NULL);
    return 0;
}

// Returns the amount of outputs, or -1 on error
int aud_backend_get_outputs(aud_output_dev* arr, int count)
{
    if (!count)
        return OUTPUT_COUNT;
    memcpy(arr, s_backend_outputs, MIN(count, OUTPUT_COUNT));
    return OUTPUT_COUNT;
}

int aud_backend_configure_output(int output_id, int sample_rate, int channels, int format_size)
{
    if (output_id != 1)
        return -1;
    if (format_size % 8 || !s_sample_rate || !channels)
        return -1;
    s_sample_rate = sample_rate;
    s_channels = channels;
    s_format_size = format_size;
    fcntl(s_backend_file_output, F_SETPIPE_SZ, s_sample_rate*s_channels*(s_format_size/8));
    return 0;
}

int aud_backend_queue_data(int output_id, const void* buf)
{
    if (!s_sample_rate)
        return -1;

    pthread_mutex_lock(&s_buffer_lock);
    size_t new_len = s_buffer.len + (s_sample_rate*s_channels*(s_format_size/8));
    s_buffer.buf = realloc(s_buffer.buf, new_len);
    memcpy(s_buffer.buf+s_buffer.len, buf, new_len - s_buffer.len);
    s_buffer.len = new_len;
    pthread_mutex_unlock(&s_buffer_lock);

    return 0;
}

int aud_backend_output_play(int output_id, bool play)
{
    if (output_id != 1)
        return -1;
    if (!s_sample_rate)
        return -1;
    if (play)
        pthread_kill(s_backend_thread, SIGCONT);
    else
        pthread_kill(s_backend_thread, SIGSTOP);
    return -1;
}

// Does nothing.
int aud_backend_set_master_volume(int output_id, float volume)
{
    if (volume < 0 || volume > 100 || output_id != 1)
        return -1;
    if (!s_sample_rate)
        return -1;
    return 0;
}