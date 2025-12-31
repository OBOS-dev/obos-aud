/*
 * test/client_main.c
 *
 * Copyright (c) 2025 Omar Berrow
 */

#define _GNU_SOURCE 1

#include <fcntl.h>
 
#include <obos-aud/trans.h>
#include <obos-aud/output.h>
#include <obos-aud/stream.h>

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <sys/socket.h>
#include <sys/param.h>

const char* usage = "%s [-d display_uri] [-c channels] [-s sample_rate] [-f format] [-o output_id] [-h] input_file\n";

static int get_format(const char* fmt)
{
    int res = -1;
    if (strcasecmp(fmt, "pcm32") == 0)
        res = OBOS_AUD_STREAM_FLAGS_PCM32_DECODE;
    else if (strcasecmp(fmt, "pcm24") == 0)
        res = OBOS_AUD_STREAM_FLAGS_PCM24_DECODE;
    else if (strcasecmp(fmt, "ulaw") == 0)
        res = OBOS_AUD_STREAM_FLAGS_ULAW_DECODE;
    else if (strcasecmp(fmt, "alaw") == 0)
        res = OBOS_AUD_STREAM_FLAGS_ALAW_DECODE;
    else if (strcasecmp(fmt, "f32") == 0)
        res = OBOS_AUD_STREAM_FLAGS_F32_DECODE;
    else if (strcasecmp(fmt, "pcm16") == 0)
        res = 0;
    return res;
}

int main(int argc, char** argv)
{
    int opt = 0;

    const char* server_uri = NULL;
    int channels = 2;
    int sample_rate = 44100;
    float volume = 100.f;
    int format_flags = 0;
    uint16_t output = OBOS_AUD_DEFAULT_OUTPUT_DEV;

    while ((opt = getopt(argc, argv, "hs:c:v:d:f:o:")) != -1)
    {
        switch (opt)
        {
            case 'd':
                server_uri = optarg;
                break;
            case 'c':
                errno = 0;
                channels = strtol(optarg, NULL, 0);
                if (errno != 0 || channels <= 0)
                {
                    fprintf(stderr, "Expected positive non-zero integer, got %s\n", optarg);
                    return -1;
                }
                break;
            case 'o':
                errno = 0;
                output = strtol(optarg, NULL, 0);
                if (errno != 0)
                {
                    fprintf(stderr, "Expected integer, got %s\n", optarg);
                    return -1;
                }
                break;
            case 's':
                errno = 0;
                sample_rate = strtol(optarg, NULL, 0);
                if (errno != 0 || sample_rate <= 0)
                {
                    fprintf(stderr, "Expected positive non-zero integer, got %s\n", optarg);
                    return -1;
                }
                break;
            case 'v':
                errno = 0;
                volume = strtof(optarg, NULL);
                if (errno != 0 || volume < 0)
                {
                    fprintf(stderr, "Expected positive float, got %s\n", optarg);
                    return -1;
                }
                break;
            case 'f':
                format_flags = get_format(optarg);
                if (format_flags == -1)
                {
                    fprintf(stderr, "Expected: f32, pcm32, pcm24, pcm16, ulaw, or alaw, got \"%s\".\n", optarg);
                    return -1;
                }
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

    char* name = autrans_make_name(argv[0], true);
    autrans_set_name(socket, client_id, name);
    free(name);

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
            free(payload);
            break;
        }

        if (reply.opcode >= OBOS_AUD_STATUS_REPLY_OK && reply.opcode < OBOS_AUD_STATUS_REPLY_CEILING)
        {
            fprintf(stderr, "While querying output device: %s\n", autrans_opcode_to_string(reply.opcode));
            if (reply.payload_len)
                fprintf(stderr, "Extra info: %.*s\n", reply.payload_len, (char*)reply.payload);
        }
        else
            fprintf(stderr, "While querying output device: Unexpected %s from server (payload length=%d)\n", autrans_opcode_to_string(reply.opcode), reply.payload_len);
        free(reply.payload);
        goto die;
    } while(0);

    output = output_info.output_id;
    printf("Selected %s output at 0x%x\n", aud_output_type_to_str[output_info.type], output);
    if (channels == 1)
        printf("Opening single-channel stream at %dhz\n", sample_rate);
    else if (channels == 2)
        printf("Opening dual-channel stream at %dhz\n", sample_rate);
    else if (channels > 2)
        printf("Opening stream with %d channels at %dhz\n", channels, sample_rate);

    uint32_t stream_flags = format_flags;
    const uint32_t initial_flags = stream_flags;
    aud_open_stream_payload stream_info = {};
    stream_info.input_channels = channels;
    stream_info.target_sample_rate = sample_rate;
    stream_info.output_id = output;
    stream_info.volume = volume;
    int sample_size = 2;
    int res = autrans_stream_open(socket, client_id, &stream_info, &stream, &stream_flags);
    if (res < 0)
        goto die;
    if (stream_flags != initial_flags)
    {
        fprintf(stderr, "Server does not support one or more passed flags\n");
        goto die;
    }

    switch (format_flags) {
        case OBOS_AUD_STREAM_FLAGS_ALAW_DECODE:
        case OBOS_AUD_STREAM_FLAGS_ULAW_DECODE:
            sample_size = 1;
            break;
        case OBOS_AUD_STREAM_FLAGS_F32_DECODE:
        case OBOS_AUD_STREAM_FLAGS_PCM32_DECODE:
            sample_size = 4;
            break;
        case OBOS_AUD_STREAM_FLAGS_PCM24_DECODE:
            sample_size = 3;
            break;
    }

    size_t buffer_size = stream_info.target_sample_rate * stream_info.input_channels * (sample_size) * 10;
    aud_data_payload *payload = malloc(buffer_size+sizeof(aud_data_payload));
    if (!payload)
        abort();
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
        free(reply.payload);
    }
    if (avail < 0)
        perror("read");
    free(payload);

    die:
    autrans_disconnect(socket, client_id);

    shutdown(socket, SHUT_RDWR);
    close(socket);
    return 0;
}