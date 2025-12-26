/*
 * obos-aud/priv/mixer.h
 *
 * This file is a part of the obos-aud project.
 *
 * Copyright (c) 2025 Omar Berrow
 * SPDX License Identifier: MIT
 */

#pragma once

#if !BUILDING_OBOS_AUD_SERVER
#   error Not building obos-aud server!
#endif

#include <obos-aud/output.h>
#include <obos-aud/stream.h>

#include <pthread.h>
#include <stdbool.h>

typedef struct aud_stream_node {
    aud_stream data;
    uint64_t time_offset_us;
    /* for averaging, not guaranteed to exist */
    int16_t* input_samples_arr;
    int sample_count;
    bool dead;
    struct obos_aud_connection* owner;
    struct aud_stream_node *next, *prev;
} aud_stream_node;

typedef struct mixer_output_device {
    aud_output_dev info;
    struct {
        aud_stream_node *head, *tail;
        size_t nNodes;
        pthread_mutex_t lock;
    } streams;
    int input_channels;
    int sample_rate;
    int channels;
    int format_size;
    float volume;
    pthread_t worker;
} mixer_output_device;

extern mixer_output_device *g_outputs;
extern size_t g_output_count;
extern mixer_output_device* g_default_output;

void mixer_initialize();

void mixer_output_initialize(mixer_output_device* dev);

mixer_output_device* mixer_output_from_id(int output_id);

aud_stream_node* mixer_output_add_stream(int output_id);
void mixer_output_remove_stream(int output_id, aud_stream_node* stream);

aud_stream_node* mixer_output_add_stream_dev(mixer_output_device* dev);
void mixer_output_remove_stream_dev(mixer_output_device* dev, aud_stream_node* stream);
void mixer_output_remove_stream_dev_unlocked(mixer_output_device* dev, aud_stream_node* stream);

void mixer_output_set_default(int output_id);

void mixer_output_set_volume(mixer_output_device* dev, float volume);
float mixer_output_get_volume(mixer_output_device* dev);

float mixer_normalize_volume(float volume);
float mixer_get_volume(float normalized_volume);