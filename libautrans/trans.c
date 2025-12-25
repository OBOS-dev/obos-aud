/*
 * libautrans/trans.c
 *
 * Copyright (c) 2025 Omar Berrow
 */

#define _GNU_SOURCE 1

#include <obos-aud/trans.h>
#include <obos-aud/compiler.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#include <sys/socket.h>

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