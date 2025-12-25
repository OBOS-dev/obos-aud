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

extern aud_output_dev *g_outputs;
extern size_t g_output_count;

void mixer_initialize();
void mixer_output_initialize(int output_id);
void mixer_output_add_stream(int output_id, aud_stream* stream);