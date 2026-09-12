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

#ifndef __HAL_DW_DMA_H_
#define __HAL_DW_DMA_H_

#include <stdint.h>
#include <stddef.h>
#include "hal_dw_list.h"

/*-----------------------------------------------------------*/

/**
 * @brief DMA transfer direction.
 */
enum dma_transfer_type {
    DMA_TRANSFER_MEM_TO_MEM,
    DMA_TRANSFER_MEM_TO_DEV,
    DMA_TRANSFER_DEV_TO_MEM,
    DMA_TRANSFER_DEV_TO_DEV,
};

/**
 * @brief DMA channel state flags.
 */
enum dma_flags {
	DMA_IS_CYCLIC = 0,
	DMA_IS_SOFT_LLP = 1,
	DMA_IS_PAUSED = 2,
	DMA_IS_INITIALIZED = 3,
};

/**
 * @brief DMA multi-block transfer type.
 */
enum dma_multblk_type {
	CONTIGUOUS,
	RELOAD,
	SHADOW_REGISTER,
	LINK_LIST
};

/**
 * @brief Configuration for a DMA transfer.
 *
 * src_per / dst_per select one of the DMAC hardware handshake
 * interfaces when the corresponding endpoint is a peripheral.
 * The peripheral request line is bound to the DMA channel through
 * the
 * SG2002 system DMA remap registers.
 */
struct dma_transfer_config {

    void *dst;
    const void *src;
    const void *llp;

    enum dma_transfer_type type;
    enum dma_multblk_type multblk_type;

    size_t size;

    unsigned char width;
    unsigned char burst;
    unsigned char ch_idx;

    unsigned char src_per;
    unsigned char dst_per;
    unsigned char src_req;
    unsigned char dst_req;
};

/*-----------------------------------------------------------*/

/**
 * @brief Build the DMA channel control register value.
 *
 * @param[in] width The transfer width.
 * @param[in] burst The burst transaction length.
 * @param[in] type The DMA transfer direction.
 * @return The value to be programmed into the DMA control register.
 */
uint64_t dma_get_ctl(unsigned char width, unsigned char burst, enum dma_transfer_type type);

/**
 * @brief Calculate the DMA block transfer size register value.
 *
 * @param[in] size The total transfer size in bytes.
 * @param[in] width The transfer width in bytes.
 * @return The value to be programmed into CHx_BLOCK_TS.
 */
uint32_t dma_get_block_ts(size_t size, unsigned char width);

/**
 * @brief Prepare a DMA transfer.
 *
 * @param[in] config The DMA transfer configuration.
 * @return 0 on success, or a negative value on error.
 */
int dma_prepare(struct dma_transfer_config *config);

/**
 * @brief Stop a DMA channel.
 *
 * @param[in] ch_idx The DMA channel index.
 * @return 0 on success, or a negative value on error.
 */
int dma_stop(unsigned int ch_idx);

/**
 * @brief Start a DMA channel.
 *
 * @param[in] ch_idx The DMA channel index.
 * @return 0 on success, or a negative value on error.
 */
int dma_start(unsigned int ch_idx);

/**
 * @brief Wait for a DMA transfer to complete.
 *
 * @param[in] ch_idx The DMA channel index.
 * @return 0 when the transfer completes, or a negative value on error.
 */
int dma_wait(unsigned int ch_idx);


/*-----------------------------------------------------------*/

/**
 * @brief Initialize DMA interrupt handling.
 *
 * @return 0 on success, or a negative value on error.
 */
int dma_irq_init(void);

/**
 * @brief Enable interrupt handling for a DMA channel.
 *
 * @param[in] ch_idx The DMA channel index.
 * @return 0 on success, or a negative value on error.
 */
int dma_irq_enable(unsigned int ch_idx);

/**
 * @brief Disable interrupt handling for a DMA channel.
 *
 * @param[in] ch_idx The DMA channel index.
 * @return 0 on success, or a negative value on error.
 */
int dma_irq_disable(unsigned int ch_idx);

/**
 * @brief Wait for a DMA interrupt transfer completion.
 *
 * @param[in] ch_idx The DMA channel index.
 * @return 0 when the transfer completes, or a negative value on error.
 */
int dma_wait_irq(unsigned int ch_idx);

/**
 * @brief Print the DMA global interrupt status.
 */
void printf_global_status(void);

#define DW_DMA_CH_NUM          8


/*
 * SG2002 DW_axi_dmac
 *
 * Common registers:
 *     0x000 ~ 0x0FF
 *
 * Channel registers:
 *     CH1 : 0x100 ~ 0x1FF
 *     CH2 : 0x200 ~ 0x2FF
 *     ...
 *     CH8 : 0x800 ~ 0x8FF
 *
 * See SG2002 TRM Chapter 11.
 */


/*-----------------------------------------------------------*/

/**
 * @brief Common DMA registers.
 */

struct dmac_reg;


/**
 * @brief Registers for one DMA channel.
 *
 * Base offset of CHx:
 *     CH1 = 0x100
 *     CH2 = 0x200
 *     ...
 *     CH8 = 0x800
 */
struct dmac_ch_reg {
    volatile uint64_t sar;                  /* 0x00 CHx_SAR */
    volatile uint64_t dar;                  /* 0x08 CHx_DAR */
    volatile uint32_t block_ts;             /* 0x10 CHx_BLOCK_TS */
    volatile uint32_t reserved_0;           /* 0x14 */

    volatile uint64_t ctl;                  /* 0x18 CHx_CTL */
    volatile uint64_t cfg;                  /* 0x20 CHx_CFG */
    volatile uint64_t llp;                  /* 0x28 CHx_LLP */
    volatile uint64_t status;               /* 0x30 CHx_STATUSREG */

    volatile uint32_t swhssrc;              /* 0x38 CHx_SWHSSRCREG */
    volatile uint32_t reserved_1;           /* 0x3C */

    volatile uint32_t swhsdst;              /* 0x40 CHx_SWHSDSTREG */
    volatile uint32_t reserved_2;           /* 0x44 */

    volatile uint32_t blk_tfr_resume_req;   /* 0x48 */
    volatile uint32_t reserved_3;           /* 0x4C */

    volatile uint32_t axi_id;               /* 0x50 CHx_AXI_IDREG */
    volatile uint32_t reserved_4;           /* 0x54 */

