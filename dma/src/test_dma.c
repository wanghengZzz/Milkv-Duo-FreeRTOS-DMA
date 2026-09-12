/*
 * SG2002 DMA HAL
 * Copyright (C) 2026 wanghengZzz.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hal_dw_lli.h"
#include "top_reg.h"
#include "intr_conf.h"
#include "mmio.h"
#include "hal_uart_dw.h"
#include "hal_dw_utils.h"
#include "hal_dw_dma.h"

enum DMA_OPTION {

    DMA_MEM_MEM_POLLING_BA,
    DMA_MEM_MEM_INT_BA,
    DMA_DEV_DEV_POLLING_BA,
    DMA_DEV_DEV_INT_BA,
    DMA_MEM_DEV_POLLING_BA,
    DMA_MEM_DEV_INT_BA,
    DMA_DEV_MEM_POLLING_BA,
    DMA_DEV_MEM_INT_BA,

    DMA_MEM_MEM_POLLING_LIST,
    DMA_MEM_MEM_INT_LIST,
    DMA_DEV_DEV_POLLING_LIST,
    DMA_DEV_DEV_INT_LIST,
    DMA_MEM_DEV_POLLING_LIST,
    DMA_MEM_DEV_INT_LIST,
    DMA_DEV_MEM_POLLING_LIST,
    DMA_DEV_MEM_INT_LIST,
};

typedef struct dma_params dma_params;

struct dma_params {
    enum DMA_OPTION opt : 8;
    unsigned char burst;
    unsigned char width;
    unsigned char ch;
} __attribute__((packed));

extern void flush_dcache_range(uintptr_t start, size_t size);
extern void inv_dcache_range(uintptr_t start, size_t size);


/* ============================================================
 * Benchmark configuration
 * ============================================================
 *
 * Size:
 *
 *     256 B
 *     512 B
 *     1 KB
 *     2 KB
 *     4 KB
 *     8 KB
 *     16 KB
 *     32 KB
 *     64 KB
 *
 * Width:
 *
 *     0x0 -> 8-bit
 *     0x1 -> 16-bit
 *     0x2 -> 32-bit
 *     0x3 -> 64-bit
 *
 * Burst:
 *
 *     0x0 ~ 0x3
 *
 * Total configurations:
 *
 *     9 sizes × 4 widths × 4 bursts = 144
 *
 * ============================================================
 */

#define DMA_TEST_BUF_SIZE   65536
#define DMA_TEST_ITER       1000


static uint8_t dma_src[DMA_TEST_BUF_SIZE];
static uint8_t dma_dst[DMA_TEST_BUF_SIZE];


/* ============================================================
 * Benchmark statistics
 * ============================================================
 */

typedef struct {
    uint64_t min;
    uint64_t max;
    uint64_t total;
} dma_bench_stat_t;


/**
 * @brief Initialize DMA benchmark statistics.
 *
 * @param[in,out] stat The benchmark statistics structure to initialize.
 */
static inline void dma_bench_stat_init(dma_bench_stat_t *stat)
{
    stat->min = UINT64_MAX;
    stat->max = 0;
    stat->total = 0;
}


/**
 * @brief Update DMA benchmark statistics with one measurement.
 *
 * @param[in,out] stat The benchmark statistics structure to update.
 * @param[in] cycles The measured cycle count.
 */
static inline void dma_bench_stat_update(dma_bench_stat_t *stat,
                                         uint64_t cycles)
{
    if (cycles < stat->min)
        stat->min = cycles;

    if (cycles > stat->max)
        stat->max = cycles;

    stat->total += cycles;
}


/* ============================================================
 * Read RISC-V cycle counter
 * ============================================================
 */

/**
 * @brief Read the RISC-V cycle counter.
 *
 * @return The current cycle counter value.
 */
static inline uint64_t dma_read_cycle(void)
{
    uint64_t cycle;

    asm volatile(
        "rdcycle %0"
        : "=r"(cycle)
    );

    return cycle;
}


/* ============================================================
 * DMA benchmark
 * ============================================================
 */

/**
 * @brief Run a DMA transfer repeatedly and collect timing statistics.
 *
 * @param[in] dst The destination buffer.
 * @param[in] src The source buffer.
 * @param[in] size The transfer size in bytes.
 * @param[in] width The DMA transfer width setting.
 * @param[in] burst The DMA burst setting.
 * @param[in] ch_idx The DMA channel index.
 * @param[in] iterations The number of benchmark iterations.
 * @return 0 when the benchmark completes successfully, otherwise a negative
 *         value.
 */
static int dma_benchmark(void *dst,
                         const void *src,
                         size_t size,
                         unsigned char width,
                         unsigned char burst,
                         unsigned int ch_idx,
                         unsigned int iterations)
{
    dma_bench_stat_t prepare_stat;
    dma_bench_stat_t dma_stat;
    dma_bench_stat_t total_stat;

    unsigned int i;

    dma_bench_stat_init(&prepare_stat);
    dma_bench_stat_init(&dma_stat);
    dma_bench_stat_init(&total_stat);


    /* --------------------------------------------------------
     * Verify configuration once before benchmark.
     * --------------------------------------------------------
     */

    struct dma_transfer_config config = {
        .dst = dst,
        .src = src,
        .llp = 0,
        .size = size,
        .width = width,
        .burst = burst,
        .ch_idx = ch_idx,
        .multblk_type = CONTIGUOUS,
        .type = DMA_TRANSFER_MEM_TO_MEM,
        .src_per = 0,
        .dst_per = 0,
        .src_req = 0,
        .dst_req = 0,
    };

    if (dma_prepare(&config) != 0) {

        debug_dmac("Initial DMA prepare failed\n");

        debug_dmac("  size  = %lx\n",
               (unsigned long)size);

        debug_dmac("  width = %x\n",
               width);

        debug_dmac("  burst = %x\n",
               burst);

        return -1;
    }

    dma_stop(ch_idx);


    /* ========================================================
     * Benchmark loop
     * ========================================================
     */

    for (i = 0; i < iterations; ++i)
    {
        uint64_t prepare_start;
        uint64_t prepare_end;

        uint64_t dma_start_cycle;
        uint64_t dma_end_cycle;

        uint64_t total_start;
        uint64_t total_end;

        uint64_t prepare_cycles;
        uint64_t dma_cycles;
        uint64_t total_cycles;


        /* ----------------------------------------------------
         * TOTAL start
         * ----------------------------------------------------
         */

        total_start = dma_read_cycle();


        /* ----------------------------------------------------
         * DMA preparation
         * ----------------------------------------------------
         */

        prepare_start = dma_read_cycle();

        struct dma_transfer_config config = {
            .dst = dst,
            .src = src,
            .llp = 0,
            .size = size,
            .width = width,
            .burst = burst,
            .ch_idx = ch_idx,
            .multblk_type = CONTIGUOUS,
            .type = DMA_TRANSFER_MEM_TO_MEM,
            .src_per = 0,
            .dst_per = 0,
            .src_req = 0,
            .dst_req = 0,
        };

        if (dma_prepare(&config) != 0) {

            debug_dmac("DMA prepare failed at iteration %d\n",
                   i);

            return -1;
        }

        prepare_end = dma_read_cycle();


        /* ----------------------------------------------------
         * DMA execution
         * ----------------------------------------------------
         */

        dma_start_cycle = dma_read_cycle();

        if (dma_start(ch_idx) != 0) {

            debug_dmac("DMA start failed at iteration %d\n",
                   i);

            return -1;
        }


        if (dma_wait(ch_idx) != 0) {

            debug_dmac("DMA wait failed at iteration %d\n",
                   i);

            dma_stop(ch_idx);
            return -1;
        }


        dma_end_cycle = dma_read_cycle();


        /* ----------------------------------------------------
         * End benchmark
         * ----------------------------------------------------
         */

        total_end = dma_end_cycle;


        prepare_cycles =
            prepare_end - prepare_start;

        dma_cycles =
            dma_end_cycle - dma_start_cycle;

        total_cycles =
            total_end - total_start;


        dma_bench_stat_update(&prepare_stat,
                              prepare_cycles);

        dma_bench_stat_update(&dma_stat,
                              dma_cycles);

        dma_bench_stat_update(&total_stat,
                              total_cycles);


        /*
         * Stop channel after each transfer.
         */
        dma_stop(ch_idx);
    }


    /* ========================================================
     * Calculate averages
     * ========================================================
     */

    uint64_t prepare_avg =
        prepare_stat.total / iterations;

    uint64_t dma_avg =
        dma_stat.total / iterations;

    uint64_t total_avg =
        total_stat.total / iterations;


    /* ========================================================
     * Calculate normalized throughput
     *
     * B/1000 cycles
     * ========================================================
     */

    unsigned long dma_b_per_1000_cycles =
        (dma_avg != 0) ?
        (unsigned long)(((uint64_t)size * 1000ULL) / dma_avg) :
        0;

    unsigned long total_b_per_1000_cycles =
        (total_avg != 0) ?
        (unsigned long)(((uint64_t)size * 1000ULL) / total_avg) :
        0;


    /* ========================================================
     * Print result
     * ========================================================
     */

    debug_dmac("\n");
    debug_dmac("========================================\n");
    debug_dmac("DMA BENCHMARK\n");
    debug_dmac("========================================\n");

    debug_dmac("size       = %lx bytes\n",
           (unsigned long)size);

    debug_dmac("width      = %x\n",
           width);

    debug_dmac("burst      = %x\n",
           burst);

    debug_dmac("iterations = %d\n",
           iterations);

    debug_dmac("\n");


    /* --------------------------------------------------------
     * PREPARE
     * --------------------------------------------------------
     */

    debug_dmac("[PREPARE]\n");

    debug_dmac("  min      = %lx cycles\n",
           (unsigned long)prepare_stat.min);

    debug_dmac("  avg      = %lx cycles\n",
           (unsigned long)prepare_avg);

    debug_dmac("  max      = %lx cycles\n",
           (unsigned long)prepare_stat.max);

    debug_dmac("\n");


    /* --------------------------------------------------------
     * DMA
     * --------------------------------------------------------
     */

    debug_dmac("[DMA]\n");

    debug_dmac("  min      = %lx cycles\n",
           (unsigned long)dma_stat.min);

    debug_dmac("  avg      = %lx cycles\n",
           (unsigned long)dma_avg);

    debug_dmac("  max      = %lx cycles\n",
           (unsigned long)dma_stat.max);

    debug_dmac("  B/1000cy = %lx\n",
           dma_b_per_1000_cycles);

    debug_dmac("\n");


    /* --------------------------------------------------------
     * TOTAL
     * --------------------------------------------------------
     */

    debug_dmac("[TOTAL]\n");

    debug_dmac("  min      = %lx cycles\n",
           (unsigned long)total_stat.min);

    debug_dmac("  avg      = %lx cycles\n",
           (unsigned long)total_avg);

    debug_dmac("  max      = %lx cycles\n",
           (unsigned long)total_stat.max);

    debug_dmac("  B/1000cy = %lx\n",
           total_b_per_1000_cycles);

    debug_dmac("========================================\n");

    return 0;
}


/* ============================================================
 * Verify DMA result
 * ============================================================
 */

/**
 * @brief Verify that the DMA destination matches the source buffer.
 *
 * @param[in] size The number of bytes to verify.
 * @param[in] width The DMA transfer width used by the test.
 * @param[in] burst The DMA burst setting used by the test.
 * @return 0 when the data matches, otherwise a negative value.
 */
static int dma_verify(size_t size,
                      unsigned char width,
                      unsigned char burst)
{
    size_t i;

    for (i = 0; i < size; ++i)
    {
        if (dma_src[i] != dma_dst[i])
        {
            debug_dmac("DMA BENCH FAIL: "
                   "size=0x%lx "
                   "width=0x%x "
                   "burst=0x%x "
                   "index=0x%lx "
                   "src=0x%x "
                   "dst=0x%x\n",
                   (unsigned long)size,
                   width,
                   burst,
                   (unsigned long)i,
                   dma_src[i],
                   dma_dst[i]);

            return -1;
        }
    }

    return 0;
}


/* ============================================================
 * DMA benchmark test
 *
 * Test matrix:
 *
 *                 WIDTH
 *
 *             0  1  2  3  4  5  6  7
 *
 * SIZE
 *
 * 256
 * 512
 * 1K
 * 2K
 * 4K
 * 8K
 * 16K
 * 32K
 * 64K
 *
 * For each size/width combination:
 *
 *             burst = 0, 1, 2, 3
 *
 * ============================================================
 */

