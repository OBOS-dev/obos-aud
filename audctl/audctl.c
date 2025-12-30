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

const char* usage = "%s [-d server_uri] [-h] command...";

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

    struct aud_packet reply = {};
    if (autrans_receive(socket, &reply, NULL, 0) < 0)
    {
        shutdown(socket, SHUT_RDWR);
        close(socket);
        perror("autrans_receive");
        return -1;
    }
    client_id = reply.client_id;

    autrans_disconnect(socket, client_id);
    shutdown(socket, SHUT_RDWR);
    close(socket);
    return 0;
}