    volatile uint32_t axi_qos;              /* 0x58 CHx_AXI_QOSREG */
    volatile uint32_t reserved_5;           /* 0x5C */

    volatile uint32_t sstat;                /* 0x60 CHx_SSTAT */
    volatile uint32_t reserved_6;           /* 0x64 */

    volatile uint32_t dstat;                /* 0x68 CHx_DSTAT */
    volatile uint32_t reserved_7;           /* 0x6C */

    volatile uint64_t sstatar;              /* 0x70 CHx_SSTATAR */
    volatile uint64_t dstatar;              /* 0x78 CHx_DSTATAR */

    volatile uint32_t intstatus_enable;     /* 0x80 */
    volatile uint32_t reserved_8;           /* 0x84 */

    volatile uint32_t intstatus;            /* 0x88 */
    volatile uint32_t reserved_9;           /* 0x8C */

    volatile uint32_t intsignal_enable;     /* 0x90 */
    volatile uint32_t reserved_10;          /* 0x94 */

    volatile uint32_t intclear;             /* 0x98 */
    volatile uint32_t reserved_11[25];      /* 0x9C ~ 0xFF */
};


/**
 * @brief Common DMA controller registers.
 */
struct dmac_reg {
    volatile uint64_t id;                   /* 0x000 */
    volatile uint32_t compver;              /* 0x008 */
    volatile uint32_t reserved_0;           /* 0x00C */

    volatile uint32_t cfg;                  /* 0x010 */
    volatile uint32_t reserved_1;           /* 0x014 */

    volatile uint64_t chen;                 /* 0x018 */

    uint8_t reserved_2[0x10];               /* 0x020 ~ 0x02F */

    volatile uint32_t intstatus;            /* 0x030 */
    volatile uint32_t reserved_3;           /* 0x034 */

    volatile uint32_t common_intclear;      /* 0x038 */
    volatile uint32_t reserved_4;           /* 0x03C */

    volatile uint32_t common_intstatus_enable; /* 0x040 */
    volatile uint32_t reserved_5;              /* 0x044 */

    volatile uint32_t common_intsignal_enable; /* 0x048 */
    volatile uint32_t reserved_6;              /* 0x04C */

    volatile uint32_t common_intstatus;      /* 0x050 */
    volatile uint32_t reserved_7;            /* 0x054 */

    volatile uint32_t reset;                 /* 0x058 */

    uint8_t reserved_8[0xA4];                /* 0x05C ~ 0x0FF */

    struct dmac_ch_reg ch[DW_DMA_CH_NUM];    /* 0x100 ~ */
};

struct dmac_int_response {
    uint32_t ch_status[DW_DMA_CH_NUM];
    uint32_t global_status;
};

/**
 * @brief Hardware Linked List Item (LLI).
 */
struct dma_lli {
    /* values that are not changed by hardware */
    volatile uint64_t sar;
    volatile uint64_t dar;
    volatile uint32_t block_ts;
    volatile uint32_t reserved_1;
    volatile uint64_t llp; /* chain to next lli */
    volatile uint64_t ctl;

	/* sstat and dstat can snapshot peripheral register state.
	 * silicon config may discard either or both...
	 */
    volatile uint32_t sstat;
    volatile uint32_t dstat;
    volatile uint64_t llp_status;
    volatile uint64_t reserved_2;
} __attribute__((aligned(64))) ;

/**
 * @brief DMA descriptor containing an LLI and software list node.
 */
struct dw_desc {

    struct dma_lli lli;
    struct list_head desc_node;

};


/*-----------------------------------------------------------*/

/**
 * @brief Structure layout checks.
 */

_Static_assert(offsetof(struct dmac_reg, id) == 0x000,
               "DMAC ID offset incorrect");

_Static_assert(offsetof(struct dmac_reg, compver) == 0x008,
               "DMAC COMPVER offset incorrect");

_Static_assert(offsetof(struct dmac_reg, cfg) == 0x010,
               "DMAC CFG offset incorrect");

_Static_assert(offsetof(struct dmac_reg, chen) == 0x018,
               "DMAC CHEN offset incorrect");

_Static_assert(offsetof(struct dmac_reg, intstatus) == 0x030,
               "DMAC INTSTATUS offset incorrect");

_Static_assert(offsetof(struct dmac_reg, common_intclear) == 0x038,
               "DMAC COMMON INTCLEAR offset incorrect");

_Static_assert(offsetof(struct dmac_reg, common_intstatus_enable) == 0x040,
               "DMAC COMMON INTSTATUS ENABLE offset incorrect");

_Static_assert(offsetof(struct dmac_reg, common_intsignal_enable) == 0x048,
               "DMAC COMMON INTSIGNAL ENABLE offset incorrect");

_Static_assert(offsetof(struct dmac_reg, common_intstatus) == 0x050,
               "DMAC COMMON INTSTATUS offset incorrect");

_Static_assert(offsetof(struct dmac_reg, reset) == 0x058,
               "DMAC RESET offset incorrect");

_Static_assert(offsetof(struct dmac_reg, ch[0]) == 0x100,
               "DMAC CH1 offset incorrect");

_Static_assert(offsetof(struct dmac_reg, ch[1]) == 0x200,
               "DMAC CH2 offset incorrect");

_Static_assert(offsetof(struct dmac_reg, ch[7]) == 0x800,
               "DMAC CH8 offset incorrect");

_Static_assert(sizeof(struct dmac_ch_reg) == 0x100,
               "DMA channel stride incorrect");

/*-----------------------------------------------------------*/

/**
 * @brief SG2002 DW_axi_dmac definitions.
 */

#define DMAC_BASE_ADDR              0x04330000UL

#define CLK_EN_1_ADDR               0x03002004UL
#define SOFT_RSTN_0_ADDR            0x03003000UL

#define CLK_EN_1_SDMA_AXI_BIT       (1U << 1)
#define SOFT_RSTN_0_SDMA_BIT        (1U << 18)

/* ============================================================
 * DMAC_CFGREG 0x010
 *
 * Bit 0 : DMAC_EN
 * Bit 1 : INT_EN
 * ============================================================
 */

#define DMAC_CFG_DMA_EN                 (1U << 0)
#define DMAC_CFG_INT_EN                 (1U << 1)


