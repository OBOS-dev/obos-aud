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
    ret->stream_handles.lock = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
    ret->stream_handles.next_stream_id = 1;
    
    pthread_mutex_lock(&g_connections.lock);
    if (!g_connections.head)
        g_connections.head = ret;
    if (g_connections.tail)
        g_connections.tail->next = ret;
    ret->prev = g_connections.tail;
    g_connections.tail = ret;
    pthread_mutex_unlock(&g_connections.lock);

    size_t nOutputs = g_output_count;
    size_t payload_len = sizeof(aud_initial_connection_reply)+ nOutputs * sizeof(uint16_t);
    aud_initial_connection_reply *payload = calloc(payload_len, 1);
    payload->client_id = ret->client_id;
    for (size_t i = 0; i < nOutputs; i++)
        payload->output_ids[i] = g_outputs[i].info.output_id;

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

void obos_aud_process_stream_open(obos_aud_connection* client, aud_packet* pckt)
{
    if (pckt->payload_len != sizeof(aud_open_stream_payload))
    {
        aud_packet resp = {};
        resp.opcode = OBOS_AUD_STATUS_REPLY_INVAL;
        resp.client_id = client->client_id;
        resp.payload = "Invalid payload length.";
        resp.payload_len = 24;
        resp.transmission_id = pckt->transmission_id;
        resp.transmission_id_valid = true;
        autrans_transmit(client->fd, &resp);
        return;
    }

    if (client->stream_handles.next_stream_id == 0)
    {
        aud_packet resp = {};
        resp.opcode = OBOS_AUD_STATUS_REPLY_UNSUPPORTED;
        resp.client_id = client->client_id;
        resp.payload = "No more stream handles left.";
        resp.payload_len = 29;
        resp.transmission_id = pckt->transmission_id;
        resp.transmission_id_valid = true;
        autrans_transmit(client->fd, &resp);
        return;
    }

    aud_open_stream_payload *payload = pckt->payload;
    
    mixer_output_device* dev = mixer_output_from_id(payload->output_id);
    if (!dev)
    {
        aud_packet resp = {};
        resp.opcode = OBOS_AUD_STATUS_REPLY_INVAL;
        resp.client_id = client->client_id;
        resp.payload = "Invalid output ID.";
        resp.payload_len = 19;
        resp.transmission_id = pckt->transmission_id;
        resp.transmission_id_valid = true;
        autrans_transmit(client->fd, &resp);
        return;
    }
    aud_stream_node* node = mixer_output_add_stream_dev(dev);
    aud_stream_initialize(&node->data, payload->target_sample_rate, payload->input_channels);

    pthread_mutex_lock(&client->stream_handles.lock);
    obos_aud_stream_handle* hnd = calloc(1, sizeof(obos_aud_stream_handle));
    hnd->stream_id = client->stream_handles.next_stream_id++;
    hnd->stream_node = node;
    hnd->dev = dev;
    if (!client->stream_handles.head)
        client->stream_handles.head = hnd;
    if (client->stream_handles.tail)
        client->stream_handles.tail->next = hnd;
    hnd->prev = client->stream_handles.tail;
    client->stream_handles.tail = hnd;
    pthread_mutex_unlock(&client->stream_handles.lock);

    aud_open_stream_reply reply_payload = {};
    reply_payload.stream_id = hnd->stream_id;

    aud_packet resp = {};
    resp.opcode = OBOS_AUD_OPEN_STREAM_REPLY;
    resp.client_id = client->client_id;
    resp.payload = &reply_payload;
    resp.payload_len = sizeof(reply_payload);
    resp.transmission_id = pckt->transmission_id;
    resp.transmission_id_valid = true;
    autrans_transmit(client->fd, &resp);
}

