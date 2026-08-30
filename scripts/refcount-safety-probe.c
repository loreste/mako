#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdint.h>
#include <stdatomic.h>
#include <string.h>

#include "mako_rt.h"

int main(int argc, char **argv) {
    void *data = mako_rc_alloc(1);
    if (argc != 2) return 2;

    if (strcmp(argv[1], "overflow") == 0) {
        atomic_store_explicit(mako_rc_of(data), UINT32_MAX, memory_order_relaxed);
        mako_rc_retain(data);
        return 3;
    }
    if (strcmp(argv[1], "released-retain") == 0) {
        atomic_store_explicit(mako_rc_of(data), 0, memory_order_relaxed);
        mako_rc_retain(data);
        return 4;
    }
    if (strcmp(argv[1], "underflow") == 0) {
        atomic_store_explicit(mako_rc_of(data), 0, memory_order_relaxed);
        mako_rc_release(data);
        return 5;
    }
    return 2;
}
