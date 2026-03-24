#ifndef TERMOB_PANIC_H
#define TERMOB_PANIC_H

#include <stdint.h>

#include "fault.h"

__attribute__((noreturn)) void kernel_panic_simple(const char* reason, const char* detail);
__attribute__((noreturn)) void kernel_panic_assertion(const char* expression,
                                                      const char* file,
                                                      uint32_t line);
__attribute__((noreturn)) void kernel_panic_exception(const char* exception_name,
                                                      const interrupt_frame_t* frame);

#define KASSERT(expr)                                                           \
    do {                                                                        \
        if (!(expr)) {                                                          \
            kernel_panic_assertion(#expr, __FILE__, (uint32_t)__LINE__);        \
        }                                                                       \
    } while (0)

#endif
