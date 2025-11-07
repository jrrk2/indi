
    /* Minimal libev stub for Windows compatibility */
    #ifndef EV_H
    #define EV_H

    #include <windows.h>

    typedef struct ev_io {
        int fd;
        void* data;
    } ev_io;

    typedef struct ev_timer {
        void* data;
    } ev_timer;

    typedef void (*ev_io_cb)(struct ev_loop *loop, struct ev_io *w, int revents);
    typedef void (*ev_timer_cb)(struct ev_loop *loop, struct ev_timer *w, int revents);

    struct ev_loop;

    /* Define stub functions */
    #define EV_READ 1
    #define EV_WRITE 2
    #define EV_TIMEOUT 4

    static inline struct ev_loop* ev_default_loop(unsigned int flags) { return NULL; }
    static inline void ev_io_init(ev_io* w, ev_io_cb cb, int fd, int events) {}
    static inline void ev_io_start(struct ev_loop* loop, ev_io* w) {}
    static inline void ev_io_stop(struct ev_loop* loop, ev_io* w) {}
    static inline void ev_timer_init(ev_timer* w, ev_timer_cb cb, double after, double repeat) {}
    static inline void ev_timer_start(struct ev_loop* loop, ev_timer* w) {}
    static inline void ev_timer_stop(struct ev_loop* loop, ev_timer* w) {}
    static inline void ev_run(struct ev_loop* loop, int flags) {}

    #endif /* EV_H */
    