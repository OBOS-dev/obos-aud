/*
 * src/server_main.c
 *
 * Copyright (c) 2025 Omar Berrow
 */

#define _GNU_SOURCE 1

#include <obos-aud/trans.h>
#include <obos-aud/priv/con.h>

#include <strings.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
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

static const char* const usage = "%s [-l connection_mode] [-n connection_mode] [-a address]\n'connection_mode' can be either tcp or unix.\n";

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

static void remove_unix_socket(int status, void* filename)
{
    remove(filename);
}

int main(int argc, char** argv)
{
    int opt = 0;
    long final_log_level = 2;

    const char* bind_address = "0.0.0.0";
    bool tcp_listen = true;
    bool unix_listen = true;

    while ((opt = getopt(argc, argv, "hl:n:a")) != -1)
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
            case 'h':
            default:
                fprintf(stderr, usage, argv[0]);
                return opt != 'h';
        }
    }

    struct pollfd *fds = calloc(3, sizeof(struct pollfd));
    size_t nToPoll = 0;

    struct sockaddr_in ip_addr = {};
    if (inet_pton(AF_INET, bind_address, &ip_addr) != 1)
    {
        perror("inet_pton");
        return -1;
    }
    struct sockaddr_un unix_addr = {.sun_family=AF_UNIX};
    mkdir("/tmp", 777);
    mkdir("/tmp/.obos-aud", 777);
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
        fd->events |= POLLIN;
    } while(0);

    if (!nToPoll)
    {
        fprintf(stderr, "Nothing to listen on. Exiting.\n");
        return -1;
    }
    
    if (unix_listen)
        on_exit(remove_unix_socket, unix_addr.sun_path);
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, quit);

    int empty_pipes[2] = {};
    pipe(empty_pipes);

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
            if (fds[i].revents & POLLERR)
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
                    fds[nToPoll-1].fd = new_fd;
                    fds[nToPoll-1].events = POLLIN;
                }
                else if (!(node = receive_packet(fds[i].fd)))
                {
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
                    ok_status.client_id = con->client_id;
                    ok_status.transmission_id = curr->pckt.transmission_id;
                    autrans_transmit(curr->fd, &ok_status);
                    obos_aud_process_disconnect(con, &curr->pckt);
                    if (curr->poll_fd_idx != (nToPoll - 1))
                        fds[curr->poll_fd_idx].fd = -fds[curr->poll_fd_idx].fd;
                    else
                        fds = realloc(fds, --nToPoll * sizeof(*fds));
                    break;

                case OBOS_AUD_OPEN_STREAM:
                case OBOS_AUD_DATA:
                case OBOS_AUD_QUERY_OUTPUT_DEVICE:
                    unsupported_status.client_id = con->client_id;
                    unsupported_status.transmission_id = curr->pckt.transmission_id;
                    autrans_transmit(curr->fd, &unsupported_status);
                    break;

                // Invalid opcode
                default:
                {
                    if (con)
                        obos_aud_process_disconnect(con, &curr->pckt);
                    else
                    {
                        shutdown(curr->fd, SHUT_RDWR);
                        close(curr->fd);
                    }
                    if (curr->poll_fd_idx != (nToPoll - 1))
                        fds[curr->poll_fd_idx].fd = -fds[curr->poll_fd_idx].fd;
                    else
                        fds = realloc(fds, --nToPoll * sizeof(*fds));
                    break;
                }
            }
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