/**
 * @brief Run the DMA size, width, and burst benchmark sweep.
 */
void dma_bench_test()
{
    unsigned int i;
    int ret;


    /* --------------------------------------------------------
     * Test sizes
     * --------------------------------------------------------
     */

    static const size_t test_sizes[] = {
        256,
        512,
        1024,
        2048,
        4096,
        8192,
        16384,
        32768,
        65536
    };


    /* --------------------------------------------------------
     * Test widths
     *
     * All possible 3-bit values.
     *
     * 0x0 = 8-bit
     * 0x1 = 16-bit
     * 0x2 = 32-bit
     * 0x3 = 64-bit
     * 0x4 = 128-bit
     * 0x5 = 256-bit
     * 0x6 = 512-bit
     * 0x7 = 1024-bit
     * --------------------------------------------------------
     */

    static const unsigned char test_widths[] = {
        0x0,
        0x1,
        0x2,
        0x3,
        0x4,
        0x5,
        0x6,
        0x7
    };


    unsigned int num_sizes =
        sizeof(test_sizes) / sizeof(test_sizes[0]);

    unsigned int num_widths =
        sizeof(test_widths) / sizeof(test_widths[0]);


    /* --------------------------------------------------------
     * Prepare source data.
     * --------------------------------------------------------
     */

    for (i = 0; i < DMA_TEST_BUF_SIZE; ++i)
        dma_src[i] = (uint8_t)(i & 0xff);


    for (i = 0; i < DMA_TEST_BUF_SIZE; ++i)
        dma_dst[i] = 0;


    /* --------------------------------------------------------
     * CPU cache -> memory
     * --------------------------------------------------------
     */

    flush_dcache_range(
        (uintptr_t)dma_src,
        DMA_TEST_BUF_SIZE
    );


    /* --------------------------------------------------------
     * Remove stale destination cache lines.
     * --------------------------------------------------------
     */

    inv_dcache_range(
        (uintptr_t)dma_dst,
        DMA_TEST_BUF_SIZE
    );


    /* --------------------------------------------------------
     * DMA initialization
     * --------------------------------------------------------
     */

    ret = dma_init();

    if (ret != 0)
    {
        debug_dmac("dma_init failed: %d\n", ret);
        return;
    }

    /* ========================================================
     * SIZE × WIDTH × BURST sweep
     * ========================================================
     */

    for (unsigned int s = 0;
         s < num_sizes;
         ++s)
    {
        size_t test_size = test_sizes[s];


        debug_dmac("\n");
        debug_dmac("\n");
        debug_dmac("########################################\n");
        debug_dmac("# SIZE = %lx bytes\n",
               (unsigned long)test_size);
        debug_dmac("########################################\n");


        for (unsigned int w = 0;
             w < num_widths;
             ++w)
        {
            unsigned char width = test_widths[w];


            debug_dmac("\n");
            debug_dmac("----------------------------------------\n");
            debug_dmac("WIDTH = 0x%x\n",
                   width);
            debug_dmac("----------------------------------------\n");


            /* ------------------------------------------------
             * Burst sweep
             * ------------------------------------------------
             */

            for (unsigned char burst = 0;
                 burst < 4;
                 ++burst)
            {
                debug_dmac("\n");
                debug_dmac("===== SIZE %lx / WIDTH 0x%x / "
                       "BURST 0x%x =====\n",
                       (unsigned long)test_size,
                       width,
                       burst);


                /* --------------------------------------------
                 * Clear destination
                 * --------------------------------------------
                 */

                memset(
                    dma_dst,
                    0,
                    test_size
                );


                inv_dcache_range(
                    (uintptr_t)dma_dst,
                    test_size
                );


                /* --------------------------------------------
                 * Benchmark
                 * --------------------------------------------
                 */

                ret = dma_benchmark(
                    dma_dst,
                    dma_src,
                    test_size,
                    width,
                    burst,
                    0,              /* channel 0 */
                    DMA_TEST_ITER
                );


                debug_dmac("AFTER dma_benchmark ret=%d\n",
                       ret);


                if (ret != 0)
                {
                    debug_dmac("BENCHMARK FAILED\n");

                    debug_dmac("size  = %lx\n",
                           (unsigned long)test_size);

                    debug_dmac("width = %x\n",
                           width);

                    debug_dmac("burst = %x\n",
                           burst);

                    return;
                }


                /* --------------------------------------------
                 * DMA -> CPU
                 *
                 * DMA modified destination.
                 * --------------------------------------------
                 */

                inv_dcache_range(
                    (uintptr_t)dma_dst,
                    test_size
                );


                /* --------------------------------------------
                 * Verify result
                 * --------------------------------------------
                 */

                ret = dma_verify(
                    test_size,
                    width,
                    burst
                );

                if (ret != 0)
                    return;


                debug_dmac("DMA DATA CHECK PASS\n");
            }
        }
    }


    debug_dmac("\n");
    debug_dmac("========================================\n");
    debug_dmac("DMA SIZE/WIDTH/BURST SWEEP DONE\n");
    debug_dmac("========================================\n");
}

/* ============================================================
 * DMA interrupt functional test
 * ============================================================
 */

#define DMA_INT_TEST_SIZE 4096

/**
 * @brief Test a memory-to-memory DMA transfer using interrupt completion.
 *
 * @param[in] params DMA test configuration parameters.
 * @return 0 when the test passes, otherwise a negative value.
 */
static int dma_mem_mem_int_test(dma_params *params)
{
    unsigned int i;
    int ret;

    unsigned char width;
    unsigned char burst;
    unsigned char ch;


    width = params->width;
    burst = params->burst;
    ch = params->ch;


    debug_dmac("\n");
    debug_dmac("========================================\n");
    debug_dmac("DMA INTERRUPT TEST\n");
    debug_dmac("========================================\n");

    debug_dmac("size  = %lx bytes\n",
           (unsigned long)DMA_INT_TEST_SIZE);

    debug_dmac("width = %x\n",
           width);

    debug_dmac("burst = %x\n",
           burst);

    debug_dmac("ch    = %x\n",
           ch);


    if (ch >= DW_DMA_CH_NUM)
    {
        debug_dmac("Invalid DMA channel\n");
        return -1;
    }


    /* --------------------------------------------------------
     * Prepare source / destination
     * --------------------------------------------------------
     */

    for (i = 0; i < DMA_INT_TEST_SIZE; ++i)
        dma_src[i] = (uint8_t)(i & 0xff);

    for (i = 0; i < DMA_INT_TEST_SIZE; ++i)
        dma_dst[i] = 0;


    /* --------------------------------------------------------
     * Cache
     * --------------------------------------------------------
     */

    flush_dcache_range(
        (uintptr_t)dma_src,
        DMA_INT_TEST_SIZE
    );

    inv_dcache_range(
        (uintptr_t)dma_dst,
        DMA_INT_TEST_SIZE
    );


    /* --------------------------------------------------------
     * DMA initialization
     * --------------------------------------------------------
     */

    ret = dma_init();

    if (ret != 0)
    {
        debug_dmac("dma_init failed: %d\n", ret);
        return -1;
    }


    /* --------------------------------------------------------
     * Interrupt initialization
     *
     * System DMA -> CPU2 -> PLIC IRQ25
     * --------------------------------------------------------
     */

    debug_dmac("Initializing DMA IRQ...\n");

    ret = dma_irq_init();

    if (ret != 0)
    {
        debug_dmac("dma_irq_init failed: %d\n", ret);
        return -1;
    }

    /* --------------------------------------------------------
     * Prepare DMA
     * --------------------------------------------------------
     */

    struct dma_transfer_config config = {
        .dst = dma_dst,
        .src = dma_src,
        .llp = 0,
        .size = DMA_INT_TEST_SIZE,
        .width = width,
        .burst = burst,
        .ch_idx = ch,
        .multblk_type = CONTIGUOUS,
        .type = DMA_TRANSFER_MEM_TO_MEM,
        .src_per = 0,
        .dst_per = 0,
        .src_req = 0,
        .dst_req = 0,
    };

    ret = dma_prepare(&config);


    if (ret != 0)
    {
        debug_dmac("dma_prepare failed\n");
        return -1;
    }


    /* --------------------------------------------------------
     * Enable DMA interrupt
     * --------------------------------------------------------
     */

    ret = dma_irq_enable(ch);

    if (ret != 0)
    {
        debug_dmac("dma_irq_enable failed\n");
        return -1;
    }


    /* --------------------------------------------------------
     * Start DMA
     * --------------------------------------------------------
     */

    debug_dmac("Starting DMA...\n");

    ret = dma_start(ch);

    if (ret != 0)
    {
        debug_dmac("dma_start failed: %d\n", ret);

        dma_irq_disable(ch);
        return -1;
    }

    /* --------------------------------------------------------
     * Wait for interrupt
     * --------------------------------------------------------
     */

    // debug_dmac("Waiting for DMA interrupt...\n");

    ret = dma_wait_irq(ch);
    // ret = dma_wait(ch);

    debug_dmac("dma_wait_irq ret = %d\n", ret);
    
    /* --------------------------------------------------------
     * Cleanup
     * --------------------------------------------------------
     */

    dma_stop(ch);
    dma_irq_disable(ch);


    if (ret != 0)
    {
        debug_dmac("DMA INTERRUPT TEST FAILED: ret != 0\n");
        // return -1;
    }


    /* --------------------------------------------------------
     * DMA -> CPU
     * --------------------------------------------------------
     */

    inv_dcache_range(
        (uintptr_t)dma_dst,
        DMA_INT_TEST_SIZE
    );

    

    /* --------------------------------------------------------
     * Verify
     * --------------------------------------------------------
     */

    for (i = 0; i < DMA_INT_TEST_SIZE; ++i)
    {
        if (dma_src[i] != dma_dst[i])
        {
            debug_dmac("DMA INTERRUPT DATA FAIL\n");

            debug_dmac("index = %lx\n",
                   (unsigned long)i);

            debug_dmac("src   = 0x%x\n",
                   dma_src[i]);

            debug_dmac("dst   = 0x%x\n",
                   dma_dst[i]);

            return -1;
        }
    }


    debug_dmac("DMA INTERRUPT DATA CHECK PASS\n");
    debug_dmac("DMA INTERRUPT TEST DONE\n");

    return 0;
}

/**
 * @brief Test a memory-to-memory DMA transfer using polling completion.
 *
 * @param[in] params DMA test configuration parameters.
 * @return 0 when the test passes, otherwise a negative value.
 */
