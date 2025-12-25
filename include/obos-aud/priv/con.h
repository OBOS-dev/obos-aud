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

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#include <netinet/in.h>

typedef struct obos_aud_connection {
    int fd;
    uint32_t client_id;
    struct obos_aud_connection *next, *prev;
} obos_aud_connection;

extern struct obos_aud_connection_array {
    obos_aud_connection *head, *tail;
    pthread_mutex_t lock;
} g_connections;

obos_aud_connection* obos_aud_get_client(int fd, uint32_t client_id);
obos_aud_connection* obos_aud_get_client_by_fd(int fd);
obos_aud_connection* obos_aud_process_initial_connection_request(int fd, aud_packet* pckt);
void obos_aud_process_disconnect(obos_aud_connection* client);
