/*
 * obos-aud/output.h
 *
 * This file is a part of the obos-aud project.
 *
 * Copyright (c) 2025 Omar Berrow
 * SPDX License Identifier: MIT
 */

#pragma once

#include <stdint.h>

#include <obos-aud/compiler.h>

enum aud_output_type {
    OBOS_AUD_OUTPUT_TYPE_LINE_OUT,
    OBOS_AUD_OUTPUT_TYPE_SPEAKER,
    OBOS_AUD_OUTPUT_TYPE_HEADPHONE,
    OBOS_AUD_OUTPUT_TYPE_CD,
    OBOS_AUD_OUTPUT_TYPE_SPDIF_OUT,
    OBOS_AUD_OUTPUT_TYPE_OTHER_DIGITAL_OUT,
    OBOS_AUD_OUTPUT_TYPE_UNKNOWN,
};

static const char* aud_output_type_to_str[] = {
	"line out",
	"speaker",
	"headphone",
	"CD",
	"S/PDIF out",
	"other",
	"unknown",
};

enum aud_output_color {
	OBOS_AUD_OUTPUT_COLOR_UNKNOWN = 0,
	OBOS_AUD_OUTPUT_COLOR_BLACK = 1,
	OBOS_AUD_OUTPUT_COLOR_GREY = 2,
	OBOS_AUD_OUTPUT_COLOR_BLUE = 3,
	OBOS_AUD_OUTPUT_COLOR_GREEN = 4,
	OBOS_AUD_OUTPUT_COLOR_RED = 5,
	OBOS_AUD_OUTPUT_COLOR_ORANGE = 6,
	OBOS_AUD_OUTPUT_COLOR_YELLOW = 7,
	OBOS_AUD_OUTPUT_COLOR_PURPLE = 8,
	OBOS_AUD_OUTPUT_COLOR_PINK = 9,
	OBOS_AUD_OUTPUT_COLOR_WHITE = 14,
	OBOS_AUD_OUTPUT_COLOR_OTHER = 15
};

static const char* aud_output_color_to_str[] = {
	"unknown",
	"black",
	"grey",
	"blue",
	"green",
	"red",
	"orange",
	"yellow",
	"purple",
	"pink",
	[14]="white",
	"other",
};

enum aud_output_location {
	OBOS_AUD_OUTPUT_LOCATION_NA = 0,
	OBOS_AUD_OUTPUT_LOCATION_REAR = 1,
	OBOS_AUD_OUTPUT_LOCATION_FRONT = 2,
	OBOS_AUD_OUTPUT_LOCATION_LEFT = 3,
	OBOS_AUD_OUTPUT_LOCATION_RIGHT = 4,
	OBOS_AUD_OUTPUT_LOCATION_TOP = 5,
	OBOS_AUD_OUTPUT_LOCATION_BOTTOM = 6,
	OBOS_AUD_OUTPUT_LOCATION_SPECIAL = 7,
	OBOS_AUD_OUTPUT_LOCATION_REAR_PANEL,
	OBOS_AUD_OUTPUT_LOCATION_DRIVE_BAY,
	OBOS_AUD_OUTPUT_LOCATION_RISER,
	OBOS_AUD_OUTPUT_LOCATION_DISPLAY,
	OBOS_AUD_OUTPUT_LOCATION_ATAPI,
	OBOS_AUD_OUTPUT_LOCATION_INSIDE_LID,
	OBOS_AUD_OUTPUT_LOCATION_OUTSIDE_LID,
	OBOS_AUD_OUTPUT_LOCATION_UNKNOWN
};

static const char* aud_output_location_to_str[] = {
	"N/A",
	"rear",
	"front",
	"left",
	"right",
	"top",
	"bottom",
	"special",
	"rear panel",
	"drive bay",
	"riser",
	"display",
	"ATAPI",
	"inside lid",
	"outside lid",
	"unknown",
};

enum aud_flags {
	OBOS_AUD_OUTPUT_FLAGS_DEFAULT = (1<<0),
};

/* All fields are in little-endian unless otherwise specified. */

typedef struct aud_output_dev {
    uint8_t type;
    uint8_t color;
    uint8_t location;
    uint8_t pad[2];
	uint8_t flags;
    uint16_t output_id;
} PACK aud_output_dev;

typedef struct aud_output_parameters {
	int channels;
	int sample_rate;
	int format_size;
} aud_output_parameters;

#define OBOS_AUD_DEFAULT_OUTPUT_DEV UINT16_MAX