/*
 * src/server_main.c
 *
 * Copyright (c) 2025 Omar Berrow
 */

#define _GNU_SOURCE 1

#include <obos-aud/trans.h>
#include <obos-aud/compiler.h>

#include <obos-aud/priv/con.h>
#include <obos-aud/priv/mixer.h>

#include <strings.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>

#include <sys/poll.h>
#include <sys/stat.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

static const char* const usage = "%s [-l connection_mode] [-n connection_mode] [-a address] [-m unix_socket_mode] [-d] [-q]\n'connection_mode' can be either tcp or unix.\n";

struct packet_node {
    aud_packet pckt;
    size_t poll_fd_idx;
    int fd;
    struct packet_node *next, *prev;
};
struct {
    struct packet_node *head, *tail;
    pthread_mutex_t mutex;
} g_packet_queue = {
    .mutex = PTHREAD_MUTEX_INITIALIZER
};

static struct packet_node* receive_packet(int fd);
static struct packet_node* pop_packet();

static void quit(int s)
{
    exit(0);
}

static const char* s_unix_socket_filename = NULL;
static void remove_unix_socket()
{
    remove(s_unix_socket_filename);
}

int main(int argc, char** argv)
{
    int opt = 0;

    const char* bind_address = "0.0.0.0";
    bool tcp_listen = true;
    bool unix_listen = true;
    int unix_socket_mode = 0777;
    bool daemonize = false;
    bool quiet = false;

    while ((opt = getopt(argc, argv, "hl:n:m:aqd")) != -1)
    {
        switch (opt)
        {
            case 'a':
                bind_address = optarg;
                break;
            case 'l':
            {
                if (strcasecmp(optarg, "tcp") == 0)
                    tcp_listen = true;
                else if (strcasecmp(optarg, "unix") == 0)
                    unix_listen = true;
                else
                {
                    fprintf(stderr, usage, argv[0]);
                    return -1;
                }
                break;
            }
            case 'n':
            {
                if (strcasecmp(optarg, "tcp") == 0)
                    tcp_listen = false;
                else if (strcasecmp(optarg, "unix") == 0)
                    unix_listen = false;
                else
                {
                    fprintf(stderr, usage, argv[0]);
                    return -1;
                }
                break;
            }
            case 'm':
            {
                errno = 0;
                unix_socket_mode = strtol(optarg, NULL, 8);
                if (errno != 0)
                {
                    fputs("Invalid mode!\n", stderr);
                    fprintf(stderr, usage, argv[0]);
                    return -1;
                }
                break;
            }
            case 'd': daemonize = true; break;
            case 'q': quiet = true; break;
            case 'h':
            default:
                fprintf(stderr, usage, argv[0]);
                return opt != 'h';
        }
    }

    if (daemonize)
        daemon(0, !quiet);
    else if (quiet)
    {
        close(0);
        close(1);
        close(2);
        int null = open("/dev/null", O_WRONLY);
        dup2(null, 1);
        dup2(null, 2);
        close(null);
        null = open("/dev/null", O_RDONLY);
        dup2(null, 0);
        close(null);
    }

    mixer_initialize();

    struct pollfd *fds = calloc(3, sizeof(struct pollfd));
    size_t nToPoll = 0;

    struct sockaddr_in ip_addr = {};
    if (inet_pton(AF_INET, bind_address, &ip_addr) != 1)
    {
        perror("inet_pton");
        return -1;
    }
    struct sockaddr_un unix_addr = {.sun_family=AF_UNIX};
    memcpy(unix_addr.sun_path, "/tmp/.obos-aud/U0", 18);
    int tcp_fd = -1;
    int unix_fd = -1;

    do if (tcp_listen)
    {
        struct pollfd* fd = &fds[nToPoll++];
        tcp_fd = fd->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (fd->fd == -1)
        {
            perror("socket(AF_INET, SOCK_STREAM)");
            nToPoll--;
            break;
        }
        ip_addr.sin_port = htons(OBOS_AUD_TCP_PORT);
        if (bind(fd->fd, (struct sockaddr*)&ip_addr, sizeof(ip_addr)) != 0)
        {
            perror("tcp bind");
            nToPoll--;
            break;
        }
        if (listen(fd->fd, 128) != 0)
        {
            perror("listen");
            nToPoll--;
            break;
        }
        fd->events |= POLLIN;
    } while(0);
    do if (unix_listen)
    {
        struct pollfd* fd = &fds[nToPoll++];
        mode_t old_mask = umask(0);
        mkdir("/tmp/.obos-aud", unix_socket_mode);
        unix_fd = fd->fd = socket(AF_UNIX, SOCK_STREAM, IPPROTO_IP);
        if (fd->fd == -1)
        {
            perror("socket(AF_UNIX, SOCK_STREAM)");
            nToPoll--;
            break;
        }
        if (bind(fd->fd, (struct sockaddr*)&unix_addr, sizeof(unix_addr)) != 0)
        {
            perror("unix bind");
            nToPoll--;
            break;
        }
        if (listen(fd->fd, 128) != 0)
        {
            perror("listen");
            nToPoll--;
            break;
        }
        chmod(unix_addr.sun_path, unix_socket_mode);
        umask(old_mask);
        fd->events |= POLLIN;
    } while(0);

    if (!nToPoll)
    {
        fprintf(stderr, "Nothing to listen on. Exiting.\n");
        return -1;
    }
    
    s_unix_socket_filename = unix_addr.sun_path;
    if (unix_listen)
        atexit(remove_unix_socket);
    signal(SIGINT, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGQUIT, quit);
    signal(SIGTERM, quit);

    aud_packet ok_status = {
        .opcode = OBOS_AUD_STATUS_REPLY_OK,
        .client_id = 0,
        .payload = 0,
        .payload_len = 0,
        .transmission_id = 0,
        .transmission_id_valid = true,
    };
    aud_packet unsupported_status = {
        .opcode = OBOS_AUD_STATUS_REPLY_UNSUPPORTED,
        .client_id = 0,
        .payload = 0,
        .payload_len = 0,
        .transmission_id = 0,
        .transmission_id_valid = true,
    };

    // Main server loop
    while (1)
    {
        int e = TEMP_FAILURE_RETRY(poll(fds, nToPoll, -1));
        if (e < 0)
        {
            perror("poll");
            break;
        }

        for (int i = 0; i < nToPoll; i++)
        {
            if (!fds[i].revents)
                continue;
            if (fds[i].revents & POLLERR || fds[i].revents & POLLNVAL || fds[i].revents & POLLHUP)
            {
                obos_aud_process_disconnect(obos_aud_get_client_by_fd(fds[i].fd), NULL);
                if (i != (nToPoll - 1))
                    fds[i].fd = -fds[i].fd;
                else
                    fds = realloc(fds, --nToPoll * sizeof(*fds));
                continue;
            }
            if (fds[i].revents & POLLIN)
            {
                struct packet_node* node = NULL;
                if (fds[i].fd == tcp_fd || fds[i].fd == unix_fd)
                {
                    int new_fd = accept(fds[i].fd, NULL, NULL);
                    if (new_fd == -1)
                    {
                        perror("accept");
                        continue;
                    }
                    nToPoll++;
                    fds = realloc(fds, nToPoll*sizeof(*fds));
                    memset(&fds[nToPoll-1], 0, sizeof(fds[nToPoll-1]));
                    fds[nToPoll-1].fd = new_fd;
                    fds[nToPoll-1].events = POLLIN;
                }
                else if (!(node = receive_packet(fds[i].fd)))
                {
                    obos_aud_connection* con = obos_aud_get_client_by_fd(fds[i].fd);
                    obos_aud_process_disconnect(con, NULL);
                    close(fds[i].fd);
                    shutdown(fds[i].fd, SHUT_RDWR);
                    if (i != (nToPoll - 1))
                        fds[i].fd = -fds[i].fd;
                    else
                        fds = realloc(fds, --nToPoll * sizeof(*fds));
                }
                if (node)
                    node->poll_fd_idx = i;
            }
        }

        struct packet_node* curr = NULL;
        while((curr = pop_packet()))
        {
            obos_aud_connection* con = NULL;
            if (curr->pckt.opcode != OBOS_AUD_INITIAL_CONNECTION_REQUEST)
            {
                con = obos_aud_get_client(curr->fd, curr->pckt.client_id);
                if (!con)
                {
                    aud_packet resp = {};
                    resp.opcode = OBOS_AUD_STATUS_REPLY_DISCONNECTED;
                    resp.client_id = curr->pckt.client_id;
                    resp.payload = "Client never seen";
                    resp.payload_len = 18;
                    resp.transmission_id = curr->pckt.transmission_id;
                    resp.transmission_id_valid = true;
                    autrans_transmit(curr->fd, &resp);

                    // Invalid connection
                    shutdown(curr->fd, SHUT_RDWR);
                    close(curr->fd);
                    free(curr);
                    continue;
                }
            }
            
            switch (curr->pckt.opcode) {
                case OBOS_AUD_INITIAL_CONNECTION_REQUEST:
                    con = obos_aud_process_initial_connection_request(curr->fd, &curr->pckt);
                    break;

                case OBOS_AUD_NOP:
                    ok_status.client_id = con->client_id;
                    ok_status.transmission_id = curr->pckt.transmission_id;
                    autrans_transmit(curr->fd, &ok_status);
                    break;

                case OBOS_AUD_DISCONNECT_REQUEST:
                    obos_aud_process_disconnect(con, &curr->pckt);
                    if (curr->poll_fd_idx != (nToPoll - 1))
                        fds[curr->poll_fd_idx].fd = -fds[curr->poll_fd_idx].fd;
                    else
                        fds = realloc(fds, --nToPoll * sizeof(*fds));
                    break;

                case OBOS_AUD_OPEN_STREAM:
                    obos_aud_process_stream_open(con, &curr->pckt);
                    break;
                case OBOS_AUD_CLOSE_STREAM:
                    obos_aud_process_stream_close(con, &curr->pckt);
                    break;
                case OBOS_AUD_QUERY_OUTPUT_DEVICE:
                    obos_aud_process_output_device_query(con, &curr->pckt);
                    break;
                case OBOS_AUD_OUTPUT_SET_BUFFER_SAMPLES:
                    obos_aud_process_output_set_buffer_samples(con, &curr->pckt);
                    break;
                case OBOS_AUD_DATA:
                    obos_aud_process_data(con, &curr->pckt);
                    break;
                case OBOS_AUD_STREAM_SET_FLAGS:
                    obos_aud_process_stream_set_flags(con, &curr->pckt);
                    break;
                case OBOS_AUD_STREAM_GET_FLAGS:
                    obos_aud_process_stream_get_flags(con, &curr->pckt);
                    break;
                case OBOS_AUD_STREAM_SET_VOLUME:
                    obos_aud_process_stream_set_volume(con, &curr->pckt);
                    break;
                case OBOS_AUD_STREAM_GET_VOLUME:
                    obos_aud_process_stream_get_volume(con, &curr->pckt);
                    break;
                case OBOS_AUD_CONNECTION_GET_VOLUME:
                    obos_aud_process_conn_get_volume(con, &curr->pckt);
                    break;
                case OBOS_AUD_CONNECTION_SET_VOLUME:
                    obos_aud_process_conn_set_volume(con, &curr->pckt);
                    break;
                case OBOS_AUD_OUTPUT_GET_VOLUME:
                    obos_aud_process_conn_get_volume(con, &curr->pckt);
                    break;
                case OBOS_AUD_OUTPUT_SET_VOLUME:
                    obos_aud_process_conn_set_volume(con, &curr->pckt);
                    break;
                case OBOS_AUD_SET_NAME:
                    obos_aud_process_set_name(con, &curr->pckt);
                    break;
                case OBOS_AUD_QUERY_CONNECTIONS:
                    obos_aud_process_query_connections(con, &curr->pckt);
                    break;
                case OBOS_AUD_QUERY_OUTPUT_PARAMETERS:
                    obos_aud_process_output_device_query_parameters(con, &curr->pckt);
                    break;

                case OBOS_AUD_STATUS_REPLY_OK:
                case OBOS_AUD_STATUS_REPLY_UNSUPPORTED:
                case OBOS_AUD_STATUS_REPLY_INVAL:
                case OBOS_AUD_STATUS_REPLY_DISCONNECTED:
                case OBOS_AUD_REQUEST_REPLY_BEGIN...OBOS_AUD_QUERY_OUTPUT_PARAMETERS_REPLY:
                    break;

                // Invalid opcode
                default:
                {
                    unsupported_status.client_id = con->client_id;
                    unsupported_status.transmission_id = curr->pckt.transmission_id;
                    autrans_transmit(curr->fd, &unsupported_status);
                    break;
                }
            }
            free(curr->pckt.payload);
            free(curr);
        }
    }

    // Cleanup
    free(fds);
    for (int i = 0; i < nToPoll; i++)
        close(fds[i].fd);
    if (unix_listen)
        remove(unix_addr.sun_path);

    return 0;
}

static struct packet_node* receive_packet(int fd)
{
    struct packet_node* node = calloc(1, sizeof(struct packet_node));
    if (autrans_receive(fd, &node->pckt, NULL, 0) != 0)
    {
        free(node);
        return NULL;
    }
    node->fd = fd;
    pthread_mutex_lock(&g_packet_queue.mutex);
    if (!g_packet_queue.head)
        g_packet_queue.head = node;
    if (g_packet_queue.tail)
        g_packet_queue.tail->next = node;
    node->prev = g_packet_queue.tail;
    g_packet_queue.tail = node;
    pthread_mutex_unlock(&g_packet_queue.mutex);
    return node;
}

static struct packet_node* pop_packet()
{
    pthread_mutex_lock(&g_packet_queue.mutex);
    struct packet_node* ret = g_packet_queue.head;
    if (!ret)
    {
        pthread_mutex_unlock(&g_packet_queue.mutex);
        return NULL;
    }
    g_packet_queue.head = ret->next;
    if (g_packet_queue.tail == ret)
        g_packet_queue.tail = NULL;
    else
        ret->next->prev = NULL;
    g_packet_queue.head = ret->next;
    pthread_mutex_unlock(&g_packet_queue.mutex);
    return ret;
}