static int dma_mem_mem_poll_test(dma_params *params)
{
    unsigned int i;
    int ret;

    unsigned char width;
    unsigned char burst;
    unsigned char ch;


    width = params->width;
    burst = params->burst;
    ch = params->ch;


    debug_dmac("\n");
    debug_dmac("========================================\n");
    debug_dmac("DMA POLL TEST\n");
    debug_dmac("========================================\n");

    debug_dmac("size  = %lx bytes\n",
           (unsigned long)DMA_INT_TEST_SIZE);

    debug_dmac("width = %x\n",
           width);

    debug_dmac("burst = %x\n",
           burst);

    debug_dmac("ch    = %x\n",
           ch);


    if (ch >= DW_DMA_CH_NUM)
    {
        debug_dmac("Invalid DMA channel\n");
        return -1;
    }


    /* --------------------------------------------------------
     * Prepare source / destination
     * --------------------------------------------------------
     */

    for (i = 0; i < DMA_INT_TEST_SIZE; ++i)
        dma_src[i] = (uint8_t)(i & 0xff);

    for (i = 0; i < DMA_INT_TEST_SIZE; ++i)
        dma_dst[i] = 0;


    /* --------------------------------------------------------
     * Cache
     * --------------------------------------------------------
     */

    flush_dcache_range(
        (uintptr_t)dma_src,
        DMA_INT_TEST_SIZE
    );

    inv_dcache_range(
        (uintptr_t)dma_dst,
        DMA_INT_TEST_SIZE
    );


    /* --------------------------------------------------------
     * DMA initialization
     * --------------------------------------------------------
     */

    ret = dma_init();

    if (ret != 0)
    {
        debug_dmac("dma_init failed: %d\n", ret);
        return -1;
    }

    /* --------------------------------------------------------
     * Prepare DMA
     * --------------------------------------------------------
     */

    struct dma_transfer_config config = {
        .dst = dma_dst,
        .src = dma_src,
        .llp = 0,
        .size = DMA_INT_TEST_SIZE,
        .width = width,
        .burst = burst,
        .ch_idx = ch,
        .multblk_type = CONTIGUOUS,
        .type = DMA_TRANSFER_MEM_TO_MEM,
        .src_per = 0,
        .dst_per = 0,
        .src_req = 0,
        .dst_req = 0,
    };

    ret = dma_prepare(&config);


    if (ret != 0)
    {
        debug_dmac("dma_prepare failed\n");
        return -1;
    }

    /* --------------------------------------------------------
     * Start DMA
     * --------------------------------------------------------
     */

    debug_dmac("Starting DMA...\n");

    ret = dma_start(ch);

    dmac_assert_msg(ret != 0, -1, "dma_start failed: %d\n", ret);

    /* --------------------------------------------------------
     * Wait for POLLING
     * --------------------------------------------------------
     */

    ret = dma_wait(ch);

    debug_dmac("dma_wait ret = %d\n", ret);
    
    /* --------------------------------------------------------
     * Cleanup
     * --------------------------------------------------------
     */

    dma_stop(ch);

    if (ret != 0)
    {
        debug_dmac("DMA INTERRUPT TEST FAILED: ret != 0\n");
        // return -1;
    }


    /* --------------------------------------------------------
     * DMA -> CPU
     * --------------------------------------------------------
     */

    inv_dcache_range(
        (uintptr_t)dma_dst,
        DMA_INT_TEST_SIZE
    );

    

    /* --------------------------------------------------------
     * Verify
     * --------------------------------------------------------
     */

    for (i = 0; i < DMA_INT_TEST_SIZE; ++i)
    {
        if (dma_src[i] != dma_dst[i])
        {
            debug_dmac("DMA POLLING DATA FAIL\n");

            debug_dmac("index = %lx\n",
                   (unsigned long)i);

            debug_dmac("src   = 0x%x\n",
                   dma_src[i]);

            debug_dmac("dst   = 0x%x\n",
                   dma_dst[i]);

            return -1;
        }
    }


    debug_dmac("DMA POLLING DATA CHECK PASS\n");
    debug_dmac("DMA POLLING TEST DONE\n");

    return 0;
}

/**
 * @brief Test a memory-to-memory linked-list DMA transfer using interrupts.
 *
 * @param[in] params DMA test configuration parameters.
 * @return 0 when the test passes, otherwise a negative value.
 */
static int dma_int_lli_test(dma_params *params)
{
    unsigned int i;
    int ret;

    unsigned char width;
    unsigned char burst;
    unsigned char ch;


    width = params->width;
    burst = params->burst;
    ch = params->ch;

    debug_dmac("\n");
    debug_dmac("========================================\n");
    debug_dmac("DMA INTERRUPT TEST\n");
    debug_dmac("========================================\n");

    debug_dmac("size  = %lx bytes\n",
           (unsigned long)DMA_INT_TEST_SIZE);

    debug_dmac("width = %x\n",
           width);

    debug_dmac("burst = %x\n",
           burst);

    debug_dmac("ch    = %x\n",
           ch);


    if (ch >= DW_DMA_CH_NUM)
    {
        debug_dmac("Invalid DMA channel\n");
        return -1;
    }


    /* --------------------------------------------------------
     * Prepare source / destination
     * --------------------------------------------------------
     */

    for (i = 0; i < DMA_INT_TEST_SIZE; ++i)
        dma_src[i] = (uint8_t)(i & 0xff);

    for (i = 0; i < DMA_INT_TEST_SIZE; ++i)
        dma_dst[i] = 0;


    /* --------------------------------------------------------
     * Cache
     * --------------------------------------------------------
     */

    flush_dcache_range(
        (uintptr_t)dma_src,
        DMA_INT_TEST_SIZE
    );

    inv_dcache_range(
        (uintptr_t)dma_dst,
        DMA_INT_TEST_SIZE
    );

    /* --------------------------------------------------------
     * DMA initialization
     * --------------------------------------------------------
     */

    ret = dma_init();

    if (ret != 0)
    {
        debug_dmac("dma_init failed: %d\n", ret);
        return -1;
    }


    /* --------------------------------------------------------
     * Interrupt initialization
     *
     * System DMA -> CPU2 -> PLIC IRQ25
     * --------------------------------------------------------
     */

    debug_dmac("Initializing DMA IRQ...\n");

    ret = dma_irq_init();

    if (ret != 0)
    {
        debug_dmac("dma_irq_init failed: %d\n", ret);
        return -1;
    }


    #define TEST_LLI_BUF_SIZE 256

    unsigned char n_nodes = DMA_INT_TEST_SIZE / TEST_LLI_BUF_SIZE;

    LIST_HEAD(head);

    for (unsigned char i = 0; i < n_nodes; ++i)
    {
        uint64_t ctl = dma_get_ctl(width, burst, DMA_TRANSFER_MEM_TO_MEM);
        uint32_t block_ts = dma_get_block_ts(TEST_LLI_BUF_SIZE, width);

        lli_new_add_tail(
            &head,
            &dma_src[i * TEST_LLI_BUF_SIZE],
            &dma_dst[i * TEST_LLI_BUF_SIZE],
            block_ts,
            ctl
        );
    }

    i = 0;
    for (struct list_head *pos = head.next; pos != &head; pos = pos->next)
    {
        struct dw_desc *desc =
            list_entry(pos, struct dw_desc, desc_node);

        flush_dcache_range(
            (uintptr_t)&desc->lli,
            sizeof(struct dma_lli)
        );
        debug_dmac("Node %d CTL: %016lx\n", i, desc->lli.ctl);
        i++;
    }

    /* --------------------------------------------------------
     * Prepare DMA
     * --------------------------------------------------------
     */

    struct dw_desc *desc = list_entry(head.next, struct dw_desc, desc_node);

    struct dma_transfer_config config = {
        .dst = 0,
        .src = 0,
        .llp = &desc->lli,
        .size = 0,
        .width = 0,
        .burst = 0,
        .ch_idx = ch,
        .multblk_type = LINK_LIST,
        .type = DMA_TRANSFER_MEM_TO_MEM,
        .src_per = 0,
        .dst_per = 0,
        .src_req = 0,
        .dst_req = 0,
    };

    ret = dma_prepare(&config);

    if (ret != 0)
    {
        debug_dmac("dma_prepare failed\n");
        return -1;
    }


    /* --------------------------------------------------------
     * Enable DMA interrupt
     * --------------------------------------------------------
     */

    ret = dma_irq_enable(ch);

    if (ret != 0)
    {
        debug_dmac("dma_irq_enable failed\n");
        return -1;
    }


    /* --------------------------------------------------------
     * Start DMA
     * --------------------------------------------------------
     */

    debug_dmac("Starting DMA...\n");

    ret = dma_start(ch);

    if (ret != 0)
    {
        debug_dmac("dma_start failed: %d\n", ret);

        dma_irq_disable(ch);
        return -1;
    }

    /* --------------------------------------------------------
     * Wait for interrupt
     * --------------------------------------------------------
     */

    ret = dma_wait_irq(ch);
    // ret = dma_wait(ch);

    debug_dmac("dma_wait_irq ret = %d\n", ret);
    
    /* --------------------------------------------------------
     * Cleanup
     * --------------------------------------------------------
     */

    dma_stop(ch);
    dma_irq_disable(ch);


    if (ret != 0)
    {
        debug_dmac("DMA INTERRUPT TEST FAILED: ret != 0\n");
        // return -1;
    }


    /* --------------------------------------------------------
     * DMA -> CPU
     * --------------------------------------------------------
     */

    inv_dcache_range(
        (uintptr_t)dma_dst,
        DMA_INT_TEST_SIZE
    );

    

    /* --------------------------------------------------------
     * Verify
     * --------------------------------------------------------
     */

    for (i = 0; i < DMA_INT_TEST_SIZE; ++i)
    {
        if (dma_src[i] != dma_dst[i])
        {
            debug_dmac("DMA INTERRUPT DATA FAIL\n");

            debug_dmac("index = %lx\n",
                   (unsigned long)i);

            debug_dmac("src   = 0x%x\n",
                   dma_src[i]);

            debug_dmac("dst   = 0x%x\n",
                   dma_dst[i]);

            return -1;
        }
    }


    debug_dmac("DMA INTERRUPT DATA CHECK PASS\n");
    debug_dmac("DMA INTERRUPT TEST DONE\n");

    return 0;
}

/**
 * @brief Test a memory-to-memory linked-list DMA transfer using polling.
 *
 * @param[in] params DMA test configuration parameters.
 * @return 0 when the test passes, otherwise a negative value.
 */
static int dma_poll_lli_test(dma_params *params)
{
    unsigned int i;
    int ret;

    unsigned char width;
    unsigned char burst;
    unsigned char ch;


    width = params->width;
    burst = params->burst;
    ch = params->ch;

    debug_dmac("\n");
    debug_dmac("========================================\n");
    debug_dmac("DMA POLLING TEST (LIST) \n");
    debug_dmac("========================================\n");

    debug_dmac("size  = %lx bytes\n",
           (unsigned long)DMA_INT_TEST_SIZE);

    debug_dmac("width = %x\n",
           width);

    debug_dmac("burst = %x\n",
           burst);

    debug_dmac("ch    = %x\n",
           ch);


    if (ch >= DW_DMA_CH_NUM)
    {
        debug_dmac("Invalid DMA channel\n");
        return -1;
    }


    /* --------------------------------------------------------
     * Prepare source / destination
     * --------------------------------------------------------
     */

    for (i = 0; i < DMA_INT_TEST_SIZE; ++i)
        dma_src[i] = (uint8_t)(i & 0xff);

    for (i = 0; i < DMA_INT_TEST_SIZE; ++i)
        dma_dst[i] = 0;


    /* --------------------------------------------------------
     * Cache
     * --------------------------------------------------------
     */

    flush_dcache_range(
        (uintptr_t)dma_src,
        DMA_INT_TEST_SIZE
    );

    inv_dcache_range(
        (uintptr_t)dma_dst,
        DMA_INT_TEST_SIZE
    );

    /* --------------------------------------------------------
     * DMA initialization
     * --------------------------------------------------------
     */

    ret = dma_init();

    if (ret != 0)
    {
        debug_dmac("dma_init failed: %d\n", ret);
        return -1;
    }

    #define TEST_LLI_BUF_SIZE 256

    unsigned char n_nodes = DMA_INT_TEST_SIZE / TEST_LLI_BUF_SIZE;

    LIST_HEAD(head);

    for (unsigned char i = 0; i < n_nodes; ++i)
    {
        uint64_t ctl = dma_get_ctl(width, burst, DMA_TRANSFER_MEM_TO_MEM);
        uint32_t block_ts = dma_get_block_ts(TEST_LLI_BUF_SIZE, width);

        lli_new_add_tail(
            &head,
            &dma_src[i * TEST_LLI_BUF_SIZE],
            &dma_dst[i * TEST_LLI_BUF_SIZE],
            block_ts,
            ctl
        );
    }

    i = 0;
    for (struct list_head *pos = head.next; pos != &head; pos = pos->next)
    {
        struct dw_desc *desc =
            list_entry(pos, struct dw_desc, desc_node);

        flush_dcache_range(
            (uintptr_t)&desc->lli,
            sizeof(struct dma_lli)
        );
        debug_dmac("Node %d CTL: %016lx\n", i, desc->lli.ctl);
        i++;
    }

    /* --------------------------------------------------------
     * Prepare DMA
     * --------------------------------------------------------
     */

    struct dw_desc *desc = list_entry(head.next, struct dw_desc, desc_node);

    struct dma_transfer_config config = {
        .dst = 0,
        .src = 0,
        .llp = &desc->lli,
        .size = 0,
        .width = 0,
        .burst = 0,
        .ch_idx = ch,
        .multblk_type = LINK_LIST,
        .type = DMA_TRANSFER_MEM_TO_MEM,
        .src_per = 0,
        .dst_per = 0,
        .src_req = 0,
        .dst_req = 0,
    };

    ret = dma_prepare(&config);

    if (ret != 0)
    {
        debug_dmac("dma_prepare failed\n");
        return -1;
    }

    /* --------------------------------------------------------
     * Start DMA
     * --------------------------------------------------------
     */

    debug_dmac("Starting DMA...\n");

    ret = dma_start(ch);

    if (ret != 0)
    {
        debug_dmac("dma_start failed: %d\n", ret);
        return -1;
    }

    /* --------------------------------------------------------
     * Wait for DMA
     * --------------------------------------------------------
     */

    ret = dma_wait(ch);

    debug_dmac("dma_wait ret = %d\n", ret);
    
    /* --------------------------------------------------------
     * Cleanup
     * --------------------------------------------------------
     */

    dma_stop(ch);

    if (ret != 0)
    {
        debug_dmac("DMA POLLING TEST FAILED (LIST): ret != 0\n");
        // return -1;
    }


    /* --------------------------------------------------------
     * DMA -> CPU
     * --------------------------------------------------------
     */

    inv_dcache_range(
        (uintptr_t)dma_dst,
        DMA_INT_TEST_SIZE
    );

    

    /* --------------------------------------------------------
     * Verify
     * --------------------------------------------------------
     */

    for (i = 0; i < DMA_INT_TEST_SIZE; ++i)
    {
        if (dma_src[i] != dma_dst[i])
        {
            debug_dmac("DMA DATA TEST FAIL\n");

            debug_dmac("index = %lx\n",
                   (unsigned long)i);

            debug_dmac("src   = 0x%x\n",
                   dma_src[i]);

            debug_dmac("dst   = 0x%x\n",
                   dma_dst[i]);

            return -1;
        }
    }


    debug_dmac("DMA POLLING DATA CHECK PASS\n");
    debug_dmac("DMA POLLING TEST DONE\n");

    return 0;
}