/* ============================================================
 * DMAC_CHENREG 0x018
 *
 * bit  0 ~ 7  : CH1_EN ~ CH8_EN
 * bit  8 ~ 15 : CH1_EN_WE ~ CH8_EN_WE
 * bit 16 ~ 23 : CH1_SUSP ~ CH8_SUSP
 * bit 24 ~ 31 : CH1_SUSP_WE ~ CH8_SUSP_WE
 * bit 32 ~ 39 : CH1_ABORT ~ CH8_ABORT
 * bit 40 ~ 47 : CH1_ABORT_WE ~ CH8_ABORT_WE
 * bit 48 ~ 63 : Reserved
 * ============================================================
 */

/* Channel enable */
#define DMAC_CHEN_EN(ch)                (1ULL << (ch))

/* Channel enable write-enable */
#define DMAC_CHEN_EN_WE(ch)             (1ULL << ((ch) + 8))

/* Channel suspend */
#define DMAC_CHEN_SUSP(ch)              (1ULL << ((ch) + 16))

/* Channel suspend write-enable */
#define DMAC_CHEN_SUSP_WE(ch)           (1ULL << ((ch) + 24))

/* Channel abort */
#define DMAC_CHEN_ABORT(ch)             (1ULL << ((ch) + 32))

/* Channel abort write-enable */
#define DMAC_CHEN_ABORT_WE(ch)          (1ULL << ((ch) + 40))


/* ============================================================
 * DMAC_INTSTATUSREG 0x030
 *
 * bit 0 ~ 7  : channel interrupt status
 * bit 16     : common register interrupt status
 * ============================================================
 */

#define DMAC_INTSTATUS_CH(ch)            (1U << (ch))
#define DMAC_INTSTATUS_COMMON            (1U << 16)


/* ============================================================
 * DMAC_COMMONREG_INTCLEARREG 0x038
 * ============================================================
 */

#define DMAC_COMMON_INTCLR_DEC_ERR       (1U << 0)
#define DMAC_COMMON_INTCLR_WR2RO_ERR     (1U << 1)
#define DMAC_COMMON_INTCLR_RD2WO_ERR     (1U << 2)
#define DMAC_COMMON_INTCLR_WRONHOLD_ERR  (1U << 3)
#define DMAC_COMMON_INTCLR_UNDEFINED_ERR (1U << 8)


/* ============================================================
 * DMAC_COMMONREG_INTSTATUS_ENABLEREG 0x040
 * ============================================================
 */

#define DMAC_COMMON_INTEN_DEC_ERR        (1U << 0)
#define DMAC_COMMON_INTEN_WR2RO_ERR      (1U << 1)
#define DMAC_COMMON_INTEN_RD2WO_ERR      (1U << 2)
#define DMAC_COMMON_INTEN_WRONHOLD_ERR   (1U << 3)
#define DMAC_COMMON_INTEN_UNDEFINED_ERR  (1U << 8)


/* ============================================================
 * DMAC_COMMONREG_INTSIGNAL_ENABLEREG 0x048
 * ============================================================
 */

#define DMAC_COMMON_INTSIG_DEC_ERR        (1U << 0)
#define DMAC_COMMON_INTSIG_WR2RO_ERR      (1U << 1)
#define DMAC_COMMON_INTSIG_RD2WO_ERR      (1U << 2)
#define DMAC_COMMON_INTSIG_WRONHOLD_ERR   (1U << 3)
#define DMAC_COMMON_INTSIG_UNDEFINED_ERR  (1U << 8)


/* ============================================================
 * DMAC_COMMONREG_INTSTATUSREG 0x050
 *
 * Status bits have the same bit positions as above.
 * ============================================================
 */

#define DMAC_COMMON_INTSTAT_DEC_ERR       (1U << 0)
#define DMAC_COMMON_INTSTAT_WR2RO_ERR     (1U << 1)
#define DMAC_COMMON_INTSTAT_RD2WO_ERR     (1U << 2)
#define DMAC_COMMON_INTSTAT_WRONHOLD_ERR  (1U << 3)
#define DMAC_COMMON_INTSTAT_UNDEFINED_ERR (1U << 8)


/* ============================================================
 * DMAC_RESETREG 0x058
 * ============================================================
 */

#define DMAC_RESET                       (1U << 0)


/* ============================================================
 * CHx_BLOCK_TS 0x110
 *
 * Bits 21:0 : BLOCK_TS
 * Bits 31:22: Reserved
 *
 * TRM:
 *   Block Transfer Size = BLOCK_TS + 1
 * ============================================================
 */

#define DMAC_BLOCK_TS_SHIFT             0
#define DMAC_BLOCK_TS_MASK              (0x003FFFFFU)

#define DMAC_BLOCK_TS(v) \
    (((uint32_t)(v)) & DMAC_BLOCK_TS_MASK)


/* ============================================================
 * CHx_CTL 0x118
 * ============================================================
 */

/* Bit 0 */
#define DMAC_CTL_SMS                     (1ULL << 0)

/* Bit 2 */
#define DMAC_CTL_DMS                     (1ULL << 2)

/* Bit 4 */
#define DMAC_CTL_SINC                    (1ULL << 4)

/* Bit 6 */
#define DMAC_CTL_DINC                    (1ULL << 6)

/*
 * SINC/DINC values
 * 0: Increment
 * 1: No Change (fixed peripheral FIFO address)
 */
#define DMAC_CTL_SINC_INC                0
#define DMAC_CTL_SINC_FIXED              DMAC_CTL_SINC
#define DMAC_CTL_DINC_INC                0
#define DMAC_CTL_DINC_FIXED              DMAC_CTL_DINC


/*
 * Source Transfer Width
 * bits 10:8
 */
#define DMAC_CTL_SRC_TR_WIDTH_SHIFT      8
#define DMAC_CTL_SRC_TR_WIDTH_MASK       (0x7ULL << 8)

#define DMAC_CTL_SRC_TR_WIDTH(v) \
    (((uint64_t)(v) << DMAC_CTL_SRC_TR_WIDTH_SHIFT) & \
     DMAC_CTL_SRC_TR_WIDTH_MASK)


/*
 * Destination Transfer Width
 * bits 13:11
 */
#define DMAC_CTL_DST_TR_WIDTH_SHIFT      11
#define DMAC_CTL_DST_TR_WIDTH_MASK       (0x7ULL << 11)

