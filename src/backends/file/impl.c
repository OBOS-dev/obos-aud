/*
 * src/backends/file/impl.c
 *
 * Copyright (c) 2025 Omar Berrow
 */

#include <obos-aud/compiler.h>
#include <obos-aud/output.h>

#include <obos-aud/priv/backend.h>

#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include <sys/param.h>

static int s_backend_file_output = -1;
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

int aud_backend_initialize()
{
    s_backend_file_output = open("obos-aud-output", O_CREAT|O_TRUNC|O_WRONLY);
    if (s_backend_file_output < 0)
        return s_backend_file_output;
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
    if (format_size % 8)
        return -1;
    s_sample_rate = sample_rate;
    s_channels = channels;
    s_format_size = format_size;
    ftruncate(s_backend_file_output, 0);
    posix_fallocate(s_backend_file_output, 0, s_sample_rate*s_channels*(s_format_size/8));
    return 0;
}