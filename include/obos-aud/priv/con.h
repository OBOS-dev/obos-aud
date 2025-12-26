/*
 * obos-aud/priv/con.h
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
#include <obos-aud/trans.h>
#include <obos-aud/stream.h>

#include <obos-aud/priv/mixer.h>

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#include <netinet/in.h>

typedef struct obos_aud_stream_handle {
    mixer_output_device* dev;
    aud_stream_node* stream_node;
    uint16_t stream_id;
    struct obos_aud_stream_handle *next, *prev;
} obos_aud_stream_handle;

typedef struct obos_aud_connection {
    int fd;
    uint32_t client_id;
    float volume;
    struct {
        uint16_t next_stream_id;
        pthread_mutex_t lock;
        obos_aud_stream_handle *head, *tail;  
    } stream_handles;
    struct obos_aud_connection *next, *prev;
} obos_aud_connection;

extern struct obos_aud_connection_array {
    obos_aud_connection *head, *tail;
    pthread_mutex_t lock;
} g_connections;

obos_aud_connection* obos_aud_get_client(int fd, uint32_t client_id);
obos_aud_connection* obos_aud_get_client_by_fd(int fd);

void obos_aud_process_stream_open(obos_aud_connection* client, aud_packet* pckt);
void obos_aud_process_stream_close(obos_aud_connection* client, aud_packet* pckt);
void obos_aud_process_data(obos_aud_connection* client, aud_packet* pckt);
void obos_aud_process_stream_set_volume(obos_aud_connection* client, aud_packet* pckt);
void obos_aud_process_output_set_volume(obos_aud_connection* client, aud_packet* pckt);
void obos_aud_process_conn_set_volume(obos_aud_connection* client, aud_packet* pckt);
void obos_aud_process_stream_get_volume(obos_aud_connection* client, aud_packet* pckt);
void obos_aud_process_output_get_volume(obos_aud_connection* client, aud_packet* pckt);
void obos_aud_process_conn_get_volume(obos_aud_connection* client, aud_packet* pckt);
void obos_aud_stream_close(obos_aud_connection* client, obos_aud_stream_handle* hnd, bool locked);
obos_aud_stream_handle* obos_aud_get_stream_by_id(obos_aud_connection* con, uint16_t stream_id);

void obos_aud_process_output_device_query(obos_aud_connection* client, aud_packet* pckt);

obos_aud_connection* obos_aud_process_initial_connection_request(int fd, aud_packet* pckt);
void obos_aud_process_disconnect(obos_aud_connection* client, aud_packet* pckt);
