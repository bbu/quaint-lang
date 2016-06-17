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

#ifdef COLOURLESS
#define COLOURED(s, b, c) s
#else
#define COLOURED(s, b, c) "\033[" #b ";" #c "m" s "\033[0m"
#endif

#define GRAY(s)   COLOURED(s, 0, 37)
#define RED(s)    COLOURED(s, 1, 31)
#define GREEN(s)  COLOURED(s, 1, 32)
#define YELLOW(s) COLOURED(s, 1, 33)
#define ORANGE(s) COLOURED(s, 1, 34)
#define CYAN(s)   COLOURED(s, 1, 36)
#define WHITE(s)  COLOURED(s, 1, 37)
