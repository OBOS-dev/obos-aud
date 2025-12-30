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
#include <stdlib.h>
#include <errno.h>

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

    const char* command = argv[optind];
    const char* const* command_argv = (optind+1) < argc ? (const char**)&argv[optind+1] : NULL;
    size_t command_argc = argc - (optind+1);

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
            fprintf(stderr, "Expected floating point integer, got %s\n", command_argv[0]);
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
            fprintf(stderr, "Expected floating point integer, got %s\n", command_argv[0]);
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