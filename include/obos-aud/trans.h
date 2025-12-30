/*
 * obos-aud/trans.h
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

#include <obos-aud/compiler.h>
#include <obos-aud/output.h>

#include <sys/socket.h>

/* PROTOCOL NOTE: Audio is to start playing on a stream after the first DATA packet is sent. */

enum aud_opcode {
    OBOS_AUD_REQUEST_BEGIN = 0x0000,
    OBOS_AUD_INITIAL_CONNECTION_REQUEST,
    OBOS_AUD_NOP,
    OBOS_AUD_DISCONNECT_REQUEST,
    OBOS_AUD_OPEN_STREAM,
    OBOS_AUD_CLOSE_STREAM,
    OBOS_AUD_DATA,
    OBOS_AUD_QUERY_OUTPUT_DEVICE,
    OBOS_AUD_STREAM_SET_VOLUME,
    OBOS_AUD_STREAM_GET_VOLUME,
    OBOS_AUD_OUTPUT_SET_VOLUME,
    OBOS_AUD_OUTPUT_GET_VOLUME,
    OBOS_AUD_CONNECTION_SET_VOLUME,
    OBOS_AUD_CONNECTION_GET_VOLUME,
    OBOS_AUD_STREAM_SET_FLAGS,
    OBOS_AUD_STREAM_GET_FLAGS,
    OBOS_AUD_SET_NAME, /* sets the user-readable name of the current connection */
    OBOS_AUD_QUERY_CONNECTIONS,

    OBOS_AUD_REQUEST_REPLY_BEGIN = 0x1000,
    OBOS_AUD_INITIAL_CONNECTION_REPLY,
    OBOS_AUD_OPEN_STREAM_REPLY,
    OBOS_AUD_QUERY_OUTPUT_DEVICE_REPLY,
    OBOS_AUD_GET_VOLUME_REPLY,
    OBOS_AUD_STREAM_GET_FLAGS_REPLY,
    OBOS_AUD_QUERY_CONNECTIONS_REPLY,

    /*
     * All status replies have no required payload,
     * as the opcode is the payload itself.
     * In the case that a status reply does have a
     * payload, it is extra info on the error.
     */
    OBOS_AUD_STATUS_REPLY_OK = 0x2000,
    OBOS_AUD_STATUS_REPLY_UNSUPPORTED,
    OBOS_AUD_STATUS_REPLY_INVAL,
    OBOS_AUD_STATUS_REPLY_DISCONNECTED,
    OBOS_AUD_STATUS_REPLY_CEILING = 0x2fff,
};

#define OBOS_AUD_HEADER_MAGIC (0x0b05a7d1 /* obosaudi */)
typedef struct aud_header {
    uint32_t magic;
    uint32_t data_offset;
    uint32_t size; // includes sizeof(aud_header)
    uint32_t opcode;
    uint32_t trans_id;
    uint32_t client_id;
    char payload[];
} PACK aud_header;

/* CONSTANT!!! */
#define OBOS_AUD_BASE_PROTOCOL_HEADER_SIZE (24)

/**************************************************/
/* Reply structures */

typedef struct aud_initial_connection_reply {
    uint32_t client_id;
    uint16_t output_ids[];
} PACK aud_initial_connection_reply;

typedef struct aud_open_stream_reply {
    uint16_t stream_id;
} PACK aud_open_stream_reply;

typedef struct aud_stream_get_flags_reply {
    uint32_t flags;
} aud_stream_get_flags_reply;

typedef struct aud_query_output_device_reply {
    aud_output_dev info;
} PACK aud_query_output_device_reply;

typedef struct aud_get_volume_reply {
    float volume;
} PACK aud_get_volume_reply;

struct aud_connection_desc {
    uint32_t sizeof_desc;
    uint32_t client_id;
    char name[];
};

#define autrans_next_connection_desc(desc) ((struct aud_connection_desc*)((uintptr_t)desc + desc->sizeof_desc))
typedef struct aud_query_connections_reply {
    uint32_t arr_offset;
    struct aud_connection_desc descs[];
} PACK aud_query_connections_reply;