/* ============================================================
 * UART0 DMA TX test
 * ============================================================
 *
 * UART0 TX request line = 9.
 * CHx_DAR is fixed at UART0 THR while SAR increments through
 * the source buffer.
 */

#define UART_SDMAM_OFF  0x094
#define UART_SFE_OFF    0x098
#define UART_TFL_OFF    0x080
#define UART_STET_OFF   0x0A0
#define UART_HTX_OFF    0x0A4
#define UART_DM​​ASA_OFF  0x0A8
#define UART_USR_OFF    0x07c

static inline volatile uint32_t *
/**
 * @brief Get the address of a 32-bit UART register.
 *
 * @param[in] uart The UART register block.
 * @param[in] off The register offset.
 * @return The address of the selected UART register.
 */
uart_reg32(volatile struct dw_uart_regs *uart, uintptr_t off)
{
    return (volatile uint32_t *)((uintptr_t)uart + off);
}

// #define UART_LSR_TEMT           (1U << 6)


static volatile struct dw_uart_regs *uart = 0;

/**
 * @brief Initialize the selected UART for the DMA test.
 *
 * @param[in] baudrate The UART baud rate.
 * @param[in] uart_clock The UART input clock frequency in Hz.
 */
static void uart_init(int baudrate, int uart_clock)
{
	int divisor = uart_clock / (16 * baudrate);
	uart->lcr = uart->lcr | UART_LCR_DLAB | UART_LCR_8N1;
	uart->dll = divisor & 0xff;
	uart->dlm = (divisor >> 8) & 0xff;
	uart->lcr = uart->lcr & (~UART_LCR_DLAB);

	uart->ier = 0;
	uart->mcr = UART_MCRVAL;
	uart->fcr = UART_FCR_DEFVAL;

	uart->lcr = 3;
}

/**
 * @brief Test memory-to-UART TX DMA using interrupt completion.
 *
 * @param[in] dev_uart The UART device used by the test.
 * @param[in] ch_idx The DMA channel index.
 * @return 0 when the test passes, otherwise a negative value.
 */
static int dma_mem_uart_int_test(device_uart dev_uart, unsigned int ch_idx)
{
    static const uint8_t tx_data[] =
        "DMA MEM->UARTXXXXXXX TEST\r\n";

    const size_t tx_size = sizeof(tx_data) - 1;

    int ret;
    unsigned int req_line = DMA_TX_REQ_N_UART(dev_uart);

    if (ch_idx >= DW_DMA_CH_NUM) {
        return -1;
    }

	switch (dev_uart) {
		case UART0:
			uart = (struct dw_uart_regs *)UART0_BASE;
			break;
		case UART1:
			uart = (struct dw_uart_regs *)UART1_BASE;
			break;
		case UART2:
			uart = (struct dw_uart_regs *)UART2_BASE;
			break;
		case UART3:
			uart = (struct dw_uart_regs *)UART3_BASE;
			break;
		default:
			break;
	}

    /*
     * ---------------------------------------------------------
     * 1. Initialize UART FIRST
     * ---------------------------------------------------------
     */
    uart_init(115200, 25 * 1000 * 1000);

    /*
     * ---------------------------------------------------------
     * 2. Enable UART FIFO + DMA mode AFTER uart_init()
     * ---------------------------------------------------------
     */
    uart->fcr =
        UART_FCR_FIFO_EN |
        UART_FCR_DMA_MODE;

    /*
     * Disable the normal UART RX / THRE interrupt sources.
     * DMA TX is driven by the UART DMA request line instead.
     */
    uart->ier &= ~0x3U;

    ret = dma_init();

    if (ret != 0) {
        debug_dmac("dma_init failed\n");
    }

    debug_dmac("Initializing DMA IRQ...\n");

    ret = dma_irq_init();

    if (ret != 0)
    {
        debug_dmac("dma_irq_init failed: %d\n", ret);
        return -1;
    }

    debug_dmac("\n");
    debug_dmac("========================================\n");
    debug_dmac("DMA MEM -> UART%d TX TEST\n", dev_uart);
    debug_dmac("========================================\n");
    debug_dmac("channel = %d\n", ch_idx);
    debug_dmac("request = %d (UART%d TX)\n", req_line, dev_uart);

    /*
     * These are dedicated UART shadow/status registers, not FCR/IIR.
     */

    volatile uint32_t *sdmam = uart_reg32(uart, UART_SDMAM_OFF);
    volatile uint32_t *sfe   = uart_reg32(uart, UART_SFE_OFF);
    volatile uint32_t *tfl   = uart_reg32(uart, UART_TFL_OFF);
    volatile uint32_t *stet  = uart_reg32(uart, UART_STET_OFF);
    volatile uint32_t *htx   = uart_reg32(uart, UART_HTX_OFF);
    volatile uint32_t *usr   = uart_reg32(uart, UART_USR_OFF);

    debug_dmac("UART IER    = 0x%08x\n", uart->ier);
    debug_dmac("UART IIR    = 0x%08x\n", uart->iir);
    debug_dmac("UART LSR    = 0x%08x\n", uart->lsr);
    debug_dmac("UART LCR    = 0x%08x\n", uart->lcr);
    debug_dmac("UART SDMAM  = 0x%08x\n", *sdmam);
    debug_dmac("UART SFE    = 0x%08x\n", *sfe);
    debug_dmac("UART STET   = 0x%08x\n", *stet);
    debug_dmac("UART HTX    = 0x%08x\n", *htx);
    debug_dmac("UART TFL    = 0x%08x\n", *tfl);
    debug_dmac("BEFORE UART USR = 0x%0x\n", *usr);

    debug_dmac("UART TFL before DMA = 0x%08x\n",
           *(volatile uint32_t *)(uart + 0x080));

    while ((uart->lsr & UART_LSR_TEMT) == 0)
        ;

    /* Source buffer is read by DMA, so push it to memory first. */
    flush_dcache_range(
        (uintptr_t)tx_data,
        tx_size
    );

    struct dma_transfer_config config = {
        .dst = (void *)&uart->thr,
        .src = tx_data,
        .llp = 0,
        .size = tx_size,
        .width = 0x0,
        .burst = 0x0,
        .ch_idx = ch_idx,
        .multblk_type = CONTIGUOUS,
        .type = DMA_TRANSFER_MEM_TO_DEV,
        .src_per = 0,
        .dst_per = ch_idx,
        .src_req = 0,
        .dst_req = req_line,
    };

    /* Configure MEM -> DEV. */
    ret = dma_prepare(&config);
    dmac_assert_msg(ret != 0, ret, "DMA PREPARE TRANSFER ERROR\n");

    /* Enable channel interrupt BEFORE starting the DMA channel. */
    ret = dma_irq_enable(ch_idx);
    dmac_assert_msg(ret != 0, ret, "DMA IRQ ENABLE ERROR\n");

    /* Start the channel. */
    ret = dma_start(ch_idx);

    if (ret != 0)
    {
        dma_irq_disable(ch_idx);
        return ret;
    }

    /* Wait for ISR to report completion or error. */
    ret = dma_wait_irq(ch_idx);

    if (ret != 0)
    {
        debug_dmac("DMA WAIT IRQ ERROR\n");
    }

    dma_stop(ch_idx);
    dma_irq_disable(ch_idx);


    debug_dmac("AFTER UART USR = 0x%0x\n", *usr);
    if (ret != 0) {
        debug_dmac("DMA MEM->UART%d TX failed: %d\n", dev_uart, ret);
        debug_dmac("UART TFL after DMA error = 0x%08x\n",
               *(volatile uint32_t *)(uart + 0x080));

        return ret;
    }

    debug_dmac("UART TFL after DMA = 0x%08x\n",
           *(volatile uint32_t *)(uart + 0x080));

    /* DMA completion means data reached the UART TX FIFO.
     * Wait for the UART shift register to finish transmitting.
     */
    while ((uart->lsr & UART_LSR_TEMT) == 0)
        ;

    debug_dmac("DMA MEM->UART0 TX PASS\n");
    return 0;
}

/**
 * @brief Test memory-to-UART TX DMA with linked-list descriptors and interrupts.
 *
 * @param[in] dev_uart The UART device used by the test.
 * @param[in] ch_idx The DMA channel index.
 * @return 0 when the test passes, otherwise a negative value.
 */
