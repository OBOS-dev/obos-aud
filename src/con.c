/*
 * src/con.c
 *
 * Copyright (c) 2025 Omar Berrow
 */

#include <obos-aud/trans.h>
#include <obos-aud/priv/con.h>

#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>

#include <sys/poll.h>

static uint32_t client_ids = 1;

struct obos_aud_connection_array g_connections = {
    .lock = PTHREAD_MUTEX_INITIALIZER
};

obos_aud_connection* obos_aud_get_client(int fd, uint32_t client_id)
{
    pthread_mutex_lock(&g_connections.lock);
    for (obos_aud_connection* curr = g_connections.head; curr;)
    {
        if (curr->client_id == client_id && (fd == -1 || curr->fd == fd))
        {
            pthread_mutex_unlock(&g_connections.lock);
            return curr;
        }
        curr = curr->next;
    }
    pthread_mutex_unlock(&g_connections.lock);
    return NULL;
}

obos_aud_connection* obos_aud_get_client_by_fd(int fd)
{
    pthread_mutex_lock(&g_connections.lock);
    for (obos_aud_connection* curr = g_connections.head; curr;)
    {
        if (curr->fd == fd)
        {
            pthread_mutex_unlock(&g_connections.lock);
            return curr;
        }
        curr = curr->next;
    }
    pthread_mutex_unlock(&g_connections.lock);
    return NULL;
}

obos_aud_connection* obos_aud_process_initial_connection_request(int fd, aud_packet* pckt)
{
    obos_aud_connection* ret = calloc(1, sizeof(obos_aud_connection));
    ret->client_id = client_ids++;
    ret->fd = fd;

    pthread_mutex_lock(&g_connections.lock);
    if (!g_connections.head)
        g_connections.head = ret;
    if (g_connections.tail)
        g_connections.tail->next = ret;
    ret->prev = g_connections.tail;
    g_connections.tail = ret;
    pthread_mutex_unlock(&g_connections.lock);

    // TODO: Provide output list
    size_t nOutputs = 0;
    size_t payload_len = sizeof(aud_initial_connection_reply)+nOutputs*sizeof(uint16_t);
    aud_initial_connection_reply *payload = calloc(payload_len, 1);
    payload->client_id = ret->client_id;

    aud_packet resp = {};
    resp.opcode = OBOS_AUD_INITIAL_CONNECTION_REPLY;
    resp.client_id = ret->client_id;
    resp.payload = payload;
    resp.payload_len = payload_len;
    resp.transmission_id = pckt->transmission_id;
    resp.transmission_id_valid = true;
    autrans_transmit(fd, &resp);
    free(payload);

    return ret;
}

void obos_aud_process_disconnect(obos_aud_connection* client, aud_packet* pckt)
{
    if (!client) return;
    aud_packet resp = {};
    resp.opcode = OBOS_AUD_STATUS_REPLY_DISCONNECTED;
    resp.client_id = client->client_id;
    resp.payload = "Gracefully disconnected";
    resp.payload_len = 24;
    resp.transmission_id = pckt ? pckt->transmission_id : 0;
    resp.transmission_id_valid = !!pckt;
    autrans_transmit(client->fd, &resp);

    shutdown(client->fd, SHUT_RDWR);
    close(client->fd);
    pthread_mutex_lock(&g_connections.lock);
    if (client->prev)
        client->prev->next = client->next;
    if (client->next)
        client->next->prev = client->prev;
    if (g_connections.head == client)
        g_connections.head = client->next;
    if (g_connections.tail == client)
        g_connections.tail = client->prev;
    pthread_mutex_unlock(&g_connections.lock);
    free(client);
}