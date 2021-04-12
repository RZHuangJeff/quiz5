#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "coroutine.h"

typedef cr_queue(uint8_t, 4096) byte_queue_t;

typedef struct socket_write_local_s {
    byte_queue_t *in;
    int fd;
    uint8_t *b;
}swl_t;

typedef struct socket_read_local_s {
    int fd;
    int r;
    uint8_t b;
} srl_t;

cr_func_def(stdin_read)
{
    byte_queue_t *q = cr_priv_var(byte_queue_t);

    cr_local uint8_t b;
    cr_local int r;

    cr_begin();
    for (;;) {
        cr_sys(r = read(STDIN_FILENO, &b, 1));
        if (r == 0) {
            cr_wait(cr_queue_empty(q));
            cr_exit(CR_FINISHED);
        }

        cr_wait(!cr_queue_full(q));
        cr_queue_push(q, b);
    }
    cr_end();
}

cr_func_def(socket_write)
{
    byte_queue_t *in = *cr_grab_priv_var(swl_t, in);
    int fd = *cr_grab_priv_var(swl_t, fd);

    cr_local uint8_t *b;

    cr_begin();
    for (;;) {
        cr_wait(!cr_queue_empty(in));
        b = cr_queue_pop(in);
        cr_sys(send(fd, b, 1, 0));
    }
    cr_end();
}

cr_func_def(socket_read)
{
    int fd = *cr_priv_var(int);

    cr_local uint8_t b;
    cr_local int r;

    cr_begin();
    for (;;) {
        cr_sys(r = recv(fd, &b, 1, 0));
        if (r == 0)
            cr_exit(CR_FINISHED);
        cr_sys(write(STDOUT_FILENO, &b, 1));
    }
    cr_end();
}

static int nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "USAGE: %s <ip> <port>\n", argv[0]);
        return 1;
    }

    char *host = argv[1];
    int port = atoi(argv[2]);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket()");
        return 1;
    }
    if (nonblock(fd) < 0) {
        perror("nonblock() socket");
        return 1;
    }
    if (nonblock(STDIN_FILENO) < 0) {
        perror("nonblock() stdin");
        return 1;
    }
    if (nonblock(STDOUT_FILENO) < 0) {
        perror("nonblock() stdout");
        return 1;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr =
            {
                .s_addr = inet_addr(host),
            },
        .sin_port = htons(port),
    };
    connect(fd, (struct sockaddr *) &addr, sizeof(struct sockaddr_in));

    byte_queue_t queue = cr_queue_init();
    swl_t socket_write_local = {
        .fd = fd,
        .in = &queue
    };

    cr_context(stdin_read) = cr_context_init(&queue);
    cr_context(socket_read) = cr_context_init(&fd);
    cr_context(socket_write) = cr_context_init(&socket_write_local);

    while (cr_track_until(stdin_read, CR_FINISHED) &&
           cr_track_until(socket_read, CR_FINISHED)) {
        if (cr_queue_empty(&queue)) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(STDIN_FILENO, &fds);
            FD_SET(fd, &fds);
            select(fd + 1, &fds, NULL, NULL, NULL);
        }
        cr_run(socket_read);
        cr_run(socket_write);
        cr_run(stdin_read);
    }

    close(fd);
    return 0;
}