#define DMAC_CTL_DST_TR_WIDTH(v) \
    (((uint64_t)(v) << DMAC_CTL_DST_TR_WIDTH_SHIFT) & \
     DMAC_CTL_DST_TR_WIDTH_MASK)


/*
 * Source Burst Transaction Length
 * bits 17:14
 */
#define DMAC_CTL_SRC_MSIZE_SHIFT         14
#define DMAC_CTL_SRC_MSIZE_MASK          (0xFULL << 14)

#define DMAC_CTL_SRC_MSIZE(v) \
    (((uint64_t)(v) << DMAC_CTL_SRC_MSIZE_SHIFT) & \
     DMAC_CTL_SRC_MSIZE_MASK)


/*
 * Destination Burst Transaction Length
 * bits 21:18
 */
#define DMAC_CTL_DST_MSIZE_SHIFT         18
#define DMAC_CTL_DST_MSIZE_MASK          (0xFULL << 18)

#define DMAC_CTL_DST_MSIZE(v) \
    (((uint64_t)(v) << DMAC_CTL_DST_MSIZE_SHIFT) & \
     DMAC_CTL_DST_MSIZE_MASK)


/*
 * AXI ar_cache
 * bits 25:22
 */
#define DMAC_CTL_AR_CACHE_SHIFT          22
#define DMAC_CTL_AR_CACHE_MASK           (0xFULL << 22)

#define DMAC_CTL_AR_CACHE(v) \
    (((uint64_t)(v) << DMAC_CTL_AR_CACHE_SHIFT) & \
     DMAC_CTL_AR_CACHE_MASK)


/*
 * AXI aw_cache
 * bits 29:26
 */
#define DMAC_CTL_AW_CACHE_SHIFT          26
#define DMAC_CTL_AW_CACHE_MASK           (0xFULL << 26)

#define DMAC_CTL_AW_CACHE(v) \
    (((uint64_t)(v) << DMAC_CTL_AW_CACHE_SHIFT) & \
     DMAC_CTL_AW_CACHE_MASK)


/*
 * Non Posted Last Write Enable
 * bit 30
 */
#define DMAC_CTL_NONPOSTED_LASTWRITE_EN  (1ULL << 30)


/*
 * AXI ar_prot
 * bits 34:32
 */
#define DMAC_CTL_AR_PROT_SHIFT           32
#define DMAC_CTL_AR_PROT_MASK            (0x7ULL << 32)

#define DMAC_CTL_AR_PROT(v) \
    (((uint64_t)(v) << DMAC_CTL_AR_PROT_SHIFT) & \
     DMAC_CTL_AR_PROT_MASK)


/*
 * AXI aw_prot
 * bits 37:35
 */
#define DMAC_CTL_AW_PROT_SHIFT           35
#define DMAC_CTL_AW_PROT_MASK            (0x7ULL << 35)

#define DMAC_CTL_AW_PROT(v) \
    (((uint64_t)(v) << DMAC_CTL_AW_PROT_SHIFT) & \
     DMAC_CTL_AW_PROT_MASK)


/*
 * Source Burst Length Enable
 * bit 38
 */
#define DMAC_CTL_ARLEN_EN                (1ULL << 38)


/*
 * Source Burst Length
 * bits 46:39
 */
#define DMAC_CTL_ARLEN_SHIFT             39
#define DMAC_CTL_ARLEN_MASK              (0xFFULL << 39)

#define DMAC_CTL_ARLEN(v) \
    (((uint64_t)(v) << DMAC_CTL_ARLEN_SHIFT) & \
     DMAC_CTL_ARLEN_MASK)


/*
 * Destination Burst Length Enable
 * bit 47
 */
#define DMAC_CTL_AWLEN_EN                (1ULL << 47)


/*
 * Destination Burst Length
 * bits 55:48
 *
 * RO
 */
#define DMAC_CTL_AWLEN_SHIFT             48
#define DMAC_CTL_AWLEN_MASK              (0xFFULL << 48)


/*
 * Source Status Enable
 * bit 56
 */
#define DMAC_CTL_SRC_STAT_EN             (1ULL << 56)


/*
 * Destination Status Enable
 * bit 57
 */
#define DMAC_CTL_DST_STAT_EN             (1ULL << 57)


/*
 * Interrupt On Completion of Block Transfer
 * bit 58
 */
#define DMAC_CTL_IOC_BLK_TFR             (1ULL << 58)


/*
 * Last Shadow Register / Linked List Item
 * bit 62
 */
#define DMAC_CTL_SHADOWREG_OR_LLI_LAST   (1ULL << 62)


/*
 * Shadow Register / Linked List Item Valid
 * bit 63
 */
#define DMAC_CTL_SHADOWREG_OR_LLI_VALID  (1ULL << 63)


/* ============================================================
 * CHx_CFG 0x120
 * ============================================================
 */

/*
 * Source Multi Block Transfer Type
 * bits 1:0
 */
#define DMAC_CFG_SRC_MULTBLK_TYPE_SHIFT  0
#define DMAC_CFG_SRC_MULTBLK_TYPE_MASK   (0x3ULL << 0)

#define DMAC_CFG_SRC_MULTBLK_TYPE(v) \
    (((uint64_t)(v) << DMAC_CFG_SRC_MULTBLK_TYPE_SHIFT) & \
     DMAC_CFG_SRC_MULTBLK_TYPE_MASK)


/*
 * Destination Multi Block Transfer Type
 * bits 3:2
 */
#define DMAC_CFG_DST_MULTBLK_TYPE_SHIFT  2
#define DMAC_CFG_DST_MULTBLK_TYPE_MASK   (0x3ULL << 2)

#define DMAC_CFG_DST_MULTBLK_TYPE(v) \
    (((uint64_t)(v) << DMAC_CFG_DST_MULTBLK_TYPE_SHIFT) & \
     DMAC_CFG_DST_MULTBLK_TYPE_MASK)


#define DMAC_MULTBLK_CONTIGUOUS           0x0
#define DMAC_MULTBLK_RELOAD               0x1
#define DMAC_MULTBLK_SHADOW_REGISTER      0x2
#define DMAC_MULTBLK_LINKED_LIST          0x3


/*
 * Transfer Type and Flow Control
 * bits 34:32
 */
#define DMAC_CFG_TT_FC_SHIFT              32
#define DMAC_CFG_TT_FC_MASK               (0x7ULL << 32)

