/*
 * libautrans/trans.c
 *
 * Copyright (c) 2025 Omar Berrow
 */

#define _GNU_SOURCE 1
 
#include <ctype.h>
#include <stdio.h>

#include <obos-aud/trans.h>
#include <obos-aud/compiler.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <netdb.h>

static uint32_t next_trans_id()
{
    static uint32_t iter = 0;
    return ++iter;
}

int autrans_transmit(int fd, aud_packet* pckt)
{
    if (!pckt || fd <= 0)
    {
        errno = EINVAL;
        return -1;
    }

    aud_header* hdr = malloc(pckt->payload_len+sizeof(*hdr));
    memset(hdr, 0, sizeof(*hdr));

    hdr->magic = aud_hton32(OBOS_AUD_HEADER_MAGIC);
    
    hdr->size = aud_hton32(pckt->payload_len+sizeof(*hdr));
    hdr->data_offset = aud_hton32(sizeof(*hdr));

    hdr->opcode = aud_hton32(pckt->opcode);
    
    if (pckt->transmission_id_valid)
        hdr->trans_id = pckt->transmission_id;
    else
        hdr->trans_id = aud_hton32(next_trans_id());
    pckt->transmission_id = hdr->trans_id;
    hdr->client_id = pckt->client_id;

    if (pckt->payload_len)
        memcpy(hdr->payload, pckt->cpayload, pckt->payload_len);

    int nTransmitted = 0;
    int nLeft = hdr->size;

    while (nTransmitted != aud_hton32(hdr->size))
    {
        int ret = TEMP_FAILURE_RETRY(send(fd, ((char*)hdr) + nTransmitted, nLeft, 0));
        if (ret < 0)
        {
            free(hdr);
            return ret;
        }
        nTransmitted += ret;
        nLeft -= ret;
    }

    free(hdr);
    return nTransmitted;
}

int autrans_receive(int fd, aud_packet* pckt, void* sockaddr, socklen_t *sockaddr_len)
{
    if (!pckt || fd <= 0)
    {
        errno = EINVAL;
        return -1;
    }
    int err = 0;
    aud_header hdr = {};
    uint32_t offset = 0;

    err = TEMP_FAILURE_RETRY(recvfrom(fd, &hdr, sizeof(hdr.magic)+sizeof(hdr.data_offset), MSG_WAITALL, sockaddr, sockaddr_len));
    if (err < 0)
        return err;
    if (err == 0)
    {
        errno = ECONNRESET;
        return -1;
    }
    hdr.magic = aud_ntoh32(hdr.magic);
    if (hdr.magic != OBOS_AUD_HEADER_MAGIC)
    {
        errno = EINVAL;
        return -1;
    }
    hdr.data_offset = aud_ntoh32(hdr.data_offset);
    offset += sizeof(hdr.data_offset)+sizeof(hdr.magic);

    // In no protocol version is the header
    // size less than this many bytes 
    if (hdr.data_offset < OBOS_AUD_BASE_PROTOCOL_HEADER_SIZE)
    {
        errno = EINVAL;
        return -1;
    }

    // Read the base protocol fields.
    err = TEMP_FAILURE_RETRY(recv(fd, ((char*)&hdr)+offset, OBOS_AUD_BASE_PROTOCOL_HEADER_SIZE-offset, MSG_WAITALL));
    if (err < 0)
        return err;
    if (err == 0)
    {
        errno = ECONNRESET;
        return -1;
    }
    hdr.size = aud_ntoh32(hdr.size);
    hdr.opcode = aud_ntoh32(hdr.opcode);
    pckt->transmission_id = hdr.trans_id;
    pckt->transmission_id_valid = true;
    pckt->client_id = hdr.client_id;
    pckt->opcode = hdr.opcode;

    if (!(hdr.size - hdr.data_offset))
    {
        pckt->payload = NULL;
        pckt->payload_len = 0;
        return 0;
    }
    offset += (OBOS_AUD_BASE_PROTOCOL_HEADER_SIZE-offset);

    // We currently do not know of any more fields.
    // Read the rest of the header, then the payload.

    pckt->payload_len = hdr.size - hdr.data_offset; 
    pckt->payload = malloc(pckt->payload_len);

    if (hdr.data_offset - sizeof(hdr))
    {
        void* sink = malloc(hdr.data_offset - sizeof(hdr));
        err = TEMP_FAILURE_RETRY(recv(fd, sink, hdr.data_offset - sizeof(hdr), MSG_WAITALL));
        if (err < 0)
            return err;
        if (err == 0)
        {
            errno = ECONNRESET;
            return -1;
        }
        free(sink);
    }

    err = TEMP_FAILURE_RETRY(recv(fd, pckt->payload, pckt->payload_len, MSG_WAITALL));
    if (err < 0)
        return err;
    if (err == 0)
    {
        errno = ECONNRESET;
        return -1;
    }
    return 0;
}