static int dma_mem_uart_int_lli_test(device_uart dev_uart, unsigned int ch_idx)
{
    static const uint8_t tx_data[] =
        "DMA MEM->UARTXXXXXXX TEST\r\n";

    const size_t tx_size = sizeof(tx_data) - 1;
    // const size_t tx_size = 1;
    int ret;
    unsigned int req_line = DMA_TX_REQ_N_UART(dev_uart);

    if (ch_idx >= DW_DMA_CH_NUM) {
        return -1;
    }

	switch (dev_uart) {
		case UART0:
			uart = (struct dw_uart_regs *)UART0_BASE;
			break;
		case UART1:
			uart = (struct dw_uart_regs *)UART1_BASE;
			break;
		case UART2:
			uart = (struct dw_uart_regs *)UART2_BASE;
			break;
		case UART3:
			uart = (struct dw_uart_regs *)UART3_BASE;
			break;
		default:
			break;
	}

    /*
     * ---------------------------------------------------------
     * 1. Initialize UART FIRST
     * ---------------------------------------------------------
     */
    uart_init(115200, 25 * 1000 * 1000);

    /*
     * ---------------------------------------------------------
     * 2. Enable UART FIFO + DMA mode AFTER uart_init()
     * ---------------------------------------------------------
     */
    uart->fcr =
        UART_FCR_FIFO_EN |
        UART_FCR_DMA_MODE;

    /*
     * Disable the normal UART RX / THRE interrupt sources.
     * DMA TX is driven by the UART DMA request line instead.
     */
    uart->ier &= ~0x3U;

    ret = dma_init();

    if (ret != 0) {
        debug_dmac("dma_init failed\n");
    }

    debug_dmac("Initializing DMA IRQ...\n");

    ret = dma_irq_init();

    if (ret != 0)
    {
        debug_dmac("dma_irq_init failed: %d\n", ret);
        return -1;
    }

    debug_dmac("\n");
    debug_dmac("========================================\n");
    debug_dmac("DMA MEM -> UART%d TX TEST\n", dev_uart);
    debug_dmac("========================================\n");
    debug_dmac("channel = %d\n", ch_idx);
    debug_dmac("request = %d (UART%d TX)\n", req_line, dev_uart);

    /*
     * These are dedicated UART shadow/status registers, not FCR/IIR.
     */

    volatile uint32_t *sdmam = uart_reg32(uart, UART_SDMAM_OFF);
    volatile uint32_t *sfe   = uart_reg32(uart, UART_SFE_OFF);
    volatile uint32_t *tfl   = uart_reg32(uart, UART_TFL_OFF);
    volatile uint32_t *stet  = uart_reg32(uart, UART_STET_OFF);
    volatile uint32_t *htx   = uart_reg32(uart, UART_HTX_OFF);
    volatile uint32_t *usr   = uart_reg32(uart, UART_USR_OFF);

    debug_dmac("UART IER    = 0x%08x\n", uart->ier);
    debug_dmac("UART IIR    = 0x%08x\n", uart->iir);
    debug_dmac("UART LSR    = 0x%08x\n", uart->lsr);
    debug_dmac("UART LCR    = 0x%08x\n", uart->lcr);
    debug_dmac("UART SDMAM  = 0x%08x\n", *sdmam);
    debug_dmac("UART SFE    = 0x%08x\n", *sfe);
    debug_dmac("UART STET   = 0x%08x\n", *stet);
    debug_dmac("UART HTX    = 0x%08x\n", *htx);
    debug_dmac("UART TFL    = 0x%08x\n", *tfl);
    debug_dmac("BEFORE UART USR = 0x%0x\n", *usr);

    debug_dmac("UART TFL before DMA = 0x%08x\n",
           *(volatile uint32_t *)(uart + 0x080));

    while ((uart->lsr & UART_LSR_TEMT) == 0)
        ;

    /* Source buffer is read by DMA, so push it to memory first. */
    flush_dcache_range(
        (uintptr_t)tx_data,
        tx_size
    );

    #define DMA_TEST_SIZE 4

    unsigned char n_nodes = tx_size / DMA_TEST_SIZE + (tx_size & 1);

    LIST_HEAD(head);

    for (unsigned char i = 0; i < tx_size; i += DMA_TEST_SIZE)
    {
        uint64_t ctl = dma_get_ctl(0x0, 0x0, DMA_TRANSFER_MEM_TO_DEV);
        size_t data_size = (i + DMA_TEST_SIZE <= tx_size) ? DMA_TEST_SIZE : (tx_size % DMA_TEST_SIZE);
        uint32_t block_ts = dma_get_block_ts(data_size, 0x0);

        lli_new_add_tail(
            &head,
            &tx_data[i],
            (void *)&uart->thr,
            block_ts,
            ctl
        );
    }

    int i = 0;
    for (struct list_head *pos = head.next; pos != &head; pos = pos->next)
    {
        struct dw_desc *desc =
            list_entry(pos, struct dw_desc, desc_node);

        flush_dcache_range(
            (uintptr_t)&desc->lli,
            sizeof(struct dma_lli)
        );
        debug_dmac("Node %d CTL: %016lx\n", i, desc->lli.ctl);
        i++;
    }

    /* --------------------------------------------------------
     * Prepare DMA
     * --------------------------------------------------------
     */

    struct dw_desc *desc = list_entry(head.next, struct dw_desc, desc_node);

    struct dma_transfer_config config = {
        .dst = 0,
        .src = 0,
        .llp = &desc->lli,
        .size = 0,
        .width = 0,
        .burst = 0,
        .ch_idx = ch_idx,
        .multblk_type = LINK_LIST,
        .type = DMA_TRANSFER_MEM_TO_DEV,
        .src_per = 0,
        .dst_per = ch_idx,
        .src_req = 0,
        .dst_req = req_line,
    };

    ret = dma_prepare(&config);

    if (ret != 0)
    {
        debug_dmac("dma_prepare failed\n");
        return -1;
    }

    /* --------------------------------------------------------
     * Enable DMA interrupt
     * --------------------------------------------------------
     */

    ret = dma_irq_enable(ch_idx);

    if (ret != 0)
    {
        debug_dmac("dma_irq_enable failed\n");
        return -1;
    }


    /* --------------------------------------------------------
     * Start DMA
     * --------------------------------------------------------
     */

    debug_dmac("Starting DMA...\n");

    ret = dma_start(ch_idx);

    if (ret != 0)
    {
        debug_dmac("dma_start failed: %d\n", ret);

        dma_irq_disable(ch_idx);
        return -1;
    }

    /* --------------------------------------------------------
     * Wait for interrupt
     * --------------------------------------------------------
     */

    ret = dma_wait_irq(ch_idx);
    // ret = dma_wait(ch);

    debug_dmac("dma_wait_irq ret = %d\n", ret);

    /* --------------------------------------------------------
     * Cleanup
     * --------------------------------------------------------
     */

    dma_stop(ch_idx);
    dma_irq_disable(ch_idx);


    if (ret != 0)
    {
        debug_dmac("DMA INTERRUPT (LLI) TEST FAILED: ret != 0\n");
        // return -1;
    }

    /* DMA completion means data reached the UART TX FIFO.
     * Wait for the UART shift register to finish transmitting.
     */

    while ((uart->lsr & UART_LSR_TEMT) == 0)
        ;

    debug_dmac("DMA MEM->UART0 TX PASS\n");
    return 0;
}

/**
 * @brief Test memory-to-UART TX DMA using polling completion.
 *
 * @param[in] dev_uart The UART device used by the test.
 * @param[in] ch_idx The DMA channel index.
 * @return 0 when the test passes, otherwise a negative value.
 */
static int dma_mem_uart_poll_test(device_uart dev_uart, unsigned int ch_idx)
{
    static const uint8_t tx_data[] =
        "DMA MEM->UARTXXXXXXX TEST\r\n";

    const size_t tx_size = sizeof(tx_data) - 1;
    // const size_t tx_size = 1;
    int ret;
    unsigned int req_line = DMA_TX_REQ_N_UART(dev_uart);

    if (ch_idx >= DW_DMA_CH_NUM) {
        return -1;
    }

	switch (dev_uart) {
		case UART0:
			uart = (struct dw_uart_regs *)UART0_BASE;
			break;
		case UART1:
			uart = (struct dw_uart_regs *)UART1_BASE;
			break;
		case UART2:
			uart = (struct dw_uart_regs *)UART2_BASE;
			break;
		case UART3:
			uart = (struct dw_uart_regs *)UART3_BASE;
			break;
		default:
			break;
	}

    /*
     * ---------------------------------------------------------
     * 1. Initialize UART FIRST
     * ---------------------------------------------------------
     */
    uart_init(115200, 25 * 1000 * 1000);

    /*
     * ---------------------------------------------------------
     * 2. Enable UART FIFO + DMA mode AFTER uart_init()
     * ---------------------------------------------------------
     */
    uart->fcr =
        UART_FCR_FIFO_EN |
        UART_FCR_DMA_MODE;

    /*
     * Disable the normal UART RX / THRE interrupt sources.
     * DMA TX is driven by the UART DMA request line instead.
     */
    uart->ier &= ~0x3U;

    ret = dma_init();

    if (ret != 0) {
        debug_dmac("dma_init failed\n");
    }

    debug_dmac("\n");
    debug_dmac("========================================\n");
    debug_dmac("DMA MEM -> UART%d TX TEST\n", dev_uart);
    debug_dmac("========================================\n");
    debug_dmac("channel = %d\n", ch_idx);
    debug_dmac("request = %d (UART%d TX)\n", req_line, dev_uart);

    /*
     * These are dedicated UART shadow/status registers, not FCR/IIR.
     */

    volatile uint32_t *sdmam = uart_reg32(uart, UART_SDMAM_OFF);
    volatile uint32_t *sfe   = uart_reg32(uart, UART_SFE_OFF);
    volatile uint32_t *tfl   = uart_reg32(uart, UART_TFL_OFF);
    volatile uint32_t *stet  = uart_reg32(uart, UART_STET_OFF);
    volatile uint32_t *htx   = uart_reg32(uart, UART_HTX_OFF);
    volatile uint32_t *usr   = uart_reg32(uart, UART_USR_OFF);

    debug_dmac("UART IER    = 0x%08x\n", uart->ier);
    debug_dmac("UART IIR    = 0x%08x\n", uart->iir);
    debug_dmac("UART LSR    = 0x%08x\n", uart->lsr);
    debug_dmac("UART LCR    = 0x%08x\n", uart->lcr);
    debug_dmac("UART SDMAM  = 0x%08x\n", *sdmam);
    debug_dmac("UART SFE    = 0x%08x\n", *sfe);
    debug_dmac("UART STET   = 0x%08x\n", *stet);
    debug_dmac("UART HTX    = 0x%08x\n", *htx);
    debug_dmac("UART TFL    = 0x%08x\n", *tfl);
    debug_dmac("BEFORE UART USR = 0x%0x\n", *usr);

    debug_dmac("UART TFL before DMA = 0x%08x\n",
           *(volatile uint32_t *)(uart + 0x080));

    while ((uart->lsr & UART_LSR_TEMT) == 0)
        ;

    /* Source buffer is read by DMA, so push it to memory first. */
    flush_dcache_range(
        (uintptr_t)tx_data,
        tx_size
    );

    struct dma_transfer_config config = {
        .dst = (void *)&uart->thr,
        .src = tx_data,
        .llp = 0,
        .size = tx_size,
        .width = 0x0,
        .burst = 0x0,
        .ch_idx = ch_idx,
        .multblk_type = CONTIGUOUS,
        .type = DMA_TRANSFER_MEM_TO_DEV,
        .src_per = 0,
        .dst_per = ch_idx,
        .src_req = 0,
        .dst_req = req_line,
    };

    /* Configure MEM -> DEV. */
    ret = dma_prepare(&config);
    dmac_assert_msg(ret != 0, ret, "DMA PREPARE TRANSFER ERROR\n");

    /* Start the channel. */
    ret = dma_start(ch_idx);
    dmac_assert_msg(ret != 0, ret, "DMA START ERROR\n");

    /* Wait for DMA to report completion or error. */
    ret = dma_wait(ch_idx);

    if (ret != 0)
    {
        debug_dmac("DMA WAIT ERROR\n");
    }

    dma_stop(ch_idx);

    debug_dmac("AFTER UART USR = 0x%0x\n", *usr);
    if (ret != 0) {
        debug_dmac("DMA MEM->UART%d TX failed: %d\n", dev_uart, ret);
        debug_dmac("UART TFL after DMA error = 0x%08x\n",
               *(volatile uint32_t *)(uart + 0x080));

        return ret;
    }

    debug_dmac("UART TFL after DMA = 0x%08x\n",
           *(volatile uint32_t *)(uart + 0x080));

    /* DMA completion means data reached the UART TX FIFO.
     * Wait for the UART shift register to finish transmitting.
     */
    while ((uart->lsr & UART_LSR_TEMT) == 0)
        ;

    debug_dmac("DMA MEM->UART0 TX PASS\n");
    return 0;
}

/**
 * @brief Test memory-to-UART TX DMA with linked-list descriptors and polling.
 *
 * @param[in] dev_uart The UART device used by the test.
 * @param[in] ch_idx The DMA channel index.
 * @return 0 when the test passes, otherwise a negative value.
 */