/* Payload structures */
/**************************************************/

/* PROTOCOL NOTE: Stream format is locked to PCM16 until further notice */

typedef struct aud_open_stream_payload {
    uint16_t output_id;
    uint32_t target_sample_rate;
    uint8_t input_channels;
    float volume;
} PACK aud_open_stream_payload;

typedef struct aud_set_volume_payload {
    /* The type of this id depends on the opcode. */
    union {
        uint16_t obj_id;
        uint32_t obj_id32;
    };
    float volume;
} PACK aud_set_volume_payload;
typedef union aud_get_volume_payload {
    /* The type of this id depends on the opcode. */
    uint16_t obj_id;
    uint32_t obj_id32;
} PACK aud_get_volume_payload;

typedef struct aud_close_stream_payload {
    uint16_t stream_id;
} PACK aud_close_stream_payload;

typedef struct aud_query_output_device_payload {
    uint16_t output_id;
} PACK aud_query_output_device_payload;

typedef struct aud_data_payload {
    uint16_t stream_id;
    char data[];
} PACK aud_data_payload;

typedef struct aud_stream_set_flags_payload {
    uint16_t stream_id;
    uint32_t flags;
} PACK aud_stream_set_flags_payload;

typedef struct aud_stream_get_flags_payload {
    uint16_t stream_id;
} PACK aud_stream_get_flags_payload;

typedef struct aud_set_name_payload {
    char name[];
} aud_set_name_payload;

/***************************************************/

/*
 * This struct is for library purposes, 
 * it does not exist on the network.
 */

typedef struct aud_packet
{
    uint32_t opcode;
    
    /* If replying to a packet, set this to the
     * transmission id of the received packet,
     * otherwise this field can be disregarded */
    uint32_t transmission_id;
    bool transmission_id_valid;
    
    uint32_t client_id;

    union {
        void* payload;
        const void* cpayload;
    };
    uint32_t payload_len;
} aud_packet;

/* All functions return -1 on error, and >0 on success */

int autrans_transmit(int fd, aud_packet* pckt);
int autrans_receive(int fd, aud_packet* pckt, void* sockaddr, socklen_t *sockaddr_len);
int autrans_initial_connection_request(int fd);
int autrans_set_name(int fd, uint32_t client_id, const char* name);
int autrans_stream_open(int fd, uint32_t client_id, const aud_open_stream_payload* payload, uint16_t* stream_id, uint32_t* stream_flags);
// *flags is set to the real flags on return.
int autrans_stream_flags(int socket, uint32_t client_id, uint16_t stream_id, uint32_t* flags);
int autrans_stream_data(int socket, uint32_t client_id, uint16_t stream_id, const void* data, size_t len);
int autrans_stream_set_volume(int socket, uint32_t client_id, uint16_t stream_id, float volume);
int autrans_stream_get_volume(int socket, uint32_t client_id, uint16_t stream_id, float* volume);
int autrans_output_set_volume(int socket, uint32_t client_id, uint16_t output_id, float volume);
int autrans_output_get_volume(int socket, uint32_t client_id, uint16_t output_id, float* volume);
int autrans_connection_set_volume(int socket, uint32_t client_id, uint32_t tgt, float volume);
int autrans_connection_get_volume(int socket, uint32_t client_id, uint32_t tgt, float* volume);
int autrans_disconnect(int fd, uint32_t client_id);
int autrans_query_output(int fd, uint32_t client_id, uint16_t output_id, uint32_t *transmission_id);

int autrans_open();
/*
 * Valid URIs:
 * tcp:addr[:port]
 * unix:path/server index
 */
int autrans_open_uri(const char* addr);
int autrans_open_addr(struct sockaddr* addr, socklen_t addr_len);

/* makes a name for the current client that
 * looks something like:
 * (whatever name is) <pid>
 */
char* autrans_make_name(const char* name, bool take_basename);

const char* autrans_opcode_to_string(uint32_t opcode);

#define OBOS_AUD_TCP_PORT 44630