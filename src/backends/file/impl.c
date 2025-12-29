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
#include <stdio.h>
#include <errno.h>

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
static bool s_playing = false;
static pthread_cond_t s_playing_event = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t s_playing_mut = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t s_buffer_lock = PTHREAD_MUTEX_INITIALIZER;
static struct {
    char* buf;
    size_t len;
} s_buffer;

static void* process_audio(void* arg)
{
    while (1)
    {
        pthread_mutex_lock(&s_playing_mut);
        if (!s_playing)
            pthread_cond_wait(&s_playing_event, &s_playing_mut);
        pthread_mutex_unlock(&s_playing_mut);
        
        pthread_mutex_lock(&s_buffer_lock);
        void* buf = s_buffer.buf;
        size_t len = s_buffer.len;
        s_buffer.buf = NULL;
        s_buffer.len = 0;
        pthread_mutex_unlock(&s_buffer_lock);

        TEMP_FAILURE_RETRY(write(s_backend_file_output, buf, len));
        free(buf);
    }
    return NULL;
}

#define FIFO_NAME "obos-aud-output"

static void delete_fifo()
{
    unlink(FIFO_NAME);
}

int aud_backend_initialize()
{
    errno = 0;
    int res = mkfifo(FIFO_NAME, 0777);
    if (res < 0)
        return s_backend_file_output;
    s_backend_file_output = open(FIFO_NAME, O_WRONLY);
    if (s_backend_file_output < 0)
        return s_backend_file_output;
    atexit(delete_fifo);
    pthread_create(&s_backend_thread, NULL, process_audio, NULL);
    aud_backend_output_play(1, false);
    return 0;
}

// Returns the amount of outputs, or -1 on error
int aud_backend_get_outputs(aud_output_dev* arr, int count)
{
    memcpy(arr, s_backend_outputs, MIN(count, OUTPUT_COUNT)*sizeof(*arr));
    return OUTPUT_COUNT;
}

int aud_backend_configure_output(int output_id, int sample_rate, int channels, int format_size)
{
    if (output_id != 1)
        return -1;
    if ((format_size % 8) != 0 || !sample_rate || !channels)
        return -1;
    s_sample_rate = sample_rate;
    s_channels = channels;
    s_format_size = format_size;
    fcntl(s_backend_file_output, F_SETPIPE_SZ, s_sample_rate*s_channels*(s_format_size/8));
    return 0;
}

int aud_backend_query_output_params(int output_id, int *sample_rate, int *channels, int *format_size)
{
    if (output_id != 1)
        return -1;
    if (!s_sample_rate)
        return -1;
    *sample_rate = s_sample_rate;
    *channels = s_channels;
    *format_size = s_format_size;
    return 0;
}

int aud_backend_queue_data(int output_id, const void* buf, int len)
{
    if (!s_sample_rate)
        return -1;

    pthread_mutex_lock(&s_buffer_lock);
    size_t new_len = s_buffer.len + len;
    s_buffer.buf = realloc(s_buffer.buf, new_len);
    memcpy(s_buffer.buf+s_buffer.len, buf, new_len - s_buffer.len);
    s_buffer.len = new_len;
    pthread_mutex_unlock(&s_buffer_lock);
    while (s_buffer.len)
        sched_yield();

    return 0;
}

int aud_backend_output_play(int output_id, bool play)
{
    if (output_id != 1)
        return -1;
    if (!s_sample_rate)
        return -1;
    pthread_mutex_lock(&s_playing_mut);
    s_playing = play;
    if (s_playing)
        pthread_cond_signal(&s_playing_event);
    pthread_mutex_unlock(&s_playing_mut);
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