static int dma_mem_uart_poll_lli_test(device_uart dev_uart, unsigned int ch_idx)
{
    static const uint8_t tx_data[] =
        "DMA MEM->UARTXXXXXXX TEST\r\n";

    const size_t tx_size = sizeof(tx_data) - 1;
    int ret;
    unsigned int req_line = DMA_TX_REQ_N_UART(dev_uart);

    if (ch_idx >= DW_DMA_CH_NUM) {
        return -1;
    }

	switch (dev_uart) {
		case UART0:
			uart = (struct dw_uart_regs *)UART0_BASE;
			break;
		case UART1:
			uart = (struct dw_uart_regs *)UART1_BASE;
			break;
		case UART2:
			uart = (struct dw_uart_regs *)UART2_BASE;
			break;
		case UART3:
			uart = (struct dw_uart_regs *)UART3_BASE;
			break;
		default:
			break;
	}

    /*
     * ---------------------------------------------------------
     * 1. Initialize UART FIRST
     * ---------------------------------------------------------
     */
    uart_init(115200, 25 * 1000 * 1000);

    /*
     * ---------------------------------------------------------
     * 2. Enable UART FIFO + DMA mode AFTER uart_init()
     * ---------------------------------------------------------
     */
    uart->fcr =
        UART_FCR_FIFO_EN |
        UART_FCR_DMA_MODE;

    /*
     * Disable the normal UART RX / THRE interrupt sources.
     * DMA TX is driven by the UART DMA request line instead.
     */
    uart->ier &= ~0x3U;

    ret = dma_init();

    if (ret != 0) {
        debug_dmac("dma_init failed\n");
    }

    debug_dmac("\n");
    debug_dmac("========================================\n");
    debug_dmac("DMA MEM -> UART%d TX TEST\n", dev_uart);
    debug_dmac("========================================\n");
    debug_dmac("channel = %d\n", ch_idx);
    debug_dmac("request = %d (UART%d TX)\n", req_line, dev_uart);

    /*
     * These are dedicated UART shadow/status registers, not FCR/IIR.
     */

    volatile uint32_t *sdmam = uart_reg32(uart, UART_SDMAM_OFF);
    volatile uint32_t *sfe   = uart_reg32(uart, UART_SFE_OFF);
    volatile uint32_t *tfl   = uart_reg32(uart, UART_TFL_OFF);
    volatile uint32_t *stet  = uart_reg32(uart, UART_STET_OFF);
    volatile uint32_t *htx   = uart_reg32(uart, UART_HTX_OFF);
    volatile uint32_t *usr   = uart_reg32(uart, UART_USR_OFF);

    debug_dmac("UART IER    = 0x%08x\n", uart->ier);
    debug_dmac("UART IIR    = 0x%08x\n", uart->iir);
    debug_dmac("UART LSR    = 0x%08x\n", uart->lsr);
    debug_dmac("UART LCR    = 0x%08x\n", uart->lcr);
    debug_dmac("UART SDMAM  = 0x%08x\n", *sdmam);
    debug_dmac("UART SFE    = 0x%08x\n", *sfe);
    debug_dmac("UART STET   = 0x%08x\n", *stet);
    debug_dmac("UART HTX    = 0x%08x\n", *htx);
    debug_dmac("UART TFL    = 0x%08x\n", *tfl);
    debug_dmac("BEFORE UART USR = 0x%0x\n", *usr);

    debug_dmac("UART TFL before DMA = 0x%08x\n",
           *(volatile uint32_t *)(uart + 0x080));

    while ((uart->lsr & UART_LSR_TEMT) == 0)
        ;

    /* Source buffer is read by DMA, so push it to memory first. */
    flush_dcache_range(
        (uintptr_t)tx_data,
        tx_size
    );

    #define DMA_TEST_SIZE 4

    unsigned char n_nodes = tx_size / DMA_TEST_SIZE + (tx_size & 1);

    LIST_HEAD(head);

    for (unsigned char i = 0; i < tx_size; i += DMA_TEST_SIZE)
    {
        uint64_t ctl = dma_get_ctl(0x0, 0x0, DMA_TRANSFER_MEM_TO_DEV);
        size_t data_size = (i + DMA_TEST_SIZE <= tx_size) ? DMA_TEST_SIZE : (tx_size % DMA_TEST_SIZE);
        uint32_t block_ts = dma_get_block_ts(data_size, 0x0);

        lli_new_add_tail(
            &head,
            &tx_data[i],
            (void *)&uart->thr,
            block_ts,
            ctl
        );
    }

    int i = 0;
    for (struct list_head *pos = head.next; pos != &head; pos = pos->next)
    {
        struct dw_desc *desc =
            list_entry(pos, struct dw_desc, desc_node);

        flush_dcache_range(
            (uintptr_t)&desc->lli,
            sizeof(struct dma_lli)
        );
        debug_dmac("Node %d CTL: %016lx\n", i, desc->lli.ctl);
        i++;
    }

    /* --------------------------------------------------------
     * Prepare DMA
     * --------------------------------------------------------
     */

    struct dw_desc *desc = list_entry(head.next, struct dw_desc, desc_node);

    struct dma_transfer_config config = {
        .dst = 0,
        .src = 0,
        .llp = &desc->lli,
        .size = 0,
        .width = 0,
        .burst = 0,
        .ch_idx = ch_idx,
        .multblk_type = LINK_LIST,
        .type = DMA_TRANSFER_MEM_TO_DEV,
        .src_per = 0,
        .dst_per = ch_idx,
        .src_req = 0,
        .dst_req = req_line,
    };

    ret = dma_prepare(&config);

    if (ret != 0)
    {
        debug_dmac("dma_prepare failed\n");
        return -1;
    }

    /* --------------------------------------------------------
     * Start DMA
     * --------------------------------------------------------
     */

    debug_dmac("Starting DMA...\n");

    ret = dma_start(ch_idx);

    if (ret != 0)
    {
        debug_dmac("dma_start failed: %d\n", ret);
        return -1;
    }

    /* --------------------------------------------------------
     * Wait for DMA Polling
     * --------------------------------------------------------
     */

    ret = dma_wait(ch_idx);

    debug_dmac("dma_wait ret = %d\n", ret);

    /* --------------------------------------------------------
     * Cleanup
     * --------------------------------------------------------
     */

    dma_stop(ch_idx);

    if (ret != 0)
    {
        debug_dmac("DMA POLLING TEST FAILED: ret != 0\n");
        // return -1;
    }

    /* DMA completion means data reached the UART TX FIFO.
     * Wait for the UART shift register to finish transmitting.
     */

    while ((uart->lsr & UART_LSR_TEMT) == 0)
        ;

    debug_dmac("DMA MEM->UART0 (LLI) TX PASS\n");
    return 0;
}

#define DMA_DEV_MEM_TEST_BUF_SIZE 4096

/**
 * @brief Test UART RX-to-memory DMA using interrupt completion.
 *
 * @param[in] dev_uart The UART device used by the test.
 * @param[in] ch_idx The DMA channel index.
 * @return 0 when the test passes, otherwise a negative value.
 */
static int dma_uart_mem_int_test(device_uart dev_uart, unsigned int ch_idx)
{

    static uint8_t rx_buf[DMA_DEV_MEM_TEST_BUF_SIZE] = {};
    static const uint8_t tx_data[] =
        "HENG><><><><><><><><><<><\n";

    const size_t tx_size = 8;

    // const size_t tx_size = 1;
    int ret;
    unsigned int req_line = DMA_RX_REQ_N_UART(dev_uart);

    if (ch_idx >= DW_DMA_CH_NUM) {
        return -1;
    }

	switch (dev_uart) {
		case UART0:
			uart = (struct dw_uart_regs *)UART0_BASE;
			break;
		case UART1:
			uart = (struct dw_uart_regs *)UART1_BASE;
			break;
		case UART2:
			uart = (struct dw_uart_regs *)UART2_BASE;
			break;
		case UART3:
			uart = (struct dw_uart_regs *)UART3_BASE;
			break;
		default:
			break;
	}

    /*
     * ---------------------------------------------------------
     * 1. Initialize UART FIRST
     * ---------------------------------------------------------
     */
    uart_init(115200, 25 * 1000 * 1000);

    /*
     * ---------------------------------------------------------
     * 2. Enable UART FIFO + DMA mode AFTER uart_init()
     * ---------------------------------------------------------
     */
    uart->fcr =
        UART_FCR_FIFO_EN |
        UART_FCR_DMA_MODE | 
        UART_FCR_TXTRIG_1_4;

    /*
     * Disable the normal UART RX / THRE interrupt sources.
     * DMA TX is driven by the UART DMA request line instead.
     */
    uart->ier &= ~0x3U;

    ret = dma_init();

    if (ret != 0) {
        debug_dmac("dma_init failed\n");
    }

    debug_dmac("Initializing DMA IRQ...\n");

    ret = dma_irq_init();

    if (ret != 0)
    {
        debug_dmac("dma_irq_init failed: %d\n", ret);
        return -1;
    }

    debug_dmac("\n");
    debug_dmac("========================================\n");
    debug_dmac("DMA MEM -> UART%d RX TEST\n", dev_uart);
    debug_dmac("========================================\n");
    debug_dmac("channel = %d\n", ch_idx);
    debug_dmac("request = %d (UART%d RX)\n", req_line, dev_uart);

    /*
     * These are dedicated UART shadow/status registers, not FCR/IIR.
     */

    volatile uint32_t *sdmam = uart_reg32(uart, UART_SDMAM_OFF);
    volatile uint32_t *sfe   = uart_reg32(uart, UART_SFE_OFF);
    volatile uint32_t *tfl   = uart_reg32(uart, UART_TFL_OFF);
    volatile uint32_t *stet  = uart_reg32(uart, UART_STET_OFF);
    volatile uint32_t *htx   = uart_reg32(uart, UART_HTX_OFF);
    volatile uint32_t *usr   = uart_reg32(uart, UART_USR_OFF);

    debug_dmac("UART IER    = 0x%08x\n", uart->ier);
    debug_dmac("UART IIR    = 0x%08x\n", uart->iir);
    debug_dmac("UART LSR    = 0x%08x\n", uart->lsr);
    debug_dmac("UART LCR    = 0x%08x\n", uart->lcr);
    debug_dmac("UART FCR    = 0x%08x\n", uart->fcr);
    debug_dmac("UART SDMAM  = 0x%08x\n", *sdmam);
    debug_dmac("UART SFE    = 0x%08x\n", *sfe);
    debug_dmac("UART STET   = 0x%08x\n", *stet);
    debug_dmac("UART HTX    = 0x%08x\n", *htx);
    debug_dmac("UART TFL    = 0x%08x\n", *tfl);
    debug_dmac("BEFORE UART USR = 0x%0x\n", *usr);

    debug_dmac("UART TFL before DMA = 0x%08x\n",
           *(volatile uint32_t *)(uart + 0x080));


    /* Source buffer is read by DMA, so push it to memory first. */
    flush_dcache_range(
        (uintptr_t)tx_data,
        tx_size
    );

    debug_dmac("Waiting UART0 RX...\n");
    debug_dmac("Current Number of bytes in RX FIFO: %d\n", (uart->rfl & 0x3f));
    debug_dmac("Please Input %d words\n", tx_size);

    while ((uart->lsr & UART_LSR_TEMT) == 0)
        ;

    uart->fcr |= UART_FCR_RXSR;

    while (1)
    {
        uint32_t rfl = uart->rfl;

        if (rfl >= tx_size + 8)
        {
            break;
        }
    }
    debug_dmac("Current Number of bytes in RX FIFO: %d\n", (uart->rfl & 0x3f));

    struct dma_transfer_config config = {
        .dst = rx_buf,
        .src = (void *)&uart->rbr,
        .llp = 0,
        .size = tx_size,
        .width = 0x0,
        .burst = 0x0,
        .ch_idx = ch_idx,
        .multblk_type = CONTIGUOUS,
        .type = DMA_TRANSFER_DEV_TO_MEM,
        .src_per = ch_idx,
        .dst_per = 0,
        .src_req = req_line,
        .dst_req = 0,
    };

    /* Configure MEM -> DEV. */
    ret = dma_prepare(&config);
    dmac_assert_msg(ret != 0, ret, "DMA PREPARE TRANSFTER ERROR\n");

    /* Enable channel interrupt BEFORE starting the DMA channel. */
    ret = dma_irq_enable(ch_idx);
    dmac_assert_msg(ret != 0, ret, "DMA IRQ ENABLE ERROR\n");

    /* Start the channel. */
    ret = dma_start(ch_idx);

    if (ret != 0)
    {
        dma_irq_disable(ch_idx);
        debug_dmac("DMA START ERROR\n");
        return ret;
    }

    /* Wait for ISR to report completion or error. */
    ret = dma_wait_irq(ch_idx);
    if (ret == -1)
    {
        debug_dmac("DMAC_CH_INTSTAT_ERR_MASK\n");
    }

    /* Stop/disable interrupt regardless of success or failure. */
    dma_stop(ch_idx);
    dma_irq_disable(ch_idx);

    // ret = dma_dev_to_mem_irq(
    //     rx_buf,
    //     (void *)&uart->rbr,
    //     tx_size,
    //     0x0,                    /* 8-bit */
    //     0x0,                    /* burst = 1 transfer */
    //     ch_idx,
    //     ch_idx,                 /* SRC_PER: handshake interface 0 */
    //     req_line                /* UART UX request line */
    // );

    debug_dmac("AFTER UART USR = 0x%0x\n", *usr);
    if (ret != 0) {
        debug_dmac("DMA UART%d RX -> MEM failed: %d\n", dev_uart, ret);
        debug_dmac("UART TFL after DMA error = 0x%08x\n",
               *(volatile uint32_t *)(uart + 0x080));

        return ret;
    }


    debug_dmac("RX Data: %s\n", rx_buf);
    debug_dmac("DMA UART%d RX -> MEM PASS\n", ch_idx);

    return 0;
}

/**
 * @brief Test UART RX-to-memory DMA with linked-list descriptors and interrupts.
 *
 * @param[in] dev_uart The UART device used by the test.
 * @param[in] ch_idx The DMA channel index.
 * @return 0 when the test passes, otherwise a negative value.
 */