#define DMAC_CFG_TT_FC(v) \
    (((uint64_t)(v) << DMAC_CFG_TT_FC_SHIFT) & \
     DMAC_CFG_TT_FC_MASK)


#define DMAC_TT_FC_MEM_TO_MEM_DMAC        0x0
#define DMAC_TT_FC_MEM_TO_PER_DMAC        0x1
#define DMAC_TT_FC_PER_TO_MEM_DMAC        0x2
#define DMAC_TT_FC_PER_TO_PER_DMAC        0x3
#define DMAC_TT_FC_PER_TO_MEM_SRC         0x4
#define DMAC_TT_FC_PER_TO_PER_SRC         0x5
#define DMAC_TT_FC_MEM_TO_PER_DST         0x6
#define DMAC_TT_FC_PER_TO_PER_DST         0x7


/*
 * Source software/hardware handshaking
 * bit 35
 */
#define DMAC_CFG_HS_SEL_SRC               (1ULL << 35)


/*
 * Destination software/hardware handshaking
 * bit 36
 */
#define DMAC_CFG_HS_SEL_DST               (1ULL << 36)


/*
 * Source hardware handshake interface
 * bit 39
 *
 * NOTE:
 * TRM specifies SRC_PER as a field beginning at bit 39.
 * Width is 1 bit in the SG2002 configuration.
 */
#define DMAC_CFG_SRC_PER                  (1ULL << 39)


/*
 * Destination hardware handshake interface
 * bit 44
 *
 * See SG2002 TRM configuration.
 */
#define DMAC_CFG_DST_PER                  (1ULL << 44)

/* SG2002 exposes two hardware handshake interfaces: 0 or 1. */
// #define DMAC_CFG_SRC_PER_SEL(v) \
//     ((((uint64_t)(v)) & 0x1ULL) << 39)
// #define DMAC_CFG_DST_PER_SEL(v) \
//     ((((uint64_t)(v)) & 0x1ULL) << 44)
#define DMAC_CFG_SRC_PER_SEL(v) \
    ((((uint64_t)(v)) & 0x1FULL) << 39)
#define DMAC_CFG_DST_PER_SEL(v) \
    ((((uint64_t)(v)) & 0x1FULL) << 44)


/*
 * Channel priority
 * bits 51:49
 */
#define DMAC_CFG_CH_PRIOR_SHIFT           49
#define DMAC_CFG_CH_PRIOR_MASK            (0x7ULL << 49)

#define DMAC_CFG_CH_PRIOR(v) \
    (((uint64_t)(v) << DMAC_CFG_CH_PRIOR_SHIFT) & \
     DMAC_CFG_CH_PRIOR_MASK)


/*
 * Channel lock
 * bit 52
 */
#define DMAC_CFG_LOCK_CH                  (1ULL << 52)


/*
 * Channel lock level
 * bits 54:53
 */
#define DMAC_CFG_LOCK_CH_L_SHIFT          53
#define DMAC_CFG_LOCK_CH_L_MASK           (0x3ULL << 53)

#define DMAC_CFG_LOCK_CH_L(v) \
    (((uint64_t)(v) << DMAC_CFG_LOCK_CH_L_SHIFT) & \
     DMAC_CFG_LOCK_CH_L_MASK)


#define DMAC_LOCK_LEVEL_DMA_TRANSFER      0x0
#define DMAC_LOCK_LEVEL_BLOCK_TRANSFER    0x1


/*
 * Source Outstanding Request Limit
 * bits 58:55
 *
 * Actual limit = field + 1
 */
#define DMAC_CFG_SRC_OSR_LMT_SHIFT        55
#define DMAC_CFG_SRC_OSR_LMT_MASK         (0xFULL << 55)

#define DMAC_CFG_SRC_OSR_LMT(v) \
    (((uint64_t)(v) << DMAC_CFG_SRC_OSR_LMT_SHIFT) & \
     DMAC_CFG_SRC_OSR_LMT_MASK)


/*
 * Destination Outstanding Request Limit
 * bits 62:59
 *
 * Actual limit = field + 1
 */
#define DMAC_CFG_DST_OSR_LMT_SHIFT        59
#define DMAC_CFG_DST_OSR_LMT_MASK         (0xFULL << 59)

#define DMAC_CFG_DST_OSR_LMT(v) \
    (((uint64_t)(v) << DMAC_CFG_DST_OSR_LMT_SHIFT) & \
     DMAC_CFG_DST_OSR_LMT_MASK)


/* ============================================================
 * CHx_LLP 0x128
 * ============================================================
 */

/*
 * LLI Master Select
 * bit 0
 */
#define DMAC_LLP_LMS                     (1ULL << 0)


/*
 * LLI location
 * bits 63:6
 *
 * Address must be 64-byte aligned.
 */
#define DMAC_LLP_LOC_SHIFT               6
#define DMAC_LLP_LOC_MASK                (~0ULL << 6)

#define DMAC_LLP_LOC(addr) \
    (((uint64_t)(addr)) & DMAC_LLP_LOC_MASK)


/* ============================================================
 * CHx_STATUSREG 0x130
 * ============================================================
 */

/*
 * Completed block transfer size
 * bits 21:0
 */
#define DMAC_STATUS_CMPLTD_BLK_TFR_SIZE_MASK \
    0x003FFFFFU


/*
 * Data left in FIFO
 * bits 46:32
 */
#define DMAC_STATUS_DATA_LEFT_IN_FIFO_SHIFT 32
#define DMAC_STATUS_DATA_LEFT_IN_FIFO_MASK  \
    (0x7FFFULL << 32)


/* ============================================================
 * CHx_SWHSSRCREG 0x138
 * ============================================================
 */

#define DMAC_SWHSSRC_REQ                  (1U << 0)
#define DMAC_SWHSSRC_REQ_WE               (1U << 1)
#define DMAC_SWHSSRC_SGLREQ               (1U << 2)
#define DMAC_SWHSSRC_SGLREQ_WE            (1U << 3)
#define DMAC_SWHSSRC_LST                  (1U << 4)
#define DMAC_SWHSSRC_LST_WE               (1U << 5)


/* ============================================================
 * CHx_SWHSDSTREG 0x140
 * ============================================================
 */

