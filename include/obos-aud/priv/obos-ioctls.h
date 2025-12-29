/*
 * obos-aud/priv/obos-ioctls.h
 *
 * This file is a part of the obos-aud project.
 *
 * Copyright (c) 2025 Omar Berrow
 * SPDX License Identifier: MIT
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <obos/syscall.h>

enum hda_ioctls {
    IOCTL_HDA_BASE_IOCTLs = 0x100,

    IOCTL_HDA_OUTPUT_STREAM_COUNT,
    IOCTL_HDA_OUTPUT_STREAM_SELECT,
    IOCTL_HDA_OUTPUT_STREAM_SELECTED,

    IOCTL_HDA_CODEC_COUNT,
    IOCTL_HDA_CODEC_SELECT,
    IOCTL_HDA_CODEC_SELECTED,

    IOCTL_HDA_CODEC_OUTPUT_GROUP_COUNT,
    IOCTL_HDA_CODEC_SELECT_OUTPUT_GROUP,
    IOCTL_HDA_CODEC_SELECTED_OUTPUT_GROUP,

    IOCTL_HDA_OUTPUT_GROUP_OUTPUT_COUNT,
    IOCTL_HDA_OUTPUT_GROUP_SELECT_OUTPUT,
    IOCTL_HDA_OUTPUT_GROUP_SELECTED_OUTPUT,

    IOCTL_HDA_OUTPUT_GET_PRESENCE,
    IOCTL_HDA_OUTPUT_GET_INFO,

    IOCTL_HDA_STREAM_SETUP,
    IOCTL_HDA_STREAM_PLAY,
    IOCTL_HDA_STREAM_QUEUE_DATA,
    IOCTL_HDA_STREAM_CLEAR_QUEUE,
    IOCTL_HDA_STREAM_SHUTDOWN,
    IOCTL_HDA_STREAM_GET_STATUS,
    IOCTL_HDA_STREAM_GET_REMAINING,
    IOCTL_HDA_STREAM_GET_BUFFER_SIZE,

    IOCTL_HDA_PATH_FIND,
    IOCTL_HDA_PATH_SETUP,
    IOCTL_HDA_PATH_SHUTDOWN,
    IOCTL_HDA_PATH_VOLUME,
    IOCTL_HDA_PATH_MUTE,
};

enum {
    FORMAT_PCM8,
    FORMAT_PCM16,
    FORMAT_PCM20,
    FORMAT_PCM24,
    FORMAT_PCM32,
};

typedef          size_t *hda_get_size_parameter;
typedef          size_t *hda_get_count_parameter;
typedef          size_t *hda_get_index_parameter;
typedef    const size_t *hda_set_index_parameter;
typedef            bool *hda_output_get_presence_parameter;
typedef const uintptr_t *hda_path_shutdown_parameter;
typedef struct stream_parameters {
    uint32_t sample_rate;
    uint32_t channels;
    uint8_t format;
} stream_parameters;
typedef struct hda_stream_setup_user_parameters {
    stream_parameters stream_params;
    uint32_t ring_buffer_size;
    /* Can be HANDLE_INVALID, and does not need to be a pipe */
    handle ring_buffer_pipe; 
} *hda_stream_setup_user_parameters;
typedef const bool *hda_stream_play;
typedef struct hda_path_find_parameters {
    bool same_stream;
    size_t other_path_count;
    uintptr_t found_path; /* output field */
    uintptr_t other_paths[];
} *hda_path_find_parameters;
typedef struct hda_path_setup_parameters {
    uintptr_t path;
    stream_parameters stream_parameters; /* stream parameters hint<-->actual stream parameters */
} *hda_path_setup_parameters;
typedef uint32_t* hda_path_get_status_parameter;
struct hda_path_boolean_parameter {
    uintptr_t path;
    bool par1;
};
struct hda_path_byte_parameter {
    uintptr_t path;
    uint8_t par1;
};
typedef const struct hda_path_boolean_parameter *hda_path_mute_parameter;
typedef const struct hda_path_byte_parameter *hda_path_volume_parameter;

typedef enum UhdaOutputType {
	UHDA_OUTPUT_TYPE_LINE_OUT,
	UHDA_OUTPUT_TYPE_SPEAKER,
	UHDA_OUTPUT_TYPE_HEADPHONE,
	UHDA_OUTPUT_TYPE_CD,
	UHDA_OUTPUT_TYPE_SPDIF_OUT,
	UHDA_OUTPUT_TYPE_OTHER_DIGITAL_OUT,
	UHDA_OUTPUT_TYPE_UNKNOWN
} UhdaOutputType;

typedef enum UhdaColor {
	UHDA_COLOR_UNKNOWN = 0,
	UHDA_COLOR_BLACK = 1,
	UHDA_COLOR_GREY = 2,
	UHDA_COLOR_BLUE = 3,
	UHDA_COLOR_GREEN = 4,
	UHDA_COLOR_RED = 5,
	UHDA_COLOR_ORANGE = 6,
	UHDA_COLOR_YELLOW = 7,
	UHDA_COLOR_PURPLE = 8,
	UHDA_COLOR_PINK = 9,
	UHDA_COLOR_WHITE = 14,
	UHDA_COLOR_OTHER = 15
} UhdaColor;

typedef enum UhdaLocation {
	UHDA_LOCATION_NA = 0,
	UHDA_LOCATION_REAR = 1,
	UHDA_LOCATION_FRONT = 2,
	UHDA_LOCATION_LEFT = 3,
	UHDA_LOCATION_RIGHT = 4,
	UHDA_LOCATION_TOP = 5,
	UHDA_LOCATION_BOTTOM = 6,
	UHDA_LOCATION_SPECIAL = 7,
	UHDA_LOCATION_REAR_PANEL,
	UHDA_LOCATION_DRIVE_BAY,
	UHDA_LOCATION_RISER,
	UHDA_LOCATION_DISPLAY,
	UHDA_LOCATION_ATAPI,
	UHDA_LOCATION_INSIDE_LID,
	UHDA_LOCATION_OUTSIDE_LID,
	UHDA_LOCATION_UNKNOWN
} UhdaLocation;

typedef struct UhdaOutputInfo {
	UhdaOutputType type;
	UhdaColor color;
	UhdaLocation location;
} UhdaOutputInfo;