/*
 * test/client_main.c
 *
 * Copyright (c) 2025 Omar Berrow
 */

#include <fcntl.h>
#define _GNU_SOURCE 1
 
#include <obos-aud/trans.h>
#include <obos-aud/output.h>

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sys/socket.h>
#include <sys/param.h>

const char* usage = "%s [-d display_uri] input_file\n";

int main(int argc, char** argv)
{
    int opt = 0;

    const char* server_uri = NULL;

    while ((opt = getopt(argc, argv, "+hd:")) != -1)
    {
        switch (opt)
        {
            case 'd':
                server_uri = optarg;
                break;
            case 'h':
            default:
                fprintf(stderr, usage, argv[0]);
                return opt != 'h';
        }
    }

    if (optind >= argc)
    {
        fprintf(stderr, usage, argv[0]);
        return -1;
    }
    const char* input_file = argv[optind];
    int input = open(input_file, O_RDONLY);
    if (input == -1)
    {
        perror("open");
        return -1;
    }

    int socket = server_uri ? autrans_open_uri(server_uri) : autrans_open();
    if (socket < 0)
        return -1;

    uint32_t client_id = 0;

    if (autrans_initial_connection_request(socket) < 0)
    {
        shutdown(socket, SHUT_RDWR);
        close(socket);
        perror("autrans_transmit");
        return -1;
    }

    struct aud_packet reply = {};
    if (autrans_receive(socket, &reply, NULL, 0) < 0)
    {
        shutdown(socket, SHUT_RDWR);
        close(socket);
        perror("autrans_receive");
        return -1;
    }
    client_id = reply.client_id;

    if (reply.opcode != OBOS_AUD_INITIAL_CONNECTION_REPLY)
    {
        if (reply.opcode != OBOS_AUD_INITIAL_CONNECTION_REPLY)
            fprintf(stderr, "Unexpected opcode in server reply to initial connection request.\n");
        autrans_disconnect(socket, client_id);
        shutdown(socket, SHUT_RDWR);
        close(socket);
        return -1;
    }

    size_t output_count = reply.payload_len-sizeof(aud_initial_connection_reply);
    if (!output_count)
    {
        autrans_disconnect(socket, client_id);
        fprintf(stderr, "Audio server has no available outputs.\n");
        free(reply.payload);
        shutdown(socket, SHUT_RDWR);
        close(socket);
        return -1;
    }

    free(reply.payload);

    uint16_t output = OBOS_AUD_DEFAULT_OUTPUT_DEV;
    uint16_t stream = 0;
    aud_output_dev output_info = {};
    do {
        uint32_t transmission_id = 0;
        if (autrans_query_output(socket, client_id, output, &transmission_id) < 0)
        {
            shutdown(socket, SHUT_RDWR);
            close(socket);
            perror("autrans_transmit");
            return -1;
        }
        if (autrans_receive(socket, &reply, NULL, 0) < 0)
        {
            shutdown(socket, SHUT_RDWR);
            close(socket);
            perror("autrans_receive");
            return -1;
        }
        if (transmission_id != reply.transmission_id)
        {
            autrans_disconnect(socket, client_id);
            shutdown(socket, SHUT_RDWR);
            close(socket);
            fprintf(stderr, "Unexpected transmission ID in server reply.\n");
            return -1;
        }

        if (reply.opcode == OBOS_AUD_QUERY_OUTPUT_DEVICE_REPLY)
        {
            aud_query_output_device_reply *payload = reply.payload;
            memcpy(&output_info, &payload->info, MIN(sizeof(output_info), reply.payload_len));
            break;
        }

        if (reply.opcode >= OBOS_AUD_STATUS_REPLY_OK && reply.opcode < OBOS_AUD_STATUS_REPLY_CEILING)
        {
            fprintf(stderr, "While querying default output: %s\n", autrans_opcode_to_string(reply.opcode));
            if (reply.payload_len)
                fprintf(stderr, "Extra info: %.*s\n", reply.payload_len, (char*)reply.payload);
        }
        else
            fprintf(stderr, "While querying default output: Unexpected %s from server (payload length=%d)\n", autrans_opcode_to_string(reply.opcode), reply.payload_len);
        goto die;
    } while(0);

    output = output_info.output_id;
    printf("Selected %s output at 0x%x\n", aud_output_type_to_str[output_info.type], output);
    printf("Opening dual-channel stream at 44100hz\n");

    aud_open_stream_payload stream_info = {};
    stream_info.input_channels = 2;
    stream_info.target_sample_rate = 44100;
    stream_info.output_id = output;
    do {

        aud_packet pckt = {};
        pckt.opcode = OBOS_AUD_OPEN_STREAM;
        pckt.client_id = client_id;
        pckt.payload = &stream_info;
        pckt.payload_len = sizeof(stream_info);
        if (autrans_transmit(socket, &pckt) < 0)
        {
            shutdown(socket, SHUT_RDWR);
            close(socket);
            perror("autrans_transmit");
            return -1;
        }

        const uint32_t transmission_id = pckt.transmission_id;

        if (autrans_receive(socket, &reply, NULL, 0) < 0)
        {
            shutdown(socket, SHUT_RDWR);
            close(socket);
            perror("autrans_receive");
            return -1;
        }
        if (transmission_id != reply.transmission_id)
        {
            autrans_disconnect(socket, client_id);
            shutdown(socket, SHUT_RDWR);
            close(socket);
            fprintf(stderr, "Unexpected transmission ID in server reply.\n");
            return -1;
        }

        if (reply.opcode == OBOS_AUD_OPEN_STREAM_REPLY)
        {
            aud_open_stream_reply *payload = reply.payload;
            stream = payload->stream_id;
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
        goto die;
    } while(0);

    size_t buffer_size = stream_info.target_sample_rate * stream_info.input_channels * sizeof(int16_t);
    aud_data_payload *payload = malloc(buffer_size+sizeof(aud_data_payload));
    payload->stream_id = stream;
    size_t avail = 0;
    while ((avail = read(input, payload->data, buffer_size)) > 0)
    {
        aud_packet pckt = {};
        pckt.opcode = OBOS_AUD_DATA;
        pckt.client_id = client_id;
        pckt.payload = payload;
        pckt.payload_len = avail+sizeof(aud_data_payload);
        if (autrans_transmit(socket, &pckt) < 0)
        {
            shutdown(socket, SHUT_RDWR);
            close(socket);
            perror("autrans_transmit");
            return -1;
        }

        if (autrans_receive(socket, &reply, NULL, 0) < 0)
            break;
        if (__builtin_expect(reply.opcode == OBOS_AUD_STATUS_REPLY_OK, true))
            continue;

        if (reply.opcode >= OBOS_AUD_STATUS_REPLY_OK && reply.opcode < OBOS_AUD_STATUS_REPLY_CEILING)
        {
            fprintf(stderr, "While writing to stream: %s\n", autrans_opcode_to_string(reply.opcode));
            if (reply.payload_len)
                fprintf(stderr, "Extra info: %.*s\n", reply.payload_len, (char*)reply.payload);
        }
        else
            fprintf(stderr, "While writing to stream: Unexpected %s from server (payload length=%d)\n", autrans_opcode_to_string(reply.opcode), reply.payload_len);
        
    }
    if (avail < 0)
        perror("read");

    die:
    autrans_disconnect(socket, client_id);

    shutdown(socket, SHUT_RDWR);
    close(socket);
    return 0;
}