#define DMAC_SWHSDST_REQ                  (1U << 0)
#define DMAC_SWHSDST_REQ_WE               (1U << 1)
#define DMAC_SWHSDST_SGLREQ               (1U << 2)
#define DMAC_SWHSDST_SGLREQ_WE            (1U << 3)
#define DMAC_SWHSDST_LST                  (1U << 4)
#define DMAC_SWHSDST_LST_WE               (1U << 5)


/* ============================================================
 * CHx_BLK_TFR_RESUMEREQREG 0x148
 * ============================================================
 */

#define DMAC_BLK_TFR_RESUME_REQ           (1U << 0)


/* ============================================================
 * CHx_AXI_IDREG 0x150
 * ============================================================
 */

#define DMAC_AXI_READ_ID_SHIFT            0
#define DMAC_AXI_READ_ID_MASK             0x7FFFU

#define DMAC_AXI_READ_ID(v) \
    (((uint32_t)(v)) & DMAC_AXI_READ_ID_MASK)


#define DMAC_AXI_WRITE_ID_SHIFT           16
#define DMAC_AXI_WRITE_ID_MASK            (0x7FFFU << 16)

#define DMAC_AXI_WRITE_ID(v) \
    (((uint32_t)(v) << DMAC_AXI_WRITE_ID_SHIFT) & \
     DMAC_AXI_WRITE_ID_MASK)


/* ============================================================
 * CHx_AXI_QOSREG 0x158
 * ============================================================
 */

#define DMAC_AXI_AWQOS_SHIFT              0
#define DMAC_AXI_AWQOS_MASK               0xFU

#define DMAC_AXI_AWQOS(v) \
    (((uint32_t)(v) << DMAC_AXI_AWQOS_SHIFT) & \
     DMAC_AXI_AWQOS_MASK)


#define DMAC_AXI_ARQOS_SHIFT              4
#define DMAC_AXI_ARQOS_MASK               (0xFU << 4)

#define DMAC_AXI_ARQOS(v) \
    (((uint32_t)(v) << DMAC_AXI_ARQOS_SHIFT) & \
     DMAC_AXI_ARQOS_MASK)


/* ============================================================
 * CHx_INTSTATUS_ENABLEREG 0x180
 * ============================================================
 */

/*
 * bits 0 ~ 1
 */
#define DMAC_CH_INTEN_BLOCK_TFR_DONE       (1U << 0)
#define DMAC_CH_INTEN_DMA_TFR_DONE         (1U << 1)

/* bit 2 reserved */

/*
 * bits 3 ~ 4
 */
#define DMAC_CH_INTEN_SRC_TRANSCOMP        (1U << 3)
#define DMAC_CH_INTEN_DST_TRANSCOMP        (1U << 4)

/*
 * bits 5 ~ 8
 */
#define DMAC_CH_INTEN_SRC_DEC_ERR          (1U << 5)
#define DMAC_CH_INTEN_DST_DEC_ERR          (1U << 6)
#define DMAC_CH_INTEN_SRC_SLV_ERR          (1U << 7)
#define DMAC_CH_INTEN_DST_SLV_ERR          (1U << 8)

/*
 * bits 9 ~ 14
 */
#define DMAC_CH_INTEN_LLI_RD_DEC_ERR       (1U << 9)
#define DMAC_CH_INTEN_LLI_WR_DEC_ERR       (1U << 10)
#define DMAC_CH_INTEN_LLI_RD_SLV_ERR       (1U << 11)
#define DMAC_CH_INTEN_LLI_WR_SLV_ERR       (1U << 12)
#define DMAC_CH_INTEN_LLI_INVALID_ERR      (1U << 13)
#define DMAC_CH_INTEN_MULTIBLKTYPE_ERR     (1U << 14)

/*
 * bits 16 ~ 21
 */
#define DMAC_CH_INTEN_SLVIF_DEC_ERR            (1U << 16)
#define DMAC_CH_INTEN_SLVIF_WR2RO_ERR          (1U << 17)
#define DMAC_CH_INTEN_SLVIF_RD2RWO_ERR         (1U << 18)
#define DMAC_CH_INTEN_SLVIF_WRONCHEN_ERR       (1U << 19)
#define DMAC_CH_INTEN_SHADOW_WRONVALID_ERR     (1U << 20)
#define DMAC_CH_INTEN_SLVIF_WRONHOLD_ERR       (1U << 21)

/*
 * bits 27 ~ 31
 */
#define DMAC_CH_INTEN_LOCK_CLEARED             (1U << 27)
#define DMAC_CH_INTEN_SRC_SUSPENDED            (1U << 28)
#define DMAC_CH_INTEN_SUSPENDED                (1U << 29)
#define DMAC_CH_INTEN_DISABLED                 (1U << 30)
#define DMAC_CH_INTEN_ABORTED                  (1U << 31)


/* ============================================================
 * CHx_INTSTATUS_ENABLE error mask
 * ============================================================
 */

#define DMAC_CH_INTEN_ERR_MASK \
    (DMAC_CH_INTEN_SRC_DEC_ERR      | \
     DMAC_CH_INTEN_DST_DEC_ERR      | \
     DMAC_CH_INTEN_SRC_SLV_ERR      | \
     DMAC_CH_INTEN_DST_SLV_ERR      | \
     DMAC_CH_INTEN_LLI_RD_DEC_ERR   | \
     DMAC_CH_INTEN_LLI_WR_DEC_ERR   | \
     DMAC_CH_INTEN_LLI_RD_SLV_ERR   | \
     DMAC_CH_INTEN_LLI_WR_SLV_ERR   | \
     DMAC_CH_INTEN_LLI_INVALID_ERR  | \
     DMAC_CH_INTEN_MULTIBLKTYPE_ERR)


/* ============================================================
 * CHx_INTSTATUS 0x188
 *
 * Same bit positions as CHx_INTSTATUS_ENABLEREG.
 * ============================================================
 */

#define DMAC_CH_INTSTAT_BLOCK_TFR_DONE       (1U << 0)
#define DMAC_CH_INTSTAT_DMA_TFR_DONE         (1U << 1)

#define DMAC_CH_INTSTAT_SRC_TRANSCOMP        (1U << 3)
#define DMAC_CH_INTSTAT_DST_TRANSCOMP        (1U << 4)

#define DMAC_CH_INTSTAT_SRC_DEC_ERR          (1U << 5)
#define DMAC_CH_INTSTAT_DST_DEC_ERR          (1U << 6)
#define DMAC_CH_INTSTAT_SRC_SLV_ERR          (1U << 7)
#define DMAC_CH_INTSTAT_DST_SLV_ERR          (1U << 8)

