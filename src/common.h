#pragma once

#define ALIGN_UP(addr, align) { \
    const __typeof__(addr) remainder = (addr) % (align); \
    (addr) += remainder ? (align) - remainder : 0; \
}

#define powerof2(x) ({ \
    const __typeof__(x) x_once = (x); \
    x_once && (x_once & (x_once - 1)) == 0; \
})

#define countof(arr) (sizeof(arr) / sizeof(*(arr)))
#define likely(exp) __builtin_expect(!!(exp), 1)
#define unlikely(exp) __builtin_expect(!!(exp), 0)
