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

// Returns -1 on error, >0 on success
WEAK int aud_backend_initialize();
// Returns the amount of outputs, or -1 on error
WEAK int aud_backend_get_outputs(aud_output_dev* arr, int count);
// All streams are PCM
WEAK int aud_backend_configure_output(int output_id, int sample_rate, int channels, int format_size);