static int dma_uart_mem_int_lli_test(device_uart dev_uart, unsigned int ch_idx)
{

    static uint8_t rx_buf[DMA_DEV_MEM_TEST_BUF_SIZE] = {};

    const size_t tx_size = 8;

    int ret;
    unsigned int req_line = DMA_RX_REQ_N_UART(dev_uart);

    if (ch_idx >= DW_DMA_CH_NUM) {
        return -1;
    }

	switch (dev_uart) {
		case UART0:
			uart = (struct dw_uart_regs *)UART0_BASE;
			break;
		case UART1:
			uart = (struct dw_uart_regs *)UART1_BASE;
			break;
		case UART2:
			uart = (struct dw_uart_regs *)UART2_BASE;
			break;
		case UART3:
			uart = (struct dw_uart_regs *)UART3_BASE;
			break;
		default:
			break;
	}

    /*
     * ---------------------------------------------------------
     * 1. Initialize UART FIRST
     * ---------------------------------------------------------
     */
    uart_init(115200, 25 * 1000 * 1000);

    /*
     * ---------------------------------------------------------
     * 2. Enable UART FIFO + DMA mode AFTER uart_init()
     * ---------------------------------------------------------
     */
    uart->fcr =
        UART_FCR_FIFO_EN |
        UART_FCR_DMA_MODE | 
        UART_FCR_TXTRIG_1_4;

    /*
     * Disable the normal UART RX / THRE interrupt sources.
     * DMA TX is driven by the UART DMA request line instead.
     */
    uart->ier &= ~0x3U;
    // uart->far = 1;

    ret = dma_init();

    if (ret != 0) {
        debug_dmac("dma_init failed\n");
    }

    debug_dmac("Initializing DMA IRQ...\n");

    ret = dma_irq_init();

    if (ret != 0)
    {
        debug_dmac("dma_irq_init failed: %d\n", ret);
        return -1;
    }

    debug_dmac("\n");
    debug_dmac("========================================\n");
    debug_dmac("DMA MEM -> UART%d RX TEST\n", dev_uart);
    debug_dmac("========================================\n");
    debug_dmac("channel = %d\n", ch_idx);
    debug_dmac("request = %d (UART%d RX)\n", req_line, dev_uart);

    /*
     * These are dedicated UART shadow/status registers, not FCR/IIR.
     */

    volatile uint32_t *sdmam = uart_reg32(uart, UART_SDMAM_OFF);
    volatile uint32_t *sfe   = uart_reg32(uart, UART_SFE_OFF);
    volatile uint32_t *tfl   = uart_reg32(uart, UART_TFL_OFF);
    volatile uint32_t *stet  = uart_reg32(uart, UART_STET_OFF);
    volatile uint32_t *htx   = uart_reg32(uart, UART_HTX_OFF);
    volatile uint32_t *usr   = uart_reg32(uart, UART_USR_OFF);

    debug_dmac("UART IER    = 0x%08x\n", uart->ier);
    debug_dmac("UART IIR    = 0x%08x\n", uart->iir);
    debug_dmac("UART LSR    = 0x%08x\n", uart->lsr);
    debug_dmac("UART LCR    = 0x%08x\n", uart->lcr);
    debug_dmac("UART FCR    = 0x%08x\n", uart->fcr);
    debug_dmac("UART SDMAM  = 0x%08x\n", *sdmam);
    debug_dmac("UART SFE    = 0x%08x\n", *sfe);
    debug_dmac("UART STET   = 0x%08x\n", *stet);
    debug_dmac("UART HTX    = 0x%08x\n", *htx);
    debug_dmac("UART TFL    = 0x%08x\n", *tfl);
    debug_dmac("BEFORE UART USR = 0x%0x\n", *usr);

    debug_dmac("UART TFL before DMA = 0x%08x\n",
           *(volatile uint32_t *)(uart + 0x080));

    debug_dmac("Waiting UART0 RX...\n");
    debug_dmac("Current Number of bytes in RX FIFO: %d\n", (uart->rfl & 0x3f));
    debug_dmac("Please Input %d words\n", tx_size);

    while ((uart->lsr & UART_LSR_TEMT) == 0)
        ;

    uart->fcr |= UART_FCR_RXSR;

    uint32_t rfl;
    while (1)
    {
        rfl = uart->rfl;
        if (rfl >= tx_size + 8)
        {
            break;
        }
    }
    debug_dmac("Current Number of bytes in RX FIFO: %d\n", (rfl & 0x3f));

    #define DMA_TEST_SIZE 4

    unsigned char n_nodes = rfl / DMA_TEST_SIZE + (rfl & 1);

    LIST_HEAD(head);

    for (unsigned char i = 0; i < rfl; i += DMA_TEST_SIZE)
    {
        uint64_t ctl = dma_get_ctl(0x0, 0x0, DMA_TRANSFER_DEV_TO_MEM);
        size_t data_size = (i + DMA_TEST_SIZE <= rfl) ? DMA_TEST_SIZE : (rfl % DMA_TEST_SIZE);
        uint32_t block_ts = dma_get_block_ts(data_size, 0x0);

        lli_new_add_tail(
            &head,
            (void *)&uart->rbr,
            &rx_buf[i],
            block_ts,
            ctl
        );
    }

    int i = 0;
    for (struct list_head *pos = head.next; pos != &head; pos = pos->next)
    {
        struct dw_desc *desc =
            list_entry(pos, struct dw_desc, desc_node);

        flush_dcache_range(
            (uintptr_t)&desc->lli,
            sizeof(struct dma_lli)
        );
        debug_dmac("Node %d CTL: %016lx\n", i, desc->lli.ctl);
        i++;
    }

    /* --------------------------------------------------------
     * Prepare DMA
     * --------------------------------------------------------
     */

    struct dw_desc *desc = list_entry(head.next, struct dw_desc, desc_node);

    struct dma_transfer_config config = {
        .dst = 0,
        .src = 0,
        .llp = &desc->lli,
        .size = 0,
        .width = 0,
        .burst = 0,
        .ch_idx = ch_idx,
        .multblk_type = LINK_LIST,
        .type = DMA_TRANSFER_DEV_TO_MEM,
        .src_per = ch_idx,
        .dst_per = 0,
        .src_req = req_line,
        .dst_req = 0,
    };

    ret = dma_prepare(&config);

    if (ret != 0)
    {
        debug_dmac("dma_prepare failed\n");
        return -1;
    }

    /* --------------------------------------------------------
     * Enable DMA interrupt
     * --------------------------------------------------------
     */

    ret = dma_irq_enable(ch_idx);

    if (ret != 0)
    {
        debug_dmac("dma_irq_enable failed\n");
        return -1;
    }


    /* --------------------------------------------------------
     * Start DMA
     * --------------------------------------------------------
     */

    debug_dmac("Starting DMA...\n");

    ret = dma_start(ch_idx);

    if (ret != 0)
    {
        debug_dmac("dma_start failed: %d\n", ret);

        dma_irq_disable(ch_idx);
        return -1;
    }

    /* --------------------------------------------------------
     * Wait for interrupt
     * --------------------------------------------------------
     */

    ret = dma_wait_irq(ch_idx);
    // ret = dma_wait(ch);

    debug_dmac("dma_wait_irq ret = %d\n", ret);

    /* --------------------------------------------------------
     * Cleanup
     * --------------------------------------------------------
     */

    dma_stop(ch_idx);
    dma_irq_disable(ch_idx);

    if (ret != 0)
    {
        debug_dmac("DMA INTERRUPT (LLI) TEST FAILED: ret != 0\n");
        // return -1;
    }

    debug_dmac("RX Data: %s\n", rx_buf);
    debug_dmac("DMA UART%d RX -> MEM PASS\n", ch_idx);

    return 0;
}

/**
 * @brief Test UART RX-to-memory DMA using polling completion.
 *
 * @param[in] dev_uart The UART device used by the test.
 * @param[in] ch_idx The DMA channel index.
 * @return 0 when the test passes, otherwise a negative value.
 */
static int dma_uart_mem_poll_test(device_uart dev_uart, unsigned int ch_idx)
{

    static uint8_t rx_buf[DMA_DEV_MEM_TEST_BUF_SIZE] = {};
    static const uint8_t tx_data[] =
        "HENG><><><><><><><><><<><\n";

    const size_t tx_size = 8;

    // const size_t tx_size = 1;
    int ret;
    unsigned int req_line = DMA_RX_REQ_N_UART(dev_uart);

    if (ch_idx >= DW_DMA_CH_NUM) {
        return -1;
    }

	switch (dev_uart) {
		case UART0:
			uart = (struct dw_uart_regs *)UART0_BASE;
			break;
		case UART1:
			uart = (struct dw_uart_regs *)UART1_BASE;
			break;
		case UART2:
			uart = (struct dw_uart_regs *)UART2_BASE;
			break;
		case UART3:
			uart = (struct dw_uart_regs *)UART3_BASE;
			break;
		default:
			break;
	}

    /*
     * ---------------------------------------------------------
     * 1. Initialize UART FIRST
     * ---------------------------------------------------------
     */
    uart_init(115200, 25 * 1000 * 1000);

    /*
     * ---------------------------------------------------------
     * 2. Enable UART FIFO + DMA mode AFTER uart_init()
     * ---------------------------------------------------------
     */
    uart->fcr =
        UART_FCR_FIFO_EN |
        UART_FCR_DMA_MODE | 
        UART_FCR_TXTRIG_1_4;

    /*
     * Disable the normal UART RX / THRE interrupt sources.
     * DMA TX is driven by the UART DMA request line instead.
     */
    uart->ier &= ~0x3U;
    // uart->far = 1;

    ret = dma_init();

    if (ret != 0) {
        debug_dmac("dma_init failed\n");
    }

    debug_dmac("\n");
    debug_dmac("========================================\n");
    debug_dmac("DMA MEM -> UART%d RX TEST\n", dev_uart);
    debug_dmac("========================================\n");
    debug_dmac("channel = %d\n", ch_idx);
    debug_dmac("request = %d (UART%d RX)\n", req_line, dev_uart);

    /*
     * These are dedicated UART shadow/status registers, not FCR/IIR.
     */

    volatile uint32_t *sdmam = uart_reg32(uart, UART_SDMAM_OFF);
    volatile uint32_t *sfe   = uart_reg32(uart, UART_SFE_OFF);
    volatile uint32_t *tfl   = uart_reg32(uart, UART_TFL_OFF);
    volatile uint32_t *stet  = uart_reg32(uart, UART_STET_OFF);
    volatile uint32_t *htx   = uart_reg32(uart, UART_HTX_OFF);
    volatile uint32_t *usr   = uart_reg32(uart, UART_USR_OFF);

    debug_dmac("UART IER    = 0x%08x\n", uart->ier);
    debug_dmac("UART IIR    = 0x%08x\n", uart->iir);
    debug_dmac("UART LSR    = 0x%08x\n", uart->lsr);
    debug_dmac("UART LCR    = 0x%08x\n", uart->lcr);
    debug_dmac("UART FCR    = 0x%08x\n", uart->fcr);
    debug_dmac("UART SDMAM  = 0x%08x\n", *sdmam);
    debug_dmac("UART SFE    = 0x%08x\n", *sfe);
    debug_dmac("UART STET   = 0x%08x\n", *stet);
    debug_dmac("UART HTX    = 0x%08x\n", *htx);
    debug_dmac("UART TFL    = 0x%08x\n", *tfl);
    debug_dmac("BEFORE UART USR = 0x%0x\n", *usr);

    debug_dmac("UART TFL before DMA = 0x%08x\n",
           *(volatile uint32_t *)(uart + 0x080));


    /* Source buffer is read by DMA, so push it to memory first. */
    flush_dcache_range(
        (uintptr_t)tx_data,
        tx_size
    );

    debug_dmac("Waiting UART0 RX...\n");
    debug_dmac("Current Number of bytes in RX FIFO: %d\n", (uart->rfl & 0x3f));
    debug_dmac("Please Input %d words\n", tx_size);

    while ((uart->lsr & UART_LSR_TEMT) == 0)
        ;

    uart->fcr |= UART_FCR_RXSR;

    while (1)
    {
        uint32_t rfl = uart->rfl;

        if (rfl >= tx_size + 8)
        {
            break;
        }
    }
    debug_dmac("Current Number of bytes in RX FIFO: %d\n", (uart->rfl & 0x3f));

    struct dma_transfer_config config = {
        .dst = rx_buf,
        .src = (void *)&uart->rbr,
        .llp = 0,
        .size = tx_size,
        .width = 0x0,
        .burst = 0x0,
        .ch_idx = ch_idx,
        .multblk_type = CONTIGUOUS,
        .type = DMA_TRANSFER_DEV_TO_MEM,
        .src_per = ch_idx,
        .dst_per = 0,
        .src_req = req_line,
        .dst_req = 0,
    };

    /* Configure MEM -> DEV. */
    ret = dma_prepare(&config);
    dmac_assert_msg(ret != 0, ret, "DMA PREPARE TRANSFTER ERROR\n");

    /* Start the channel. */
    ret = dma_start(ch_idx);
    dmac_assert_msg(ret != 0, ret, "DMA START ERROR\n");

    ret = dma_wait(ch_idx);

    if (ret == -1)
    {
        debug_dmac("DMA WAIT ERROR\n");
    }

    dma_stop(ch_idx);

    // ret = dma_dev_to_mem(
    //     rx_buf,
    //     (void *)&uart->rbr,
    //     tx_size,
    //     0x0,                    /* 8-bit */
    //     0x0,                    /* burst = 1 transfer */
    //     ch_idx,
    //     ch_idx,                 /* SRC_PER: handshake interface 0 */
    //     req_line                /* UART UX request line */
    // );

    debug_dmac("AFTER UART USR = 0x%0x\n", *usr);
    if (ret != 0) {
        debug_dmac("DMA UART%d RX -> MEM failed: %d\n", dev_uart, ret);
        debug_dmac("UART TFL after DMA error = 0x%08x\n",
               *(volatile uint32_t *)(uart + 0x080));

        return ret;
    }


    debug_dmac("RX Data: %s\n", rx_buf);
    debug_dmac("DMA UART%d RX -> MEM PASS\n", ch_idx);

    return 0;
}

