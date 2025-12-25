/*
 * test/client_main.c
 *
 * Copyright (c) 2025 Omar Berrow
 */

 #define _GNU_SOURCE 1
 
#include <obos-aud/trans.h>

#include <unistd.h>
#include <stdio.h>

#include <sys/socket.h>

const char* usage = "%s [-d display_uri]\n";

int main(int argc, char** argv)
{
    int opt = 0;

    const char* server_uri = NULL;

    while ((opt = getopt(argc, argv, "hd:")) != -1)
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

    int socket = server_uri ? autrans_open_uri(server_uri) : autrans_open();
    if (socket < 0)
        return -1;

    autrans_initial_connection_request(socket);

    autrans_disconnect(socket);

    shutdown(socket, SHUT_RDWR);
    close(socket);
}