#define DMAC_CH_INTSTAT_LLI_RD_DEC_ERR       (1U << 9)
#define DMAC_CH_INTSTAT_LLI_WR_DEC_ERR       (1U << 10)
#define DMAC_CH_INTSTAT_LLI_RD_SLV_ERR       (1U << 11)
#define DMAC_CH_INTSTAT_LLI_WR_SLV_ERR       (1U << 12)
#define DMAC_CH_INTSTAT_LLI_INVALID_ERR      (1U << 13)
#define DMAC_CH_INTSTAT_MULTIBLKTYPE_ERR     (1U << 14)

#define DMAC_CH_INTSTAT_SLVIF_DEC_ERR        (1U << 16)
#define DMAC_CH_INTSTAT_SLVIF_WR2RO_ERR      (1U << 17)
#define DMAC_CH_INTSTAT_SLVIF_RD2RWO_ERR     (1U << 18)
#define DMAC_CH_INTSTAT_SLVIF_WRONCHEN_ERR   (1U << 19)
#define DMAC_CH_INTSTAT_SHADOW_WRONVALID_ERR (1U << 20)
#define DMAC_CH_INTSTAT_SLVIF_WRONHOLD_ERR   (1U << 21)

#define DMAC_CH_INTSTAT_LOCK_CLEARED         (1U << 27)
#define DMAC_CH_INTSTAT_SRC_SUSPENDED        (1U << 28)
#define DMAC_CH_INTSTAT_SUSPENDED            (1U << 29)
#define DMAC_CH_INTSTAT_DISABLED             (1U << 30)
#define DMAC_CH_INTSTAT_ABORTED              (1U << 31)


/* ============================================================
 * CHx_INTSTATUS error mask
 * ============================================================
 */

#define DMAC_CH_INTSTAT_ERR_MASK \
    (DMAC_CH_INTSTAT_SRC_DEC_ERR      | \
     DMAC_CH_INTSTAT_DST_DEC_ERR      | \
     DMAC_CH_INTSTAT_SRC_SLV_ERR      | \
     DMAC_CH_INTSTAT_DST_SLV_ERR      | \
     DMAC_CH_INTSTAT_LLI_RD_DEC_ERR   | \
     DMAC_CH_INTSTAT_LLI_WR_DEC_ERR   | \
     DMAC_CH_INTSTAT_LLI_RD_SLV_ERR   | \
     DMAC_CH_INTSTAT_LLI_WR_SLV_ERR   | \
     DMAC_CH_INTSTAT_LLI_INVALID_ERR  | \
     DMAC_CH_INTSTAT_MULTIBLKTYPE_ERR)


/*
 * Interrupts handled by DMA interrupt mode:
 *
 *     DMA_TFR_DONE
 *     +
 *     error interrupts
 */
#define DMAC_CH_INTSTAT_DMA_IRQ_MASK \
    (DMAC_CH_INTSTAT_DMA_TFR_DONE | \
     DMAC_CH_INTSTAT_ERR_MASK)


/* ============================================================
 * CHx_INTSIGNAL_ENABLEREG 0x190
 * ============================================================
 */

#define DMAC_CH_INTSIG_BLOCK_TFR_DONE       (1U << 0)
#define DMAC_CH_INTSIG_DMA_TFR_DONE         (1U << 1)

#define DMAC_CH_INTSIG_SRC_TRANSCOMP        (1U << 3)
#define DMAC_CH_INTSIG_DST_TRANSCOMP        (1U << 4)

#define DMAC_CH_INTSIG_SRC_DEC_ERR          (1U << 5)
#define DMAC_CH_INTSIG_DST_DEC_ERR          (1U << 6)
#define DMAC_CH_INTSIG_SRC_SLV_ERR          (1U << 7)
#define DMAC_CH_INTSIG_DST_SLV_ERR          (1U << 8)

#define DMAC_CH_INTSIG_LLI_RD_DEC_ERR       (1U << 9)
#define DMAC_CH_INTSIG_LLI_WR_DEC_ERR       (1U << 10)
#define DMAC_CH_INTSIG_LLI_RD_SLV_ERR       (1U << 11)
#define DMAC_CH_INTSIG_LLI_WR_SLV_ERR       (1U << 12)
#define DMAC_CH_INTSIG_LLI_INVALID_ERR      (1U << 13)
#define DMAC_CH_INTSIG_MULTIBLKTYPE_ERR     (1U << 14)

#define DMAC_CH_INTSIG_SLVIF_DEC_ERR        (1U << 16)
#define DMAC_CH_INTSIG_SLVIF_WR2RO_ERR      (1U << 17)
#define DMAC_CH_INTSIG_SLVIF_RD2RWO_ERR     (1U << 18)
#define DMAC_CH_INTSIG_SLVIF_WRONCHEN_ERR   (1U << 19)
#define DMAC_CH_INTSIG_SHADOW_WRONVALID_ERR (1U << 20)
#define DMAC_CH_INTSIG_SLVIF_WRONHOLD_ERR   (1U << 21)

#define DMAC_CH_INTSIG_LOCK_CLEARED         (1U << 27)
#define DMAC_CH_INTSIG_SRC_SUSPENDED        (1U << 28)
#define DMAC_CH_INTSIG_SUSPENDED            (1U << 29)
#define DMAC_CH_INTSIG_DISABLED             (1U << 30)
#define DMAC_CH_INTSIG_ABORTED              (1U << 31)


/* ============================================================
 * CHx_INTSIGNAL_ENABLE error mask
 * ============================================================
 */

#define DMAC_CH_INTSIG_ERR_MASK \
    (DMAC_CH_INTSIG_SRC_DEC_ERR      | \
     DMAC_CH_INTSIG_DST_DEC_ERR      | \
     DMAC_CH_INTSIG_SRC_SLV_ERR      | \
     DMAC_CH_INTSIG_DST_SLV_ERR      | \
     DMAC_CH_INTSIG_LLI_RD_DEC_ERR   | \
     DMAC_CH_INTSIG_LLI_WR_DEC_ERR   | \
     DMAC_CH_INTSIG_LLI_RD_SLV_ERR   | \
     DMAC_CH_INTSIG_LLI_WR_SLV_ERR   | \
     DMAC_CH_INTSIG_LLI_INVALID_ERR  | \
     DMAC_CH_INTSIG_MULTIBLKTYPE_ERR)