/**
 * @brief Test UART RX-to-memory DMA with linked-list descriptors and polling.
 *
 * @param[in] dev_uart The UART device used by the test.
 * @param[in] ch_idx The DMA channel index.
 * @return 0 when the test passes, otherwise a negative value.
 */
static int dma_uart_mem_poll_lli_test(device_uart dev_uart, unsigned int ch_idx)
{

    static uint8_t rx_buf[DMA_DEV_MEM_TEST_BUF_SIZE] = {};

    const size_t tx_size = 8;

    int ret;
    unsigned int req_line = DMA_RX_REQ_N_UART(dev_uart);

    if (ch_idx >= DW_DMA_CH_NUM) {
        return -1;
    }

	switch (dev_uart) {
		case UART0:
			uart = (struct dw_uart_regs *)UART0_BASE;
			break;
		case UART1:
			uart = (struct dw_uart_regs *)UART1_BASE;
			break;
		case UART2:
			uart = (struct dw_uart_regs *)UART2_BASE;
			break;
		case UART3:
			uart = (struct dw_uart_regs *)UART3_BASE;
			break;
		default:
			break;
	}

    /*
     * ---------------------------------------------------------
     * 1. Initialize UART FIRST
     * ---------------------------------------------------------
     */
    uart_init(115200, 25 * 1000 * 1000);

    /*
     * ---------------------------------------------------------
     * 2. Enable UART FIFO + DMA mode AFTER uart_init()
     * ---------------------------------------------------------
     */
    uart->fcr =
        UART_FCR_FIFO_EN |
        UART_FCR_DMA_MODE | 
        UART_FCR_TXTRIG_1_4;

    /*
     * Disable the normal UART RX / THRE interrupt sources.
     * DMA TX is driven by the UART DMA request line instead.
     */
    uart->ier &= ~0x3U;
    // uart->far = 1;

    ret = dma_init();

    if (ret != 0) {
        debug_dmac("dma_init failed\n");
    }

    debug_dmac("\n");
    debug_dmac("========================================\n");
    debug_dmac("DMA MEM -> UART%d RX TEST\n", dev_uart);
    debug_dmac("========================================\n");
    debug_dmac("channel = %d\n", ch_idx);
    debug_dmac("request = %d (UART%d RX)\n", req_line, dev_uart);

    /*
     * These are dedicated UART shadow/status registers, not FCR/IIR.
     */

    volatile uint32_t *sdmam = uart_reg32(uart, UART_SDMAM_OFF);
    volatile uint32_t *sfe   = uart_reg32(uart, UART_SFE_OFF);
    volatile uint32_t *tfl   = uart_reg32(uart, UART_TFL_OFF);
    volatile uint32_t *stet  = uart_reg32(uart, UART_STET_OFF);
    volatile uint32_t *htx   = uart_reg32(uart, UART_HTX_OFF);
    volatile uint32_t *usr   = uart_reg32(uart, UART_USR_OFF);

    debug_dmac("UART IER    = 0x%08x\n", uart->ier);
    debug_dmac("UART IIR    = 0x%08x\n", uart->iir);
    debug_dmac("UART LSR    = 0x%08x\n", uart->lsr);
    debug_dmac("UART LCR    = 0x%08x\n", uart->lcr);
    debug_dmac("UART FCR    = 0x%08x\n", uart->fcr);
    debug_dmac("UART SDMAM  = 0x%08x\n", *sdmam);
    debug_dmac("UART SFE    = 0x%08x\n", *sfe);
    debug_dmac("UART STET   = 0x%08x\n", *stet);
    debug_dmac("UART HTX    = 0x%08x\n", *htx);
    debug_dmac("UART TFL    = 0x%08x\n", *tfl);
    debug_dmac("BEFORE UART USR = 0x%0x\n", *usr);

    debug_dmac("UART TFL before DMA = 0x%08x\n",
           *(volatile uint32_t *)(uart + 0x080));

    debug_dmac("Waiting UART0 RX...\n");
    debug_dmac("Current Number of bytes in RX FIFO: %d\n", (uart->rfl & 0x3f));
    debug_dmac("Please Input %d words\n", tx_size);

    while ((uart->lsr & UART_LSR_TEMT) == 0)
        ;

    uart->fcr |= UART_FCR_RXSR;

    uint32_t rfl;
    while (1)
    {
        rfl = uart->rfl;
        if (rfl >= tx_size + 8)
        {
            break;
        }
    }

    debug_dmac("Current Number of bytes in RX FIFO: %d\n", (rfl & 0x3f));
    #define DMA_TEST_SIZE 4

    unsigned char n_nodes = rfl / DMA_TEST_SIZE + (rfl & 1);

    LIST_HEAD(head);

    for (unsigned char i = 0; i < rfl; i += DMA_TEST_SIZE)
    {
        uint64_t ctl = dma_get_ctl(0x0, 0x0, DMA_TRANSFER_DEV_TO_MEM);
        size_t data_size = (i + DMA_TEST_SIZE <= rfl) ? DMA_TEST_SIZE : (rfl % DMA_TEST_SIZE);
        uint32_t block_ts = dma_get_block_ts(data_size, 0x0);

        lli_new_add_tail(
            &head,
            (void *)&uart->rbr,
            &rx_buf[i],
            block_ts,
            ctl
        );
    }

    int i = 0;
    for (struct list_head *pos = head.next; pos != &head; pos = pos->next)
    {
        struct dw_desc *desc =
            list_entry(pos, struct dw_desc, desc_node);

        flush_dcache_range(
            (uintptr_t)&desc->lli,
            sizeof(struct dma_lli)
        );
        debug_dmac("Node %d CTL: %016lx\n", i, desc->lli.ctl);
        i++;
    }


    struct dw_desc *desc = list_entry(head.next, struct dw_desc, desc_node);

    struct dma_transfer_config config = {
        .dst = 0,
        .src = 0,
        .llp = &desc->lli,
        .size = 0,
        .width = 0,
        .burst = 0,
        .ch_idx = ch_idx,
        .multblk_type = LINK_LIST,
        .type = DMA_TRANSFER_DEV_TO_MEM,
        .src_per = ch_idx,
        .dst_per = 0,
        .src_req = req_line,
        .dst_req = 0,
    };

    ret = dma_prepare(&config);

    if (ret != 0)
    {
        debug_dmac("dma_prepare failed\n");
        return -1;
    }

    /* --------------------------------------------------------
     * Start DMA
     * --------------------------------------------------------
     */

    debug_dmac("Starting DMA...\n");

    ret = dma_start(ch_idx);

    if (ret != 0)
    {
        debug_dmac("dma_start failed: %d\n", ret);
        return -1;
    }

    /* --------------------------------------------------------
     * Wait for Polling
     * --------------------------------------------------------
     */

    ret = dma_wait(ch_idx);

    debug_dmac("dma_wait ret = %d\n", ret);

    /* --------------------------------------------------------
     * Cleanup
     * --------------------------------------------------------
     */

    dma_stop(ch_idx);

    if (ret != 0)
    {
        debug_dmac("DMA POLLING (LLI) TEST FAILED: ret != 0\n");
        // return -1;
    }

    debug_dmac("RX Data: %s\n", rx_buf);
    debug_dmac("DMA UART%d RX -> MEM (LLI) PASS\n", ch_idx);

    return 0;
}

/**
 * @brief Dispatch a DMA test according to the selected DMA option.
 *
 * @param[in] params DMA test configuration parameters.
 * @return 0 when the selected test completes successfully, otherwise a
 *         negative value.
 */
void dma_mode(dma_params *dma_params_ptr)
{
    enum DMA_OPTION opt = dma_params_ptr->opt;

    unsigned char ch = dma_params_ptr->ch;

    int ret = 777;

    (void)ch;
    (void)ret; 

    switch (opt)
    {
        case DMA_MEM_MEM_POLLING_BA:

            // dma_bench_test();
            ret = dma_mem_mem_poll_test(dma_params_ptr);
            debug_dmac("DMA MEM->MEM POLL (BASIC) ret = %d\n", ret);

            break;


        case DMA_MEM_MEM_INT_BA:

            ret = dma_mem_mem_int_test(dma_params_ptr);
            debug_dmac("DMA MEM->MEM INT (BASIC) ret = %d\n", ret);
            break;

        case DMA_MEM_MEM_POLLING_LIST:
            ret = dma_poll_lli_test(dma_params_ptr);
            debug_dmac("DMA MEM->MEM POLL (LLI) ret = %d\n", ret);
            break;


        case DMA_MEM_MEM_INT_LIST:

            ret = dma_int_lli_test(dma_params_ptr);
            debug_dmac("DMA MEM->MEM INT (LLI) ret = %d\n", ret);
            break;

        case DMA_MEM_DEV_POLLING_BA:

            ret = dma_mem_uart_poll_test(0, ch);
            debug_dmac("DMA MEM->UART POLL (BASIC) ret = %d\n", ret);
            break;


        case DMA_MEM_DEV_INT_BA:
            ret = dma_mem_uart_int_test(0, ch);
            debug_dmac("DMA MEM->UART INT (BASIC) ret = %d\n", ret);
            break;

        case DMA_MEM_DEV_POLLING_LIST:

            ret = dma_mem_uart_poll_lli_test(0, ch);
            debug_dmac("DMA MEM->UART POLL (LLI) ret = %d\n", ret);
            break;


        case DMA_MEM_DEV_INT_LIST:

            ret = dma_mem_uart_int_lli_test(0, ch);
            debug_dmac("DMA MEM->UART INT (LLI) ret = %d\n", ret);
            break;

        case DMA_DEV_MEM_POLLING_BA:
            ret = dma_uart_mem_poll_test(0, ch);
            debug_dmac("DMA UART->MEM POLL (BASIC) ret = %d\n", ret);
            break;


        case DMA_DEV_MEM_INT_BA:
            ret = dma_uart_mem_int_test(0, ch);
            debug_dmac("DMA UART->MEM INT (BASIC) ret = %d\n", ret);
            break;

        case DMA_DEV_MEM_POLLING_LIST:

            ret = dma_uart_mem_poll_lli_test(0, ch);
            debug_dmac("DMA UART->MEM POLL (LLI) ret = %d\n", ret);
            break;


        case DMA_DEV_MEM_INT_LIST:

            ret = dma_uart_mem_int_lli_test(0, ch);
            debug_dmac("DMA UART->MEM INT (LLI) ret = %d\n", ret);
            break;

        case DMA_DEV_DEV_POLLING_LIST:

            break;


        case DMA_DEV_DEV_INT_LIST:

            break;

        case DMA_DEV_DEV_POLLING_BA:

            break;


        case DMA_DEV_DEV_INT_BA:

            break;

        default:

            break;
    }
}