#ifndef __DMA_UTILS_H_
#define __DMA_UTILS_H_

#define __DEBUG_DMAC__
#ifdef __DEBUG_DMAC__
#define debug_dmac printf
#else
#define debug_dmac(...)
#endif

#include <stdint.h>

#define dmac_assert(cond, ret)   \
    do {                         \
        if ((cond)) return ret;  \
    } while (0)                  \


#define dmac_assert_msg(cond, ret, ...) \
    do {                                \
        if ((cond))                     \
        {                               \
            debug_dmac(__VA_ARGS__);    \
            return ret;                 \
        }                               \
                                        \
    } while (0)                         \

static inline volatile uint64_t *
reg_read_32(volatile struct dw_uart_regs *uart, uintptr_t off)
{
    return (volatile uint64_t *)((uintptr_t)uart + off);
}

static inline volatile uint64_t *
reg_read_64(volatile struct dw_uart_regs *uart, uintptr_t off)
{
    return (volatile uint64_t *)((uintptr_t)uart + off);
}

#endif