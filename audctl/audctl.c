/*
 * audctl/audctl.c
 *
 * Copyright (c) 2025 Omar Berrow
 *
 * An audio server manipulation utility
*/

#include <obos-aud/trans.h>

#include <unistd.h>
#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <sys/param.h>

const char* usage = "%s [-d server_uri] [-h] command...\n";

static void print_volume(float volume)
{
    printf("volume=%f%%\n|", volume);
    for (int i = 0; i < (int)(volume > 100 ? 99 : volume); i++)
        printf("#");
    for (int i = 0; i < (100-(int)volume); i++)
        printf("-");
    if (volume > 100)
        printf("*");
    printf("|\n");
}

static int query_output(int socket, uint32_t client_id, uint16_t output, aud_output_dev* output_info)
{
    do {
        aud_packet reply = {};
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
            memcpy(output_info, &payload->info, MIN(sizeof(*output_info), reply.payload_len));
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
        return -1;
    } while(0);
    return 0;
}

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

    uint32_t client_id = 0;

    int socket = server_uri ? autrans_open_uri(server_uri) : autrans_open();
    if (socket < 0)
        return -1;

    if (autrans_initial_connection_request(socket) < 0)
    {
        shutdown(socket, SHUT_RDWR);
        close(socket);
        perror("autrans_transmit");
        return -1;
    }

    aud_packet reply = {};
    if (autrans_receive(socket, &reply, NULL, 0) < 0)
    {
        shutdown(socket, SHUT_RDWR);
        close(socket);
        perror("autrans_receive");
        return -1;
    }
    client_id = reply.client_id;
    free(reply.payload);

    char* name = autrans_make_name(argv[0], true);
    autrans_set_name(socket, client_id, name);
    free(name);

    const char* command = argv[optind];
    const char* const* command_argv = (optind+1) < argc ? (const char**)&argv[optind+1] : NULL;
    size_t command_argc = argc - (optind+1);
    if (command_argc)
        assert(command_argv);
    for (size_t i = 0; i < command_argc; i++)
        assert(command_argv[i] != NULL);
    assert(command != NULL);

    if (strcasecmp(command, "output-set-volume") == 0)
    {
        if (command_argc < 1)
        {
            fprintf(stderr, "output-set-volume [output_id] volume\n");
            goto die;
        }
        uint16_t output_id = OBOS_AUD_DEFAULT_OUTPUT_DEV;
        float volume = 0;
        if (command_argc == 2)
        {
            errno = 0;
            output_id = strtoul(command_argv[0], NULL, 0);
            if (errno != 0)
            {
                fprintf(stderr, "Expected unsigned integer, got %s\n", command_argv[0]);
                goto die;
            }
        }
        errno = 0;
        volume = strtof(command_argv[command_argc == 2 ? 1 : 0], NULL);
        if (errno != 0 || volume < 0)
        {
            fprintf(stderr, "Expected floating point integer, got %s\n", command_argv[command_argc == 2 ? 1 : 0]);
            goto die;
        }

        if (autrans_output_set_volume(socket, client_id, output_id, volume) == 0)
            print_volume(volume);
    }
    else if (strcasecmp(command, "connection-set-volume") == 0)
    {
        if (command_argc < 2)
        {
            fprintf(stderr, "connection-set-volume connection_id volume\n");
            goto die;
        }
        uint16_t connection_id = 0;
        float volume = 0;
        errno = 0;
        connection_id = strtoul(command_argv[0], NULL, 0);
        if (errno != 0)
        {
            fprintf(stderr, "Expected unsigned integer, got %s\n", command_argv[0]);
            goto die;
        }
        volume = strtof(command_argv[command_argc == 2 ? 1 : 0], NULL);
        if (errno != 0 || volume < 0)
        {
            fprintf(stderr, "Expected floating point integer, got %s\n", command_argv[command_argc == 2 ? 1 : 0]);
            goto die;
        }

        if (autrans_connection_set_volume(socket, client_id, connection_id, volume) == 0)
            print_volume(volume);
    }
    else if (strcasecmp(command, "output-get-volume") == 0)
    {
        uint16_t output_id = OBOS_AUD_DEFAULT_OUTPUT_DEV;
        float volume = 0;
        if (command_argc == 1)
        {
            errno = 0;
            output_id = strtoul(command_argv[0], NULL, 0);
            if (errno != 0)
            {
                fprintf(stderr, "Expected unsigned integer, got %s\n", command_argv[0]);
                goto die;
            }
        }
        if (autrans_output_get_volume(socket, client_id, output_id, &volume) == 0)
            print_volume(volume);
    }
    else if (strcasecmp(command, "connection-get-volume") == 0)
    {
        if (command_argc < 1)
        {
            fprintf(stderr, "connection-get-volume connection_id\n");
            goto die;
        }
        uint16_t connection_id = 0;
        float volume = 0;
        errno = 0;
        connection_id = strtoul(command_argv[0], NULL, 0);
        if (errno != 0)
        {
            fprintf(stderr, "Expected unsigned integer, got %s\n", command_argv[0]);
            goto die;
        }

        if (autrans_connection_get_volume(socket, client_id, connection_id, &volume) == 0)
            print_volume(volume);
    }
    else if (strcasecmp(command, "get-connections") == 0)
    {
        struct aud_connection_desc* descs = NULL;
        size_t desc_count = 0;
        if (autrans_query_connections(socket, client_id, &descs, &desc_count) < 0)
            goto die;

        struct aud_connection_desc* curr = descs;
        for (size_t i = 0; i < desc_count; i++, curr = autrans_next_connection_desc(curr))
            printf("connection 0x%x \"%.*s\"%s\n", curr->client_id, (int)(curr->sizeof_desc - sizeof(*curr)), curr->name, curr->client_id == client_id ? " (us)" : "");

        free(descs);
    }
    else if (strcasecmp(command, "output-query") == 0)
    {
        uint16_t output = OBOS_AUD_DEFAULT_OUTPUT_DEV;
        if (command_argc == 1)
        {
            errno = 0;
            output = strtoul(command_argv[0], NULL, 0);
            if (errno != 0)
            {
                fprintf(stderr, "Expected unsigned integer, got %s\n", command_argv[0]);
                goto die;
            }
        }

        aud_output_dev output_info = {};
        if (query_output(socket, client_id, output, &output_info) < 0)
            goto die;

        printf("Output 0x%x properties:\n", output_info.output_id);
        printf("  type: %s\n", aud_output_type_to_str[output_info.type]);
        printf("  color: %s\n", aud_output_color_to_str[output_info.type]);
        printf("  location: %s\n", aud_output_location_to_str[output_info.type]);
        printf("  flags: ");
        if (!output_info.flags)
            printf("N/A (0x0)\n");
        else
        {
            if (output_info.flags & OBOS_AUD_OUTPUT_FLAGS_DEFAULT)
                printf("DEFAULT");
            printf(" (0x%x)\n", output_info.flags);
        }
    }
    else if (strcasecmp(command, "output-query-parameters") == 0)
    {
        uint16_t output = OBOS_AUD_DEFAULT_OUTPUT_DEV;
        if (command_argc == 1)
        {
            errno = 0;
            output = strtoul(command_argv[0], NULL, 0);
            if (errno != 0)
            {
                fprintf(stderr, "Expected unsigned integer, got %s\n", command_argv[0]);
                goto die;
            }
        }

        if (output == OBOS_AUD_DEFAULT_OUTPUT_DEV)
        {
            aud_output_dev output_info = {};
            if (query_output(socket, client_id, output, &output_info) < 0)
                goto die;
            output = output_info.output_id;
        }

        aud_query_output_parameters_reply reply = {};
        if (autrans_query_output_parameters(socket, client_id, output, &reply) < 0)
            goto die;

        const char* format = "";
        switch (reply.params.format_size) {
            case 8: format = "PCM8"; break;
            case 16: format = "PCM16"; break;
            case 20: format = "PCM20"; break;
            case 24: format = "PCM24"; break;
            case 32: format = "PCM32"; break;
            default: format = "UNKNOWN";
        }

        printf("Output 0x%x parameters:\n", output);
        printf("  sample rate: %d\n", reply.params.sample_rate);
        printf("  output channels: %d\n", reply.params.channels);
        printf("  output stream format: %s\n", format);
        printf("  source channels: %d%s\n", reply.input_channels, reply.input_channels ? "" : " (idle)");
        printf("  buffer size (samples): %d\n", reply.buffer_samples);
        printf("  buffer size (seconds): %f\n", reply.buffer_samples / (float)reply.params.channels / (float)reply.params.sample_rate);
        printf("  volume: %f%%\n", reply.volume);
    }
    else if (strcasecmp(command, "output-set-buffer-size-samples") == 0)
    {
        if (command_argc < 1)
        {
            fprintf(stderr, "output-set-buffer-size-samples [output_id] volume\n");
            goto die;
        }
        uint16_t output_id = OBOS_AUD_DEFAULT_OUTPUT_DEV;
        int32_t samples = 0;
        if (command_argc == 2)
        {
            errno = 0;
            output_id = strtoul(command_argv[0], NULL, 0);
            if (errno != 0)
            {
                fprintf(stderr, "Expected unsigned integer, got %s\n", command_argv[0]);
                goto die;
            }
        }
        errno = 0;
        samples = strtol(command_argv[command_argc == 2 ? 1 : 0], NULL, 0);
        if (errno != 0 || samples <= 0)
        {
            fprintf(stderr, "Expected integer greater than zero, got %s\n", command_argv[command_argc == 2 ? 1 : 0]);
            goto die;
        }

        aud_query_output_parameters_reply reply = {};
        if (autrans_query_output_parameters(socket, client_id, output_id, &reply) < 0)
            goto die;

        if (autrans_output_set_buffer_samples(socket, client_id, output_id, samples) < 0)
            goto die;

        printf("set buffer size to %d sample%c (%f seconds)\n", samples, samples != 1 ? 's' : '\0', samples / (float)reply.params.channels / (float)reply.params.sample_rate);
    }
    else if (strcasecmp(command, "output-set-buffer-size-seconds") == 0)
    {
        if (command_argc < 1)
        {
            fprintf(stderr, "output-set-buffer-size-seconds [output_id] seconds\n");
            goto die;
        }
        uint16_t output_id = OBOS_AUD_DEFAULT_OUTPUT_DEV;
        float time = 0;
        if (command_argc == 2)
        {
            errno = 0;
            output_id = strtoul(command_argv[0], NULL, 0);
            if (errno != 0)
            {
                fprintf(stderr, "Expected unsigned integer, got %s\n", command_argv[0]);
                goto die;
            }
        }
        errno = 0;
        time = strtof(command_argv[command_argc == 2 ? 1 : 0], NULL);
        if (errno != 0 || time <= 0)
        {
            fprintf(stderr, "Expected floating point integer greater than zero, got %s\n", command_argv[command_argc == 2 ? 1 : 0]);
            goto die;
        }

        aud_query_output_parameters_reply reply = {};
        if (autrans_query_output_parameters(socket, client_id, output_id, &reply) < 0)
            goto die;

        int samples = time * (float)reply.params.channels * (float)reply.params.sample_rate;
        if (!samples)
            samples = 1;

        if (autrans_output_set_buffer_samples(socket, client_id, output_id, samples) < 0)
            goto die;

        printf("set buffer size to %d sample%c (%f seconds)\n", samples, samples != 1 ? 's' : '\0', time);
    }
    else if (strcasecmp(command, "set-default-output") == 0)
    {
        if (command_argc < 1)
        {
            fprintf(stderr, "set-default-output output_id\n");
            goto die;
        }

        uint16_t output_id = OBOS_AUD_DEFAULT_OUTPUT_DEV;
        errno = 0;
        output_id = strtoul(command_argv[0], NULL, 0);
        if (errno != 0)
        {
            fprintf(stderr, "Expected unsigned integer, got %s\n", command_argv[0]);
            goto die;
        }

        if (autrans_set_default_output(socket, client_id, output_id) == 0)
            printf("default output set to output 0x%x\n", output_id);
    }
    else if (strcasecmp(command, "help") == 0)
    {
        printf("Valid commands:\n");
        printf("  output-set-volume [output_id] volume: Sets an output's volume\n");
        printf("  connection-set-volume [output_id] volume: Sets a connection's volume\n");
        printf("  output-get-volume [output_id]: Retrieves an output's volume\n");
        printf("  connection-get-volume [output_id]: Retrieves a connection's volume\n");
        printf("  get-connections: Queries all connected processes and prints their names.\n");
        printf("  output-query [output_id]: Queries an output's properties.\n");
        printf("  output-query-parameters [output_id]: Queries an output's parameters.\n");
        printf("  output-set-buffer-size-samples [output_id] samples: Sets an output's buffer size in samples.\n");
        printf("  output-set-buffer-size-seconds [output_id] seconds: Sets an output's buffer size in seconds.\n");
        printf("  set-default-output output_id: Sets the server's default output.\n");
        printf("  help: Prints this message.\n");
    }
    else
    {
        fprintf(stderr, "Unknown command '%s'\n", command);
        goto die;
    }

    die:
    autrans_disconnect(socket, client_id);
    shutdown(socket, SHUT_RDWR);
    close(socket);
    return 0;
}