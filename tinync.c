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

static void stdin_loop(struct cr *o, byte_queue_t *out)
{
    static uint8_t b;
    static int r;
    cr_begin(o);
    for (;;) {
        cr_sys(o, r = read(STDIN_FILENO, &b, 1));
        if (r == 0) {
            cr_wait(o, cr_queue_empty(out));
            cr_exit(o, 1);
        }
        cr_wait(o, !cr_queue_full(out));
        cr_queue_push(out, b);
    }
    cr_end(o);
}

static void socket_write_loop(struct cr *o, int fd, byte_queue_t *in)
{
    static uint8_t *b;
    cr_begin(o);
    for (;;) {
        cr_wait(o, !cr_queue_empty(in));
        b = cr_queue_pop(in);
        cr_sys(o, send(fd, b, 1, 0));
    }
    cr_end(o);
}

static void socket_read_loop(struct cr *o, int fd)
{
    static uint8_t b;
    static int r;
    cr_begin(o);
    for (;;) {
        cr_sys(o, r = recv(fd, &b, 1, 0));
        if (r == 0)
            cr_exit(o, 1);
        cr_sys(o, write(STDOUT_FILENO, &b, 1));
    }
    cr_end(o);
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

    struct cr cr_stdin = cr_init();
    struct cr cr_socket_read = cr_init();
    struct cr cr_socket_write = cr_init();
    byte_queue_t queue = cr_queue_init();

    while (cr_status(&cr_stdin) == CR_BLOCKED &&
           cr_status(&cr_socket_read) == CR_BLOCKED) {
        if (cr_queue_empty(&queue)) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(STDIN_FILENO, &fds);
            FD_SET(fd, &fds);
            select(fd + 1, &fds, NULL, NULL, NULL);
        }
        socket_read_loop(&cr_socket_read, fd);
        socket_write_loop(&cr_socket_write, fd, &queue);
        stdin_loop(&cr_stdin, &queue);
    }

    close(fd);
    return 0;
}