#ifndef COROUTINE_H
#define COROUTINE_H

#include <stddef.h>

/* coroutine status values */
enum {
    CR_BLOCKED = 0,
    CR_FINISHED = 1,
};

/* Helper macros to generate unique labels */
#define __cr_line3(name, line) _cr_##name##line
#define __cr_line2(name, line) __cr_line3(name, line)
#define __cr_line(name) __cr_line2(name, __LINE__)

struct cr {
    void *label;
    int status;
    void *local; /* private local storage */
};

#define cr_init()                           \
    {                                       \
        .label = NULL, .status = CR_BLOCKED \
    }
#define cr_begin(o)                     \
    do {                                \
        if ((o)->status == CR_FINISHED) \
            return;                     \
        if ((o)->label)                 \
            goto *(o)->label;           \
    } while (0)
#define cr_label(o, stat)                                   \
    do {                                                    \
        (o)->status = (stat);                               \
        __cr_line(label) : (o)->label = &&__cr_line(label); \
    } while (0)
#define cr_end(o) cr_label(o, CR_FINISHED)

#define cr_status(o) (o)->status

#define cr_wait(o, cond)         \
    do {                         \
        cr_label(o, CR_BLOCKED); \
        if (!(cond))             \
            return;              \
    } while (0)

#define cr_exit(o, stat)   \
    do {                   \
        cr_label(o, stat); \
        return;            \
    } while (0)

#define cr_queue(T, size) \
    struct {              \
        T buf[size];      \
        size_t r, w;      \
    }
#define cr_queue_init() \
    {                   \
        .r = 0, .w = 0  \
    }
#define cr_queue_len(q) (sizeof((q)->buf) / sizeof((q)->buf[0]))
#define cr_queue_cap(q) ((q)->w - (q)->r)
#define cr_queue_empty(q) ((q)->w == (q)->r)
#define cr_queue_full(q) (cr_queue_cap(q) == cr_queue_len(q))

#define cr_queue_push(q, el) \
    (!cr_queue_full(q) && ((q)->buf[(q)->w++ % cr_queue_len(q)] = (el), 1))
#define cr_queue_pop(q) \
    (cr_queue_empty(q) ? NULL : &(q)->buf[(q)->r++ % cr_queue_len(q)])

/* Wrap system calls and other functions that return -1 and set errno */
#define cr_sys(o, call)                                                     \
    cr_wait(o, (errno = 0) || !(((call) == -1) &&                           \
                                (errno == EAGAIN || errno == EWOULDBLOCK || \
                                 errno == EINPROGRESS || errno == EINTR)))

#endif