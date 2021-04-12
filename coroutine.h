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

#define cr_context_name(name) cr_context_##name

#define cr_context(name) struct cr cr_context_name(name)

#define cr_context_init(loc)    \
    {                           \
        .label = NULL,          \
        .status = CR_BLOCKED,   \
        .local = loc            \
    }
#define cr_begin()                          \
    do {                                    \
        if ((__ct)->status == CR_FINISHED)  \
            return;                         \
        if ((__ct)->label)                  \
            goto *(__ct)->label;            \
    } while (0)
#define cr_label(o, stat)                                   \
    do {                                                    \
        (o)->status = (stat);                               \
        __cr_line(label) : (o)->label = &&__cr_line(label); \
    } while (0)
#define cr_end() cr_label(__ct, CR_FINISHED)

#define cr_status(o) (o)->status

#define cr_wait(cond)               \
    do {                            \
        cr_label(__ct, CR_BLOCKED); \
        if (!(cond))                \
            return;                 \
    } while (0)

#define cr_exit(stat)           \
    do {                        \
        cr_label(__ct, stat);   \
        return;                 \
    } while (0)

#define cr_func_name(name) cr_func_##name

#define cr_func_def(name) \
    static void cr_func_name(name)(struct cr *__ct)

#define cr_run(name) cr_func_name(name)(&cr_context_name(name))

#define cr_track_until(name, cond) \
    (cr_status(&cr_context_name(name)) != (cond))

#define cr_grab_priv_var(type, memb) &((type*) (__ct->local))->memb

#define cr_priv_var(type) ((type *) (__ct->local))

#define cr_local static

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
#define cr_sys(call)                                                        \
    cr_wait((errno = 0) || !(((call) == -1) &&                              \
                                (errno == EAGAIN || errno == EWOULDBLOCK || \
                                 errno == EINPROGRESS || errno == EINTR)))

#endif