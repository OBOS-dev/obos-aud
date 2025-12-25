/*
 * src/mixer.c
 *
 * Copyright (c) 2025 Omar Berrow
 */

#include <obos-aud/priv/mixer.h>
#include <obos-aud/priv/backend.h>

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

aud_output_dev *g_outputs;
size_t g_output_count;

void mixer_initialize()
{
    if (aud_backend_initialize() < 0)
    {
        fprintf(stderr, "aud_backend_initialize failed! errno=%d\n", errno);
        abort();
    }

    g_output_count = aud_backend_get_outputs(NULL, 0);
    g_outputs = calloc(g_output_count, sizeof(aud_output_dev));
    aud_backend_get_outputs(g_outputs, g_output_count);

    for (size_t i = 0; i < g_output_count; i++)
        mixer_output_initialize(g_outputs[i].output_id);
}

void mixer_output_initialize(int output_id)
{
    
}

void mixer_output_add_stream(int output_id, aud_stream* stream);