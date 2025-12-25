/*
 * obos-aud/priv/backend.h
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

#include <obos-aud/compiler.h>
#include <obos-aud/output.h>

#include <stdbool.h>

/* Returns -1 on error, >0 on success */
WEAK int aud_backend_initialize();
/* Returns the amount of outputs, or -1 on error */
WEAK int aud_backend_get_outputs(aud_output_dev* arr, int count);
/* All streams are PCM */
WEAK int aud_backend_configure_output(int output_id, int sample_rate, int channels, int format_size);
/* The memory at buf is assumed to be sample_rate*channels*format_size_bytes in length. */
WEAK int aud_backend_queue_data(int output_id, const void* buf);
WEAK int aud_backend_output_play(int output_id, bool play);
WEAK int aud_backend_set_output_volume(int output_id, float volume /* out of 100 */);