/*
 * Copyright (c) 2016-2017,2019 Jason Graham <jgraham@compukix.net>
 *
 * This file is part of libsockets.
 *
 * libsockets is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 3 of the License,
 * or (at your option) any later version.
 *
 * libsockets is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with libsockets.  If not, see
 * <https://www.gnu.org/licenses/>.
 */

#define _MULTI_THREADED
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <libsockets/sockets.h>

#include "data_file.h"
#include "global.h"

size_t data_size = 0;
size_t nelem     = 0;

char *server_name = NULL;
void *data        = NULL;

void *send_data(void *args_)
{
        ssize_t n;
        long int tid = (long int)args_;
        sock_client_t sock;
        data_file_t d;

        char buffer[256];
        char *msg;
        size_t msg_len;

        if (sock_client_ctor(&sock, server_name, PORTNO) < 0)
                perror("Unable to construct");
        if (sock_client_connect(&sock, 0) < 0) {
                sprintf(buffer, "Unable to connect to %s", server_name);
                perror(buffer);
                exit(errno);
        }

        d.size = data_size;

        sprintf(d.name, "data-%ld.bin", tid);
        memcpy(data, &d, sizeof(d));

        printf("thread %ld: writing %zd bytes to %s...\n", tid, sizeof(d) + d.size, d.name);

        if ((n = sock_client_send(&sock, data, sizeof(d) + d.size)) < 0) {
                sprintf(buffer, "thread %ld: unable to send data file", tid);
                perror(buffer);
                goto fini;
        }
        printf("thread %ld: %zd:%zd:required %zd sends.\n", tid, n, sizeof(d) + d.size + sizeof(sock_tcp_header_t),
               sock.ntrans);

        // memset(buffer,0,256);
        n = sock_client_recv(&sock, (void **)&msg, &msg_len);

        printf("thread %ld: recv %zd bytes: %s\n", tid, n, msg);

        // memset(buffer,0,256);
        n = sock_client_recv(&sock, (void **)&msg, &msg_len);
        printf("thread %ld: recv %zd bytes: %s\n", tid, n, msg);

        sock_client_dtor(&sock);

fini:

        pthread_exit(NULL);
}

int main(int argc, char *argv[])
{

        // sock_client_t sock[N];
        // data_file_t d;

        int i, j;

        size_t *v;
        long nthread = 0;
        pthread_t *threads;

        sock_client_t sock;

        nthread     = strtol(argv[1], NULL, 10);
        server_name = argv[2];
        data_size   = 1024 * strtol(argv[3], NULL, 10);

        nelem = data_size / sizeof(size_t);
        data  = malloc(data_size + sizeof(data_file_t));

        threads = calloc(nthread, sizeof(*threads));

        printf("data_size = %zd\n", data_size);
        v = (size_t *)((data_file_t *)data + 1);

        memset(data, 0, data_size + sizeof(data_file_t));
        for (j       = 0; j < nelem; j++)
                v[j] = j;

        for (i = 0; i < nthread; i++) {
                pthread_create(threads + i, NULL, send_data, (void *)i);
        }

        for (i = 0; i < nthread; i++)
                pthread_join(threads[i], NULL);

// Tell the server to shutdown
/* printf("Sending SIGTERM to server...\n"); */
/* if( sock_client_ctor( &sock, server_name, PORTNO ) < 0 ) { */
/* 	perror("Unable to construct client socket"); */
/* 	goto fini; */
/* } */
/* if( sock_client_connect( &sock, SOCK_OPTS_SIGTERM ) < 0 ) */
/* 	perror("Unable to send SIGTERM"); */

fini:

        free(threads);
        free(data);

        return 0;
}
