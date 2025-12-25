/*
 * libautrans/trans.c
 *
 * Copyright (c) 2025 Omar Berrow
 */

 #include <bits/sockaddr.h>
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

int autrans_transmit(int fd, const aud_packet* pckt)
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
    hdr->client_id = pckt->client_id;

    if (pckt->payload_len)
        memcpy(hdr->payload, pckt->payload, pckt->payload_len);

    int ret = TEMP_FAILURE_RETRY(send(fd, hdr, hdr->size, 0));
    free(hdr);
    return ret;
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
    hdr.size = aud_ntoh32(hdr.size);
    hdr.opcode = aud_ntoh32(hdr.opcode);
    pckt->transmission_id = hdr.trans_id;
    pckt->transmission_id_valid = true;
    pckt->client_id = hdr.client_id;

    if (!(hdr.size - hdr.data_offset))
    {
        pckt->opcode = hdr.opcode;
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
        free(sink);
    }

    return TEMP_FAILURE_RETRY(recv(fd, pckt->payload, pckt->payload_len, MSG_WAITALL));
}

int autrans_initial_connection_request(int fd)
{
    aud_packet pckt = {.opcode=OBOS_AUD_INITIAL_CONNECTION_REQUEST};
    return autrans_transmit(fd, &pckt);
}
int autrans_disconnect(int fd)
{
    aud_packet pckt = {.opcode=OBOS_AUD_DISCONNECT_REQUEST};
    return autrans_transmit(fd, &pckt);
}

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