int autrans_initial_connection_request(int fd)
{
    aud_packet pckt = {.opcode=OBOS_AUD_INITIAL_CONNECTION_REQUEST};
    return autrans_transmit(fd, &pckt);
}
int autrans_disconnect(int fd, uint32_t client_id)
{
    aud_packet pckt = {.opcode=OBOS_AUD_DISCONNECT_REQUEST,.client_id=client_id};
    return autrans_transmit(fd, &pckt);
}
int autrans_query_output(int fd, uint32_t client_id, uint16_t output_id, uint32_t *transmission_id)
{
    aud_query_output_device_payload payload = {.output_id=output_id};
    aud_packet pckt = {
        .opcode = OBOS_AUD_QUERY_OUTPUT_DEVICE,
        .client_id = client_id,
        .payload = &payload,
        .payload_len = sizeof(payload),
    };
    int ret = autrans_transmit(fd, &pckt);
    *transmission_id = pckt.transmission_id;
    return ret;
}


int autrans_stream_flags(int socket, uint32_t client_id, uint16_t stream_id, uint32_t* flags)
{
    int res = 0;
    do {
        aud_stream_set_flags_payload payload = {};
        payload.stream_id = stream_id;
        payload.flags = *flags;

        aud_packet reply = {};
        aud_packet pckt = {};
        pckt.opcode = OBOS_AUD_STREAM_SET_FLAGS;
        pckt.client_id = client_id;
        pckt.payload = &payload;
        pckt.payload_len = sizeof(payload);
        if (autrans_transmit(socket, &pckt) < 0)
        {
            shutdown(socket, SHUT_RDWR);
            close(socket);
            perror("autrans_transmit");
            return -1;
        }
    
        if ((res = autrans_receive(socket, &reply, NULL, 0)) < 0)
            break;
        if (__builtin_expect(reply.opcode == OBOS_AUD_STATUS_REPLY_OK, true))
            continue;
    
        if (reply.opcode >= OBOS_AUD_STATUS_REPLY_OK && reply.opcode < OBOS_AUD_STATUS_REPLY_CEILING)
        {
            fprintf(stderr, "While setting stream flags: %s\n", autrans_opcode_to_string(reply.opcode));
            if (reply.payload_len)
                fprintf(stderr, "Extra info: %.*s\n", reply.payload_len, (char*)reply.payload);
        }
        else
            fprintf(stderr, "While setting stream flags: Unexpected %s from server (payload length=%d)\n", autrans_opcode_to_string(reply.opcode), reply.payload_len);
        free(reply.payload);
        return -1;
    } while(0);
    do {
        aud_stream_get_flags_payload payload = {};
        payload.stream_id = stream_id;

        aud_packet reply = {};
        aud_packet pckt = {};
        pckt.opcode = OBOS_AUD_STREAM_GET_FLAGS;
        pckt.client_id = client_id;
        pckt.payload = &payload;
        pckt.payload_len = sizeof(payload);
        if ((res = autrans_transmit(socket, &pckt)) < 0)
        {
            shutdown(socket, SHUT_RDWR);
            close(socket);
            perror("autrans_transmit");
            return res;
        }
    
        if ((res = autrans_receive(socket, &reply, NULL, 0)) < 0)
            return res;
        
        res = -1;
        errno = EOPNOTSUPP;

        if (reply.opcode >= OBOS_AUD_STATUS_REPLY_OK && reply.opcode < OBOS_AUD_STATUS_REPLY_CEILING)
        {
            fprintf(stderr, "While fetching stream flags: %s\n", autrans_opcode_to_string(reply.opcode));
            if (reply.payload_len)
                fprintf(stderr, "Extra info: %.*s\n", reply.payload_len, (char*)reply.payload);
        }
        else if (reply.opcode != OBOS_AUD_STREAM_GET_FLAGS_REPLY)
            fprintf(stderr, "While fetching stream flags: Unexpected %s from server (payload length=%d)\n", autrans_opcode_to_string(reply.opcode), reply.payload_len);
        else {
            aud_stream_get_flags_reply* reply_payload = reply.payload;
            *flags = reply_payload->flags;
            errno = 0;
            res = 0;
        }
        
        free(reply.payload);
    } while(0);
    return res;

}