void obos_aud_process_stream_close(obos_aud_connection* client, aud_packet* pckt)
{
    if (pckt->payload_len != sizeof(aud_close_stream_payload))
    {
        aud_packet resp = {};
        resp.opcode = OBOS_AUD_STATUS_REPLY_INVAL;
        resp.client_id = client->client_id;
        resp.payload = "Invalid payload length.";
        resp.payload_len = 24;
        resp.transmission_id = pckt->transmission_id;
        resp.transmission_id_valid = true;
        autrans_transmit(client->fd, &resp);
        return;
    }

    aud_close_stream_payload *payload = pckt->payload;
    obos_aud_stream_handle* hnd = obos_aud_get_stream_by_id(client, payload->stream_id);
    if (!hnd)
    {
        aud_packet resp = {};
        resp.opcode = OBOS_AUD_STATUS_REPLY_INVAL;
        resp.client_id = client->client_id;
        resp.payload = "Invalid stream ID.";
        resp.payload_len = 19;
        resp.transmission_id = pckt->transmission_id;
        resp.transmission_id_valid = true;
        autrans_transmit(client->fd, &resp);
        return;
    }

    obos_aud_stream_close(client, hnd, true);
}

void obos_aud_stream_close(obos_aud_connection* client, obos_aud_stream_handle* hnd, bool locked)
{
    if (locked)
        pthread_mutex_lock(&client->stream_handles.lock);
    if (client->stream_handles.head == hnd)
        client->stream_handles.head = hnd->next;
    else
        hnd->prev->next = hnd->next;
    if (client->stream_handles.tail == hnd)
        client->stream_handles.tail = hnd->prev;
    else
        hnd->next->prev = hnd->prev;
    if (locked)
        pthread_mutex_unlock(&client->stream_handles.lock);
    mixer_output_remove_stream_dev(hnd->dev, hnd->stream_node);
    free(hnd);
}

obos_aud_stream_handle* obos_aud_get_stream_by_id(obos_aud_connection* con, uint16_t stream_id)
{
    pthread_mutex_lock(&con->stream_handles.lock);
    obos_aud_stream_handle* curr = con->stream_handles.head;
    for (; curr; curr = curr->next)
        if (curr->stream_id == stream_id)
            break;
    pthread_mutex_unlock(&con->stream_handles.lock);
    return curr;
}

void obos_aud_process_output_device_query(obos_aud_connection* client, aud_packet* pckt)
{
    if (pckt->payload_len != sizeof(aud_query_output_device_payload))
    {
        aud_packet resp = {};
        resp.opcode = OBOS_AUD_STATUS_REPLY_INVAL;
        resp.client_id = client->client_id;
        resp.payload = "Invalid payload length.";
        resp.payload_len = 24;
        resp.transmission_id = pckt->transmission_id;
        resp.transmission_id_valid = true;
        autrans_transmit(client->fd, &resp);
        return;
    }

    aud_query_output_device_payload* payload = pckt->payload;
    mixer_output_device* dev = mixer_output_from_id(payload->output_id);
    if (!dev)
    {
        aud_packet resp = {};
        resp.opcode = OBOS_AUD_STATUS_REPLY_INVAL;
        resp.client_id = client->client_id;
        resp.payload = "Invalid output device ID.";
        resp.payload_len = 26;
        resp.transmission_id = pckt->transmission_id;
        resp.transmission_id_valid = true;
        autrans_transmit(client->fd, &resp);
        return;
    }

    aud_query_output_device_reply reply_payload = {};
    reply_payload.info = dev->info;

    aud_packet resp = {};
    resp.opcode = OBOS_AUD_QUERY_OUTPUT_DEVICE_REPLY;
    resp.client_id = client->client_id;
    resp.payload = &reply_payload;
    resp.payload_len = sizeof(reply_payload);
    resp.transmission_id = pckt->transmission_id;
    resp.transmission_id_valid = true;
    autrans_transmit(client->fd, &resp);
}

void obos_aud_process_disconnect(obos_aud_connection* client, aud_packet* pckt)
{
    if (!client) return;

    pthread_mutex_lock(&client->stream_handles.lock);
    for (obos_aud_stream_handle* curr = client->stream_handles.head; curr; )
    {
        obos_aud_stream_handle* next = curr->next;
        obos_aud_stream_close(client, curr, false);
        curr = next;
    }
    pthread_mutex_unlock(&client->stream_handles.lock);

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