/* ============================================================
 * CHx_INTCLEARREG 0x198
 *
 * Write 1 to the corresponding bit to clear interrupt.
 * ============================================================
 */

#define DMAC_CH_INTCLR_BLOCK_TFR_DONE       (1U << 0)
#define DMAC_CH_INTCLR_DMA_TFR_DONE         (1U << 1)

#define DMAC_CH_INTCLR_SRC_TRANSCOMP        (1U << 3)
#define DMAC_CH_INTCLR_DST_TRANSCOMP        (1U << 4)

#define DMAC_CH_INTCLR_SRC_DEC_ERR          (1U << 5)
#define DMAC_CH_INTCLR_DST_DEC_ERR          (1U << 6)
#define DMAC_CH_INTCLR_SRC_SLV_ERR          (1U << 7)
#define DMAC_CH_INTCLR_DST_SLV_ERR          (1U << 8)

#define DMAC_CH_INTCLR_LLI_RD_DEC_ERR       (1U << 9)
#define DMAC_CH_INTCLR_LLI_WR_DEC_ERR       (1U << 10)
#define DMAC_CH_INTCLR_LLI_RD_SLV_ERR       (1U << 11)
#define DMAC_CH_INTCLR_LLI_WR_SLV_ERR       (1U << 12)
#define DMAC_CH_INTCLR_LLI_INVALID_ERR      (1U << 13)
#define DMAC_CH_INTCLR_MULTIBLKTYPE_ERR     (1U << 14)

#define DMAC_CH_INTCLR_SLVIF_DEC_ERR        (1U << 16)
#define DMAC_CH_INTCLR_SLVIF_WR2RO_ERR      (1U << 17)
#define DMAC_CH_INTCLR_SLVIF_RD2RWO_ERR     (1U << 18)
#define DMAC_CH_INTCLR_SLVIF_WRONCHEN_ERR   (1U << 19)
#define DMAC_CH_INTCLR_SHADOW_WRONVALID_ERR (1U << 20)
#define DMAC_CH_INTCLR_SLVIF_WRONHOLD_ERR   (1U << 21)

#define DMAC_CH_INTCLR_LOCK_CLEARED         (1U << 27)
#define DMAC_CH_INTCLR_SRC_SUSPENDED        (1U << 28)
#define DMAC_CH_INTCLR_SUSPENDED            (1U << 29)
#define DMAC_CH_INTCLR_DISABLED             (1U << 30)
#define DMAC_CH_INTCLR_ABORTED              (1U << 31)


/* ============================================================
 * CHx_INTCLEAR error mask
 * ============================================================
 */

#define DMAC_CH_INTCLR_ERR_MASK \
    (DMAC_CH_INTCLR_SRC_DEC_ERR      | \
     DMAC_CH_INTCLR_DST_DEC_ERR      | \
     DMAC_CH_INTCLR_SRC_SLV_ERR      | \
     DMAC_CH_INTCLR_DST_SLV_ERR      | \
     DMAC_CH_INTCLR_LLI_RD_DEC_ERR   | \
     DMAC_CH_INTCLR_LLI_WR_DEC_ERR   | \
     DMAC_CH_INTCLR_LLI_RD_SLV_ERR   | \
     DMAC_CH_INTCLR_LLI_WR_SLV_ERR   | \
     DMAC_CH_INTCLR_LLI_INVALID_ERR  | \
     DMAC_CH_INTCLR_MULTIBLKTYPE_ERR)

enum SYS_DMA_CH_MAP {
    DMA_RX_REQ_I2S0      = 0,
    DMA_TX_REQ_I2S0      = 1,
    DMA_RX_REQ_I2S1      = 2,
    DMA_TX_REQ_I2S1      = 3,
    DMA_RX_REQ_I2S2      = 4,
    DMA_TX_REQ_I2S2      = 5,
    DMA_RX_REQ_I2S3      = 6,
    DMA_TX_REQ_I2S3      = 7,

    DMA_RX_REQ_N_UART0   = 8,
    DMA_TX_REQ_N_UART0   = 9,
    DMA_RX_REQ_N_UART1   = 10,
    DMA_TX_REQ_N_UART1   = 11,
    DMA_RX_REQ_N_UART2   = 12,
    DMA_TX_REQ_N_UART2   = 13,
    DMA_RX_REQ_N_UART3   = 14,
    DMA_TX_REQ_N_UART3   = 15,

    DMA_RX_REQ_SPI0      = 16,
    DMA_TX_REQ_SPI0      = 17,
    DMA_RX_REQ_SPI1      = 18,
    DMA_TX_REQ_SPI1      = 19,
    DMA_RX_REQ_SPI2      = 20,
    DMA_TX_REQ_SPI2      = 21,
    DMA_RX_REQ_SPI3      = 22,
    DMA_TX_REQ_SPI3      = 23,

    DMA_RX_REQ_I2C0      = 24,
    DMA_TX_REQ_I2C0      = 25,
    DMA_RX_REQ_I2C1      = 26,
    DMA_TX_REQ_I2C1      = 27,
    DMA_RX_REQ_I2C2      = 28,
    DMA_TX_REQ_I2C2      = 29,
    DMA_RX_REQ_I2C3      = 30,
    DMA_TX_REQ_I2C3      = 31,

    DMA_RX_REQ_I2C4      = 32,
    DMA_TX_REQ_I2C4      = 33,

    DMA_RX_REQ_TDM0      = 34,
    DMA_TX_REQ_TDM0      = 35,
    DMA_RX_REQ_TDM1      = 36,

    DMA_REQ_AUDSRC       = 37,
    DMA_REQ_SPI_NAND     = 38,
    DMA_REQ_SPI_NOR      = 39,

    DMA_RX_REQ_N_UART4   = 40,
    DMA_TX_REQ_N_UART4   = 41,

    DMA_REQ_SPI_NOR1     = 42,

};

#define DMA_TX_REQ_N_UART(n) (DMA_TX_REQ_N_UART0 + ((n) << 1))
#define DMA_RX_REQ_N_UART(n) (DMA_RX_REQ_N_UART0 + ((n) << 1))

#endif /* __HAL_DW_DMA_H_ */