int autrans_stream_data(int socket, uint32_t client_id, uint16_t stream_id, const void* data, size_t len)
{
    aud_data_payload *payload = malloc(len+2);
    memcpy(payload->data, data, len);
    payload->stream_id = stream_id;

    aud_packet pckt = {};
    aud_packet reply = {};
    pckt.opcode = OBOS_AUD_DATA;
    pckt.client_id = client_id;
    pckt.payload = payload;
    pckt.payload_len = len+sizeof(aud_data_payload);
    if (autrans_transmit(socket, &pckt) < 0)
    {
        shutdown(socket, SHUT_RDWR);
        close(socket);
        perror("autrans_transmit");
        return -1;
    }

    if (autrans_receive(socket, &reply, NULL, 0) < 0)
        return -1;
    if (__builtin_expect(reply.opcode == OBOS_AUD_STATUS_REPLY_OK, true))
        return 0;

    if (reply.opcode >= OBOS_AUD_STATUS_REPLY_OK && reply.opcode < OBOS_AUD_STATUS_REPLY_CEILING)
    {
        fprintf(stderr, "While writing to stream: %s\n", autrans_opcode_to_string(reply.opcode));
        if (reply.payload_len)
            fprintf(stderr, "Extra info: %.*s\n", reply.payload_len, (char*)reply.payload);
    }
    else
        fprintf(stderr, "While writing to stream: Unexpected %s from server (payload length=%d)\n", autrans_opcode_to_string(reply.opcode), reply.payload_len);
    free(reply.payload);
    return -1;
}

int autrans_stream_open(int socket, const uint32_t client_id, const aud_open_stream_payload* stream_info, uint16_t* stream_id, uint32_t* stream_flags)
{
    do {
        aud_packet pckt = {};
        aud_packet reply = {};
        pckt.opcode = OBOS_AUD_OPEN_STREAM;
        pckt.client_id = client_id;
        pckt.payload = (void*)stream_info /* is not modified on transit */;
        pckt.payload_len = sizeof(*stream_info);
        if (autrans_transmit(socket, &pckt) < 0)
            return -1;

        const uint32_t transmission_id = pckt.transmission_id;

        if (autrans_receive(socket, &reply, NULL, 0) < 0)
            return -1;

        if (transmission_id != reply.transmission_id)
        {
            fprintf(stderr, "Unexpected transmission ID in server reply.\n");
            return -1;
        }

        if (reply.opcode == OBOS_AUD_OPEN_STREAM_REPLY)
        {
            aud_open_stream_reply *payload = reply.payload;
            *stream_id = payload->stream_id;
            free(payload);
            break;
        }

        if (reply.opcode >= OBOS_AUD_STATUS_REPLY_OK && reply.opcode < OBOS_AUD_STATUS_REPLY_CEILING)
        {
            fprintf(stderr, "While opening stream: %s\n", autrans_opcode_to_string(reply.opcode));
            if (reply.payload_len)
                fprintf(stderr, "Extra info: %.*s\n", reply.payload_len, (char*)reply.payload);
        }
        else
            fprintf(stderr, "While opening stream: Unexpected %s from server (payload length=%d)\n", autrans_opcode_to_string(reply.opcode), reply.payload_len);
        free(reply.payload);
        return -1;
    } while(0);
    int sample_size = 2;
    if (stream_flags && *stream_flags)
        return autrans_stream_flags(socket, client_id, *stream_id, stream_flags);
    return 0;
}

#define volume_set_common(socket, client_id, tgt, prefix, opcode_val, volume) \
{\
    do {\
        aud_set_volume_payload payload = {.obj_id##prefix=tgt,.volume=volume};\
        aud_packet pckt = {};\
        aud_packet reply = {};\
        pckt.opcode = opcode_val;\
        pckt.client_id = client_id;\
        pckt.payload = &payload;\
        pckt.payload_len = sizeof(payload);\
        if (autrans_transmit(socket, &pckt) < 0)\
            return -1;\
\
        const uint32_t transmission_id = pckt.transmission_id;\
\
        if (autrans_receive(socket, &reply, NULL, 0) < 0)\
            return -1;\
\
        if (transmission_id != reply.transmission_id)\
        {\
            fprintf(stderr, "Unexpected transmission ID in server reply.\n");\
            return -1;\
        }\
\
        if (reply.opcode == OBOS_AUD_STATUS_REPLY_OK)\
            break;\
\
        if (reply.opcode >= OBOS_AUD_STATUS_REPLY_OK && reply.opcode < OBOS_AUD_STATUS_REPLY_CEILING)\
        {\
            fprintf(stderr, "While opening stream: %s\n", autrans_opcode_to_string(reply.opcode));\
            if (reply.payload_len)\
                fprintf(stderr, "Extra info: %.*s\n", reply.payload_len, (char*)reply.payload);\
        }\
        else\
            fprintf(stderr, "While opening stream: Unexpected %s from server (payload length=%d)\n", autrans_opcode_to_string(reply.opcode), reply.payload_len);\
        free(reply.payload);\
        return -1;\
    } while(0);\
    return 0;\
}
#define volume_get_common(socket, client_id, tgt, prefix, opcode_val, volume) \
{\
    do {\
        aud_get_volume_payload payload = {.obj_id##prefix=tgt};\
        aud_packet pckt = {};\
        aud_packet reply = {};\
        pckt.opcode = opcode_val;\
        pckt.client_id = client_id;\
        pckt.payload = &payload;\
        pckt.payload_len = sizeof(payload);\
        if (autrans_transmit(socket, &pckt) < 0)\
            return -1;\
\
        const uint32_t transmission_id = pckt.transmission_id;\
\
        if (autrans_receive(socket, &reply, NULL, 0) < 0)\
            return -1;\
\
        if (transmission_id != reply.transmission_id)\
        {\
            fprintf(stderr, "Unexpected transmission ID in server reply.\n");\
            return -1;\
        }\
\
        if (reply.opcode == OBOS_AUD_STREAM_GET_FLAGS_REPLY)\
        {\
            aud_get_volume_reply* reply_payload = reply.payload;\
            *volume = reply_payload->volume;\
            free(reply.payload);\
            break;\
        }\
\
        if (reply.opcode >= OBOS_AUD_STATUS_REPLY_OK && reply.opcode < OBOS_AUD_STATUS_REPLY_CEILING)\
        {\
            fprintf(stderr, "While opening stream: %s\n", autrans_opcode_to_string(reply.opcode));\
            if (reply.payload_len)\
                fprintf(stderr, "Extra info: %.*s\n", reply.payload_len, (char*)reply.payload);\
        }\
        else\
            fprintf(stderr, "While opening stream: Unexpected %s from server (payload length=%d)\n", autrans_opcode_to_string(reply.opcode), reply.payload_len);\
        free(reply.payload);\
        return -1;\
    } while(0);\
    return 0;\
}

int autrans_stream_set_volume(int socket, uint32_t client_id, uint16_t stream_id, float volume)
    volume_set_common(socket, client_id, stream_id, , OBOS_AUD_STREAM_SET_VOLUME, volume)
int autrans_stream_get_volume(int socket, uint32_t client_id, uint16_t stream_id, float* volume)
    volume_get_common(socket, client_id, stream_id, , OBOS_AUD_STREAM_GET_VOLUME, volume)

int autrans_output_set_volume(int socket, uint32_t client_id, uint16_t output_id, float volume)
    volume_set_common(socket, client_id, output_id, , OBOS_AUD_OUTPUT_SET_VOLUME, volume)
int autrans_output_get_volume(int socket, uint32_t client_id, uint16_t output_id, float* volume)
    volume_get_common(socket, client_id, output_id, , OBOS_AUD_OUTPUT_GET_VOLUME, volume)

int autrans_connection_set_volume(int socket, uint32_t client_id, uint32_t tgt, float volume)
    volume_set_common(socket, client_id, tgt, 32, OBOS_AUD_CONNECTION_SET_VOLUME, volume)
int autrans_connection_get_volume(int socket, uint32_t client_id, uint32_t tgt, float* volume)
    volume_get_common(socket, client_id, tgt, 32, OBOS_AUD_CONNECTION_GET_VOLUME, volume)

int autrans_open()
{
    return autrans_open_uri(getenv("AUD_DISPLAY"));
}
int autrans_open_uri(const char* addr)
{
    const char* const initial_addr = addr;

    if (!addr)
    {
        errno = EINVAL;
        fprintf(stderr, "Could not open display %s\n", initial_addr);
        return -1;
    }
    if (!*addr)
    {
        errno = EINVAL;
        fprintf(stderr, "Could not open display %s\n", initial_addr);
        return -1;
    }
    char* comp1_end = strchr(addr, ':');
    if (!comp1_end)
    {
        errno = EINVAL;
        fprintf(stderr, "Could not open display %s\n", initial_addr);
        return -1;
    }
    size_t comp1_len = comp1_end-addr;
    bool inet = false;
    if (strncmp(addr, "tcp", comp1_len) == 0)
        inet = true;
    else if (strncmp(addr, "unix", comp1_len) == 0)
        inet = false;
    else
    {
        errno = EINVAL;
        return -1;
    }
    addr = comp1_end+1;
    if (!*addr)
    {
        errno = EINVAL;
        return -1;
    }

    if (inet)
    {
        struct addrinfo hints = {};
        struct addrinfo *result = NULL, *iter = NULL;
        int res = 0;

        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = 0;
        hints.ai_protocol = 0;

        const char* name = NULL;
        const char* service = NULL;
        bool service_free = false;
        bool name_free = false;

        char* comp2_end = strchr(addr, ':');
        if (!comp2_end)
        {
            service = STRINGIFY(OBOS_AUD_TCP_PORT);
            name = addr;
            name_free = false;
            service_free = false;
        }
        else
        {
            service = comp2_end+1;
            service_free = false;
            name = memcpy(malloc((comp2_end-addr)+1), addr, (comp2_end-addr));
            ((char*)name)[(comp2_end-addr)] = 0;
            name_free = true;
        }
        
        res = getaddrinfo(name, service, &hints, &result);

        if (name_free) 
            free((char*)name);
        if (service_free) 
            free((char*)service);

        if (res != 0)
        {
            fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(res));
            fprintf(stderr, "Could not open display %s\n", initial_addr);
            return -1;
        }

        for (iter = result; iter; iter = iter->ai_next)
        {
            res = autrans_open_addr(iter->ai_addr, iter->ai_addrlen);
            if (res >= 0)
                break;
        }
        freeaddrinfo(result);

        if (res < 0)
            fprintf(stderr, "Could not open display %s\n", initial_addr);

        return res;
    }

    // Open a UNIX socket
    const char* path = NULL;
    bool free_path = false;
    if (isdigit(*addr))
    {
        int index = strtol(addr, NULL, 0);
        free_path = true;
        char* p = NULL;
        int len = snprintf(NULL, 0, "/tmp/.obos-aud/U%d", index);
        p = malloc(len+1);
        snprintf(p, len+1, "/tmp/.obos-aud/U%d", index);
        path = p;
    }
    else 
        path = addr;

    socklen_t addr_len = sizeof(sa_family_t)+strlen(path)+1;
    struct sockaddr* saddr = malloc(addr_len);
    saddr->sa_family = AF_UNIX;
    memcpy(saddr->sa_data, path, addr_len-sizeof(sa_family_t));

    int res = autrans_open_addr(saddr, addr_len);
    if (res < 0)
    {
        perror("autrans_open_addr");
        fprintf(stderr, "Could not open display %s\n", initial_addr);
    }
    free(saddr);
    if (free_path)
        free((char*)path);

    return res;
}
int autrans_open_addr(struct sockaddr* addr, socklen_t addr_len)
{
    int sock = socket(addr->sa_family, SOCK_STREAM, 0), res = 0;
    if (sock == -1)
        return -1;
    res = connect(sock, addr, addr_len);
    if (res < 0)
    {
        close(sock);
        return res;
    }
    return sock;
}

const char* autrans_opcode_to_string(uint32_t opcode)
{
    switch (opcode) {
        case OBOS_AUD_REQUEST_BEGIN: return "OBOS_AUD_REQUEST_BEGIN";
        case OBOS_AUD_INITIAL_CONNECTION_REQUEST: return "OBOS_AUD_INITIAL_CONNECTION_REQUEST";
        case OBOS_AUD_NOP: return "OBOS_AUD_NOP";
        case OBOS_AUD_DISCONNECT_REQUEST: return "OBOS_AUD_DISCONNECT_REQUEST";
        case OBOS_AUD_OPEN_STREAM: return "OBOS_AUD_OPEN_STREAM";
        case OBOS_AUD_CLOSE_STREAM: return "OBOS_AUD_CLOSE_STREAM";
        case OBOS_AUD_DATA: return "OBOS_AUD_DATA";
        case OBOS_AUD_QUERY_OUTPUT_DEVICE: return "OBOS_AUD_QUERY_OUTPUT_DEVICE";
        case OBOS_AUD_STREAM_SET_VOLUME: return "OBOS_AUD_STREAM_SET_VOLUME";
        case OBOS_AUD_STREAM_GET_VOLUME: return "OBOS_AUD_STREAM_GET_VOLUME";
        case OBOS_AUD_OUTPUT_SET_VOLUME: return "OBOS_AUD_OUTPUT_SET_VOLUME";
        case OBOS_AUD_OUTPUT_GET_VOLUME: return "OBOS_AUD_OUTPUT_GET_VOLUME";
        case OBOS_AUD_CONNECTION_SET_VOLUME: return "OBOS_AUD_CONNECTION_SET_VOLUME";
        case OBOS_AUD_CONNECTION_GET_VOLUME: return "OBOS_AUD_CONNECTION_GET_VOLUME";
        case OBOS_AUD_STREAM_SET_FLAGS: return "OBOS_AUD_STREAM_SET_FLAGS";
        case OBOS_AUD_STREAM_GET_FLAGS: return "OBOS_AUD_STREAM_GET_FLAGS";
        case OBOS_AUD_SET_NAME: return "OBOS_AUD_SET_NAME";
        case OBOS_AUD_QUERY_CONNECTIONS: return "OBOS_AUD_QUERY_CONNECTIONS";
        case OBOS_AUD_REQUEST_REPLY_BEGIN: return "OBOS_AUD_REQUEST_REPLY_BEGIN";
        case OBOS_AUD_INITIAL_CONNECTION_REPLY: return "OBOS_AUD_INITIAL_CONNECTION_REPLY";
        case OBOS_AUD_OPEN_STREAM_REPLY: return "OBOS_AUD_OPEN_STREAM_REPLY";
        case OBOS_AUD_QUERY_OUTPUT_DEVICE_REPLY: return "OBOS_AUD_QUERY_OUTPUT_DEVICE_REPLY";
        case OBOS_AUD_GET_VOLUME_REPLY: return "OBOS_AUD_GET_VOLUME_REPLY";
        case OBOS_AUD_STREAM_GET_FLAGS_REPLY: return "OBOS_AUD_STREAM_GET_FLAGS_REPLY";
        case OBOS_AUD_QUERY_CONNECTIONS_REPLY: return "OBOS_AUD_QUERY_CONNECTIONS_REPLY";
        case OBOS_AUD_STATUS_REPLY_OK: return "OBOS_AUD_STATUS_REPLY_OK";
        case OBOS_AUD_STATUS_REPLY_UNSUPPORTED: return "OBOS_AUD_STATUS_REPLY_UNSUPPORTED";
        case OBOS_AUD_STATUS_REPLY_INVAL: return "OBOS_AUD_STATUS_REPLY_INVAL";
        case OBOS_AUD_STATUS_REPLY_DISCONNECTED: return "OBOS_AUD_STATUS_REPLY_DISCONNECTED";
        case OBOS_AUD_STATUS_REPLY_CEILING: return "OBOS_AUD_STATUS_REPLY_CEILING";
        default: break;
    }
    return "OBOS_AUD_INVALID_OPCODE";
}