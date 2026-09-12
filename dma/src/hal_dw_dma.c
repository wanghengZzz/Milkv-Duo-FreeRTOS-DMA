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
 *
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#include "hal_dw_lli.h"
#include "top_reg.h"
#include "intr_conf.h"
#include "hal_dw_utils.h"


extern void inv_dcache_range(uintptr_t start, size_t size);
extern void flush_dcache_range(uintptr_t start, size_t size);

static volatile struct dmac_reg * const dmac =
    (volatile struct dmac_reg *)DMAC_BASE_ADDR;

static volatile uint32_t * const clk_en_1 =
    (volatile uint32_t *)CLK_EN_1_ADDR;

static volatile uint32_t * const soft_rstn_0 =
    (volatile uint32_t *)SOFT_RSTN_0_ADDR;

static volatile uint32_t * const dma_int_mux =
    (volatile uint32_t *)DMA_INT_MUX;

static volatile uint32_t * const dma_ch_remap0 =
    (volatile uint32_t *)DMA_CH_REMAP0;


static volatile uint32_t * const dma_ch_remap1 =
    (volatile uint32_t *)DMA_CH_REMAP1;

/**
 * @brief Builds the CHx_CTL value for a DMA transfer.
 *
 * @param[in] ucWidth Transfer-width encoding used by the DMA controller.
 * @param[in] ucBurst Source and destination burst-size encoding.
 * @param[in] eType Transfer direction and endpoint type.
 *
 * @return The CHx_CTL value, or 0 for an invalid transfer type.
 *
 * @note Peripheral endpoints use fixed addresses because the peripheral
 *       register/FIFO address must not advance between transfers.
 */
uint64_t dma_get_ctl(unsigned char width, unsigned char burst, enum dma_transfer_type type)
{
    uint64_t ctl = 0;
    ctl |= DMAC_CTL_SRC_TR_WIDTH(width);
    ctl |= DMAC_CTL_DST_TR_WIDTH(width);
    ctl |= DMAC_CTL_SRC_MSIZE(burst);
    ctl |= DMAC_CTL_DST_MSIZE(burst);
    ctl |= DMAC_CTL_AR_PROT(0);
    ctl |= DMAC_CTL_AW_PROT(0);

    switch (type) {

    case DMA_TRANSFER_MEM_TO_MEM:

        /* SINC=0, DINC=0: increment both addresses. */

        ctl |= DMAC_CTL_DINC_INC;
        ctl |= DMAC_CTL_SINC_INC;

        break;

    case DMA_TRANSFER_MEM_TO_DEV:

        /* SINC=0: memory increments. DINC=1: peripheral fixed. */
        ctl |= DMAC_CTL_DINC_FIXED;
        ctl |= DMAC_CTL_SINC_INC;

        break;

    case DMA_TRANSFER_DEV_TO_MEM:

        /* SINC=1: peripheral fixed. DINC=0: memory increments. */

        ctl |= DMAC_CTL_SINC_FIXED;
        ctl |= DMAC_CTL_DINC_INC;

        break;

    case DMA_TRANSFER_DEV_TO_DEV:

        /* SINC=1: peripheral fixed. DINC=1: peripheral fixed. */

        ctl |= DMAC_CTL_SINC_FIXED;
        ctl |= DMAC_CTL_DINC_FIXED;

        break;

    default:
        return -1;
    }

    return ctl;
}

/*-----------------------------------------------------------*/
/* DMA interrupt state                                      */
/*-----------------------------------------------------------*/

static volatile uint8_t dma_irq_done[DW_DMA_CH_NUM];

static volatile int dma_irq_result[DW_DMA_CH_NUM];

static volatile uint32_t dma_irq_status[DW_DMA_CH_NUM];

static volatile uint8_t dma_irq_registered;

static volatile struct dmac_int_response dma_int_res = {0};

/*-----------------------------------------------------------*/
/* Transfer width helpers                                   */
/*
 * DW_axi_dmac transfer-width encoding used by this driver:
 *     0x0 = 8-bit
 *     0x1 = 16-bit
 *     0x2 = 32-bit
 *     0x3 = 64-bit
 *
 * Values 0x4 to 0x7 are not used by this driver.
 */
/*-----------------------------------------------------------*/

/**
 * @brief Converts the DMA transfer-width encoding to a byte count.
 *
 * @param[in] xWidth DMA transfer-width encoding.
 * @param[out] pxWidthBytes Address that receives the number of bytes
 *                           represented by xWidth.
 *
 * @return pdPASS-style zero on success, or -1 if the width is unsupported
 *         or pxWidthBytes is NULL.
 */
int dma_width_to_bytes(size_t width, size_t *width_bytes)
{
    if (width_bytes == NULL)
        return -1;

    switch (width) {
    case 0x0:
        *width_bytes = 1;
        break;

    case 0x1:
        *width_bytes = 2;
        break;

    case 0x2:
        *width_bytes = 4;
        break;

    case 0x3:
        *width_bytes = 8;
        break;

    default:
        return -1;
    }

    return 0;
}

/**
 * @brief Converts a byte count to the value programmed into CHx_BLOCK_TS.
 *
 * @param[in] xSize Number of bytes in the DMA transfer.
 * @param[in] ucWidth DMA transfer-width encoding.
 *
 * @return The encoded BLOCK_TS value. Zero is returned for an invalid
 *         or unsupported transfer size.
 *
 * @note DW_axi_dmac stores the transfer count minus one rather than
 *       the byte count minus one.
 */
uint32_t dma_get_block_ts(size_t size, unsigned char width)
{
    size_t width_bytes;
    size_t transfer_count;

    if (size == 0)
    {
        debug_dmac("SIZE MUST NOT BE ZERO\n");
        return 0;
    }

    if (dma_width_to_bytes(width, &width_bytes) != 0)
    {
        debug_dmac("DMA WIDTH TO BYTES ERROR\n");
        return 0;
    }

    if (size % width_bytes != 0)
    {
        debug_dmac("DMA size is not aligned to transfer width\n");
        return 0;
    }

    transfer_count = size / width_bytes;

    if (transfer_count > (size_t)(DMAC_BLOCK_TS_MASK + 1ULL))
    {
        debug_dmac("transfer_count is too great\n");
        return 0;
    }

    /* BLOCK_TS stores transfer_count - 1. */
    return DMAC_BLOCK_TS((uint32_t)(transfer_count - 1));
}


/*-----------------------------------------------------------*/
/* DMA clock control                                          */
/*-----------------------------------------------------------*/

/**
 * @brief Enables the SG2002 system DMA clock.
 */
static void dma_clock_enable(void)
{
    *clk_en_1 |= CLK_EN_1_SDMA_AXI_BIT;
}


/*-----------------------------------------------------------*/
/* DMA reset control                                           */
/*-----------------------------------------------------------*/

/**
 * @brief Resets the SG2002 system DMA block.
 */
static void dma_reset(void)
{
    /*
     * Assert reset.
     */
    *soft_rstn_0 &= ~SOFT_RSTN_0_SDMA_BIT;

    /*
     * Release reset.
     */
    *soft_rstn_0 |= SOFT_RSTN_0_SDMA_BIT;
}


/*-----------------------------------------------------------*/
/* DMA channel enable / disable                               */
/*-----------------------------------------------------------*/

/**
 * @brief Enables a DMA channel.
 *
 * @param[in] uxChannel DMA channel number.
 *
 * @note SG2002 uses a separate write-enable bit for the channel enable
 *       field, so both bits are written together.
 */
static void dma_channel_enable(unsigned int ch)
{
    uint64_t value;

    value =
        DMAC_CHEN_EN(ch) |
        DMAC_CHEN_EN_WE(ch);

    dmac->chen = value;
}


/**
 * @brief Disables a DMA channel.
 *
 * @param[in] uxChannel DMA channel number.
 */
static void dma_channel_disable(unsigned int ch)
{
    dmac->chen =
        DMAC_CHEN_EN_WE(ch);
}

/*-----------------------------------------------------------*/
/* System DMA peripheral request remapping                     */
/*-----------------------------------------------------------*/

/**
 * @brief Maps a peripheral request line to a DMA channel.
 *
 * @param[in] uxChannel DMA channel number.
 * @param[in] ucRequestId SG2002 peripheral request-line identifier.
 *
 * @return 0 on success, or -1 if the channel or request identifier is invalid.
 *
 * @note The remap register is selected from the channel number and the
 *       selected field is committed using the SG2002 update bit.
 */
static int dma_map_request(unsigned int ch_idx, unsigned char req_id)
{
    volatile uint32_t *reg;
    uint32_t value;
    unsigned int shift;

    if (ch_idx >= DW_DMA_CH_NUM)
        return -1;

    if (req_id > DMA_CH_REMAP_FIELD_MASK)
        return -1;

    if (ch_idx < 4)
        reg = dma_ch_remap0;
    else
        reg = dma_ch_remap1;

    shift = DMA_CH_REMAP_SHIFT(ch_idx);

    value = *reg;

    value &= ~(DMA_CH_REMAP_FIELD_MASK << shift);
    value |= ((uint32_t)req_id & DMA_CH_REMAP_FIELD_MASK) << shift;

    /* Commit the new request mapping with the SG2002 remap update bit. */
    *reg = value | DMA_CH_REMAP_UPDATE;

    return 0;
}

/*-----------------------------------------------------------*/
/* DMA initialisation                                          */
/*-----------------------------------------------------------*/

/**
 * @brief Initialises the DMA controller.
 *
 * Enables the AXI clock, resets the controller, enables the DMAC block,
 * and leaves all DMA channels disabled.
 *
 * @return 0 on success.
 *
 * @note Global DMA interrupt generation is intentionally left disabled.
 *       Interrupt mode enables it explicitly through dma_irq_init().
 */
int dma_init(void)
{
    unsigned int ch;

    /*
     * 1. Enable DMAC AXI clock.
     */
    dma_clock_enable();

    /*
     * 2. Reset DMAC.
     */
    dma_reset();

    /*
     * 3. Enable DMAC.
     *
     * External DMA interrupt remains disabled here.
     * Interrupt mode enables INT_EN explicitly in
     * dma_irq_init().
     */
    dmac->cfg = DMAC_CFG_DMA_EN;

    /*
     * 4. Disable all channels.
     */
    for (ch = 0; ch < DW_DMA_CH_NUM; ++ch)
        dma_channel_disable(ch);

    return 0;
}

/*-----------------------------------------------------------*/
/* DMA interrupt routing                                      */
/*-----------------------------------------------------------*/

/**
 * @brief Routes a DMA channel interrupt to the CPU2 DMA interrupt output.
 *
 * @param[in] uxChannel DMA channel number.
 */
static void dma_irq_route_enable(unsigned int ch_idx)
{
    uint32_t value;

    value = *dma_int_mux;

    debug_dmac("BEFORE ROUTE: %x\n", *dma_int_mux);
    value |= DMA_INT_MUX_CPU2_CH_BIT(ch_idx);
    value |= DMA_INT_MUX_CPU2_INTEN;

    *dma_int_mux = value;
    debug_dmac("AFTER ROUTE: %x\n", *dma_int_mux);
}

/*-----------------------------------------------------------*/
/* DMA transfer preparation                                  */
/*
 * This routine programs the channel registers only. Interrupt
 * status/signal routing is handled separately by dma_irq_enable().
 */
/*-----------------------------------------------------------*/

/**
 * @brief Prepares one contiguous DMA transfer.
 *
 * @param[in] pxConfig Pointer to the DMA transfer configuration.
 *
 * @return 0 on success, or a negative value if the configuration is invalid.
 *
 * @note This function only programs the channel. The caller must invoke
 *       dma_start() to begin the transfer.
 */
int dma_prepare_transfer(const struct dma_transfer_config *config)
{
    volatile struct dmac_ch_reg *ch;
    uint64_t ctl;
    uint64_t cfg;
    size_t width_bytes;
    size_t transfer_count;

    dmac_assert_msg(config == NULL, -1, "CONFIG IS NULL\n");

    dmac_assert_msg(config->ch_idx >= DW_DMA_CH_NUM, -1, "CHANNEL IDX IS GREATER THAN DW_DMA_CH_NUM\n");

    dmac_assert_msg(config->dst == NULL || config->src == NULL, -1, "CONFIG DST OR SRC is NULL\n");

    dmac_assert(config->size == 0, -1);

    dmac_assert_msg(dma_width_to_bytes(config->width, &width_bytes) != 0, -1, "DMA WIDTH TO BYTES ERROR\n");

    dmac_assert_msg(config->size % width_bytes != 0, -1, "DMA onfig->size mod width_bytes ERROR\n");

    transfer_count = config->size / width_bytes;
    dmac_assert_msg(transfer_count > (size_t)(DMAC_BLOCK_TS_MASK + 1ULL), -1, "transfer_count is too great\n");

    switch (config->type) {
    case DMA_TRANSFER_MEM_TO_MEM:
        break;

    case DMA_TRANSFER_MEM_TO_DEV:
        dmac_assert(config->dst_per >= DW_DMA_CH_NUM, -1);
        dmac_assert(config->dst_req > DMA_CH_REMAP_FIELD_MASK, -1);

        break;

    case DMA_TRANSFER_DEV_TO_MEM:
        dmac_assert(config->dst_per >= DW_DMA_CH_NUM, -1);
        dmac_assert(config->dst_req > DMA_CH_REMAP_FIELD_MASK, -1);
        break;
    case DMA_TRANSFER_DEV_TO_DEV:
        /* Peripheral-to-peripheral routing is reserved for future use. */
        break;
    default:
        return -1;
    }

    ch = &dmac->ch[config->ch_idx];

    /* The channel must be disabled before its configuration is changed. */
    dma_channel_disable(config->ch_idx);

    /* Clear completion/error status left by a previous transfer. */
    ch->intclear =
        DMAC_CH_INTCLR_BLOCK_TFR_DONE |
        DMAC_CH_INTCLR_DMA_TFR_DONE |
        DMAC_CH_INTCLR_ERR_MASK;

    /*
     * Generate DMA_TFR_DONE status for polling/IRQ completion.
     * This does not route the status to the CPU interrupt output;
     * dma_irq_enable() controls that separately.
     */
    ch->intstatus_enable = DMAC_CH_INTEN_DMA_TFR_DONE;

    /* Bind a peripheral request line to this DMA channel. */

    int req = 0;

    switch (config->type)
    {
    case DMA_TRANSFER_MEM_TO_DEV:
        req = config->dst_req;
        break;
    case DMA_TRANSFER_DEV_TO_MEM:
        req = config->src_req;
        break;
    case DMA_TRANSFER_DEV_TO_DEV:
        break;
    default:
        break;
    };

    if (config->type != DMA_TRANSFER_MEM_TO_MEM)
    {
        
        debug_dmac("BEFORE DMA CH REMAP: 0x%x\n", 
            (config->ch_idx <= 3) ? *dma_ch_remap0 : *dma_ch_remap1);

        dmac_assert_msg(dma_map_request(config->ch_idx, req) != 0, -1, "DMA REQUEST ERROR\n");        

        debug_dmac("AFTER DMA CH REMAP: 0x%x\n", 
            (config->ch_idx <= 3) ? *dma_ch_remap0 : *dma_ch_remap1);
    }

    ch->sar = (uint64_t)config->src;
    ch->dar = (uint64_t)config->dst;

    /* BLOCK_TS stores transfer_count - 1. */
    ch->block_ts = DMAC_BLOCK_TS((uint32_t)(transfer_count - 1));

    ctl = 0;
    ctl |= DMAC_CTL_SRC_TR_WIDTH(config->width);
    ctl |= DMAC_CTL_DST_TR_WIDTH(config->width);
    ctl |= DMAC_CTL_SRC_MSIZE(config->burst);
    ctl |= DMAC_CTL_DST_MSIZE(config->burst);
    ctl |= DMAC_CTL_AR_PROT(0);
    ctl |= DMAC_CTL_AW_PROT(0);

    switch (config->type) {

    case DMA_TRANSFER_MEM_TO_MEM:

        /* SINC=0, DINC=0: increment both addresses. */

        ctl |= DMAC_CTL_DINC_INC;
        ctl |= DMAC_CTL_SINC_INC;

        break;

    case DMA_TRANSFER_MEM_TO_DEV:

        /* SINC=0: memory increments. DINC=1: peripheral fixed. */
        ctl |= DMAC_CTL_DINC_FIXED;
        ctl |= DMAC_CTL_SINC_INC;

        break;

    case DMA_TRANSFER_DEV_TO_MEM:

        /* SINC=1: peripheral fixed. DINC=0: memory increments. */

        ctl |= DMAC_CTL_SINC_FIXED;
        ctl |= DMAC_CTL_DINC_INC;

        break;

    case DMA_TRANSFER_DEV_TO_DEV:

        /* SINC=1: peripheral fixed. DINC=1: peripheral fixed. */

        ctl |= DMAC_CTL_SINC_FIXED;
        ctl |= DMAC_CTL_DINC_FIXED;

        break;

    default:
        return -1;
    }

    ch->ctl = ctl;

    cfg = 0;

    cfg |= DMAC_CFG_SRC_MULTBLK_TYPE(config->multblk_type);
    cfg |= DMAC_CFG_DST_MULTBLK_TYPE(config->multblk_type);

    switch (config->type) {
    case DMA_TRANSFER_MEM_TO_MEM:
        cfg |= DMAC_CFG_TT_FC(DMAC_TT_FC_MEM_TO_MEM_DMAC);
        break;

    case DMA_TRANSFER_MEM_TO_DEV:
        cfg |= DMAC_CFG_TT_FC(DMAC_TT_FC_MEM_TO_PER_DMAC);
        cfg &= ~DMAC_CFG_HS_SEL_DST;
        cfg |= DMAC_CFG_DST_PER_SEL(config->dst_per);

        break;
    case DMA_TRANSFER_DEV_TO_MEM:
        cfg |= DMAC_CFG_TT_FC(DMAC_TT_FC_PER_TO_MEM_DMAC);
        cfg &= ~DMAC_CFG_HS_SEL_SRC;
        cfg |= DMAC_CFG_SRC_PER_SEL(config->src_per);

        break;
    case DMA_TRANSFER_DEV_TO_DEV:
        break;
    default:
        return -1;
    }

    ch->cfg = cfg;

    return 0;
}

/**
 * @brief Prepares one linked-list DMA transfer.
 *
 * @param[in] pxConfig Pointer to the DMA transfer configuration.
 *                     pxConfig->llp must point to the first LLI.
 *
 * @return 0 on success, or a negative value if the configuration is invalid.
 *
 * @note In linked-list mode the channel fetches transfer information
 *       from the LLI memory rather than relying on the basic channel
 *       source, destination and block-size registers.
 */
int dma_prepare_list_transfer(struct dma_transfer_config *config)
{
    volatile struct dmac_ch_reg *ch;
    uint64_t cfg;
    void *llp = config->llp;
    unsigned char ch_idx = config->ch_idx;
    unsigned char src_per = config->src_per;
    unsigned char src_req = config->src_req;
    unsigned char dst_per = config->dst_per;
    unsigned char dst_req = config->dst_req;
    enum dma_transfer_type type = config->type;

    dmac_assert_msg(llp == NULL, -1, "LLP IS NULL\n");

    dmac_assert_msg(ch_idx >= DW_DMA_CH_NUM, -1, "CHANNEL IDX IS GREATER THAN DW_DMA_CH_NUM\n");

    switch (type) {
    case DMA_TRANSFER_MEM_TO_MEM:
        break;

    case DMA_TRANSFER_MEM_TO_DEV:
        dmac_assert(dst_per >= DW_DMA_CH_NUM, -1);
        dmac_assert(dst_req > DMA_CH_REMAP_FIELD_MASK, -1);

        break;

    case DMA_TRANSFER_DEV_TO_MEM:
        dmac_assert(dst_per >= DW_DMA_CH_NUM, -1);
        dmac_assert(dst_req > DMA_CH_REMAP_FIELD_MASK, -1);
        break;
    case DMA_TRANSFER_DEV_TO_DEV:

        break;
    default:
        return -1;
    }

    ch = &dmac->ch[ch_idx];

    /* The channel must be disabled before its configuration is changed. */
    dma_channel_disable(ch_idx);

    /* Clear completion/error status left by a previous transfer. */
    ch->intclear =
        DMAC_CH_INTCLR_BLOCK_TFR_DONE |
        DMAC_CH_INTCLR_DMA_TFR_DONE |
        DMAC_CH_INTCLR_ERR_MASK;

    /*
     * Generate DMA_TFR_DONE status for polling/IRQ completion.
     * This does not route the status to the CPU interrupt output;
     * dma_irq_enable() controls that separately.
     */
    ch->intstatus_enable = DMAC_CH_INTEN_DMA_TFR_DONE;

    int req = 0;

    switch (type)
    {
    case DMA_TRANSFER_MEM_TO_DEV:
        req = dst_req;
        break;
    case DMA_TRANSFER_DEV_TO_MEM:
        req = src_req;
        break;
    case DMA_TRANSFER_DEV_TO_DEV:
        break;
    default:
        break;
    };

    if (type != DMA_TRANSFER_MEM_TO_MEM)
    {
        
        debug_dmac("BEFORE DMA CH REMAP: 0x%x\n", 
            (ch_idx <= 3) ? *dma_ch_remap0 : *dma_ch_remap1);

        dmac_assert_msg(dma_map_request(ch_idx, req) != 0, -1, "DMA REQUEST ERROR\n");        

        debug_dmac("AFTER DMA CH REMAP: 0x%x\n", 
            (ch_idx <= 3) ? *dma_ch_remap0 : *dma_ch_remap1);
    }

    cfg = 0;

    cfg |= DMAC_CFG_SRC_MULTBLK_TYPE(LINK_LIST);
    cfg |= DMAC_CFG_DST_MULTBLK_TYPE(LINK_LIST);

    switch (type) {

    case DMA_TRANSFER_MEM_TO_MEM:
        cfg |= DMAC_CFG_TT_FC(DMAC_TT_FC_MEM_TO_MEM_DMAC);
        break;

    case DMA_TRANSFER_MEM_TO_DEV:
        cfg |= DMAC_CFG_TT_FC(DMAC_TT_FC_MEM_TO_PER_DMAC);

        /*
         * Destination uses hardware handshake.
         *
         * HS_SEL_DST = 0
         */
        cfg &= ~DMAC_CFG_HS_SEL_DST;

        /*
         * Select hardware handshake interface.
         */
        cfg |= DMAC_CFG_DST_PER_SEL(dst_per);

        break;
    case DMA_TRANSFER_DEV_TO_MEM:
        cfg |= DMAC_CFG_TT_FC(DMAC_TT_FC_PER_TO_MEM_DMAC);

        /*
         * Source uses hardware handshake.
         *
         * HS_SEL_SRC = 0
         */
        cfg &= ~DMAC_CFG_HS_SEL_SRC;

        /*
         * Select source peripheral request interface.
         */
        cfg |= DMAC_CFG_SRC_PER_SEL(src_per);

        break;
    default:
        return -1;
    }

    ch->cfg = cfg;

    ch->llp = (uint64_t)llp;
    ch->ctl = DMAC_CTL_SHADOWREG_OR_LLI_VALID;

    /*
     * DIAGNOSTIC: the previous "CTL: %016x" print only reads the low
     * 32 bits from a 64-bit va_arg (missing the ll length modifier),
     * so it can't actually tell us whether bit 63 landed. Read back
     * with the correct width immediately, with nothing else touching
     * the register in between.
     */
    debug_dmac("CTL immediately after write = 0x%016llx\n",
           (unsigned long long)ch->ctl);

    /*
     * DIAGNOSTIC: is this register writable *at all* right now, or is
     * something (channel state, a lock, whatever) rejecting every
     * write to it? Try a fully recognizable pattern and read it back
     * with no other register touched in between.
     */
    ch->ctl = 0x5A5A5A5A5A5A5A5AULL;
    debug_dmac("CTL after 0x5A...5A test write = 0x%016llx\n",
           (unsigned long long)ch->ctl);

    /* Restore the real value we actually want latched. */
    ch->ctl = DMAC_CTL_SHADOWREG_OR_LLI_VALID;
    debug_dmac("CTL after restoring real value = 0x%016llx\n",
           (unsigned long long)ch->ctl);

    return 0;
}

/**
 * @brief Prepares a DMA transfer according to the selected multi-block mode.
 *
 * @param[in] pxConfig Pointer to the DMA transfer configuration.
 *
 * @return 0 on success, or a negative value if the selected mode is
 *         unsupported or the configuration is invalid.
 *
 * @note CONTIGUOUS transfers use the channel registers, while LINK_LIST
 *       transfers use the first LLI supplied through pxConfig->llp.
 */
int dma_prepare(struct dma_transfer_config *config)
{
    switch (config->multblk_type)
    {
    case CONTIGUOUS:
        return dma_prepare_transfer(config);
        break;

    case RELOAD:
        break;

    case SHADOW_REGISTER:
        break;

    case LINK_LIST:
        return dma_prepare_list_transfer(config);
        break;

    default:
        break;
    }

    return -1;
}

/* ============================================================
 * DMA start
 * ============================================================
 */

/**
 * @brief Starts a previously prepared DMA channel.
 *
 * @param[in] uxChannel DMA channel number.
 *
 * @return 0 on success, or -1 if the channel number is invalid.
 *
 * @note The current implementation reads back the channel configuration
 *       immediately before enabling the channel when debug logging is enabled.
 */
int dma_start(unsigned int ch_idx)
{
    volatile struct dmac_ch_reg *ch;

    if (ch_idx >= DW_DMA_CH_NUM)
        return -1;

    ch = &dmac->ch[ch_idx];

    debug_dmac("BEFORE START\n");

    debug_dmac("CHEN        = 0x%016llx\n",
           (unsigned long long)dmac->chen);

    debug_dmac("CH INTSTATUS   = 0x%08x\n",
           ch->intstatus);

    debug_dmac("GLOBAL      = 0x%08x\n",
           dmac->intstatus);

    /*
     * Dump the raw CTL/CFG that are about to be latched by the
     * channel enable below.  This is the only reliable way to see
     * whether TT_FC / HS_SEL_DST / DST_PER actually made it into
     * hardware for a MEM->DEV transfer.
     */
    debug_dmac("CH%u CFG (raw) = 0x%016llx\n",
           ch_idx,
           (unsigned long long)ch->cfg);

    debug_dmac("CH%u CTL (raw) = 0x%016llx\n",
           ch_idx,
           (unsigned long long)ch->ctl);

    // debug_dmac("start flush cache\n");
    // flush_dcache_range(dmac, sizeof(*dmac));

    dma_channel_enable(ch_idx);

    /*
     * Immediately sample status.
     */
    // for (volatile int i = 0; i < 100000; ++i)
    //     ;

    // debug_dmac("AFTER DELAY\n");

    // debug_dmac("CHEN        = 0x%016llx\n",
    //        (unsigned long long)dmac->chen);

    // debug_dmac("CH INTSTATUS   = 0x%08x\n",
    //        dma_int_res.ch_status[ch_idx]);

    // debug_dmac("GLOBAL      = 0x%08x\n",
    //        dma_int_res.global_status);

    return 0;
}

/*-----------------------------------------------------------*/
/* DMA polling completion                                     */
/*-----------------------------------------------------------*/

/**
 * @brief Waits synchronously for a DMA transfer to complete.
 *
 * @param[in] uxChannel DMA channel number.
 *
 * @return 0 when the transfer completes, or a negative value on error
 *         or timeout.
 *
 * @note This function uses the channel interrupt-status register and
 *       does not depend on CPU interrupt delivery.
 */
int dma_wait(unsigned int ch_idx)
{
    uint32_t status;
    uint32_t timeout = 1000000;

    volatile struct dmac_ch_reg *ch;
    if (ch_idx >= DW_DMA_CH_NUM)
        return -1;

    ch = &dmac->ch[ch_idx];

    while (1) {
        
        status = ch->intstatus;
        if (status & 
            (DMAC_CH_INTSTAT_BLOCK_TFR_DONE |
             DMAC_CH_INTSTAT_DMA_TFR_DONE))
        {
            return 0;
        }

        // if (--timeout == 0) {
        //     debug_dmac("DMA WAIT TIMEOUT\n");
        //     debug_dmac("  CHEN   = 0x%016llx\n",
        //            (unsigned long long)dmac->chen);
        //     debug_dmac("  STATUS = 0x%08x\n",
        //            dmac->ch[ch_idx].intstatus);
        //     debug_dmac("  CFG    = 0x%016llx\n",
        //            (unsigned long long)dmac->ch[ch_idx].cfg);
        //     debug_dmac("  CTL    = 0x%016llx\n",
        //            (unsigned long long)dmac->ch[ch_idx].ctl);
        //     return -2;
        // }
    }
    return -2;
}

/*-----------------------------------------------------------*/
/* DMA stop                                                    */
/*-----------------------------------------------------------*/

/**
 * @brief Stops a DMA channel and clears its completion status.
 *
 * @param[in] uxChannel DMA channel number.
 *
 * @return 0 on success, or -1 if the channel number is invalid.
 */
int dma_stop(unsigned int ch_idx)
{
    volatile struct dmac_ch_reg *ch;

    if (ch_idx >= DW_DMA_CH_NUM)
        return -1;

    ch = &dmac->ch[ch_idx];

    /*
     * Clear completion status.
     */
    ch->intclear =
        DMAC_CH_INTCLR_BLOCK_TFR_DONE |
        DMAC_CH_INTCLR_DMA_TFR_DONE;

    /*
     * Disable channel.
     */
    dma_channel_disable(ch_idx);

    return 0;
}


/*-----------------------------------------------------------*/
/* DMA debug helpers                                           */
/*-----------------------------------------------------------*/

/**
 * @brief Prints the current configuration of a DMA channel.
 *
 * @param[in] uxChannel DMA channel number.
 *
 * @note Intended for debug builds only.
 */
static void dma_dump_config(unsigned int ch_idx)
{
    volatile struct dmac_ch_reg *ch;

    if (ch_idx >= DW_DMA_CH_NUM)
        return;

    ch = &dmac->ch[ch_idx];

    debug_dmac("DMA CH%u configuration:\n", ch_idx);

    debug_dmac("  SAR       = 0x%016llx\n",
           (unsigned long long)ch->sar);

    debug_dmac("  DAR       = 0x%016llx\n",
           (unsigned long long)ch->dar);

    debug_dmac("  BLOCK_TS  = 0x%08x\n",
           ch->block_ts);

    debug_dmac("  CTL       = 0x%016llx\n",
           (unsigned long long)ch->ctl);

    debug_dmac("  CFG       = 0x%016llx\n",
           (unsigned long long)ch->cfg);

    debug_dmac("  INT_EN    = 0x%08x\n",
           ch->intstatus_enable);
}

/**
 * @brief Removes a DMA channel from the CPU2 DMA interrupt route.
 *
 * @param[in] uxChannel DMA channel number.
 *
 * @note The shared CPU2 DMA interrupt remains enabled because another
 *       channel may still be using the same interrupt line.
 */
static void dma_irq_route_disable(unsigned int ch_idx)
{
    uint32_t value;

    value = *dma_int_mux;

    value &= ~DMA_INT_MUX_CPU2_CH_BIT(ch_idx);

    /*
     * Keep CPU2 INTEN enabled.
     *
     * Another DMA channel may still use IRQ25.
     */
    *dma_int_mux = value;
}


/**
 * @brief Prints global DMA interrupt status and all channel statuses.
 */
void debug_dmac_global_status(void)
{
    debug_dmac("PRINT GLOBAL STATUS = 0x%lx\n",
           (unsigned long)dmac->intstatus);

    volatile struct dmac_ch_reg *ch;
    uint32_t status;
    for (unsigned int ch_idx = 0;
         ch_idx < DW_DMA_CH_NUM;
         ++ch_idx)
    {
        ch = &dmac->ch[ch_idx];

        status = ch->intstatus;

        debug_dmac("CH%u ||STATUS|| = 0x%lx\n",
               ch_idx,
               (unsigned long)status);
    }
}

/**
 * @brief DMA interrupt service routine.
 *
 * @param[in] iIrq The interrupt number supplied by the interrupt controller.
 * @param[in] pvPrivateData Private data supplied when the interrupt is registered.
 *
 * @return 0.
 *
 * @note The ISR only snapshots and clears DMA status and updates software
 *       completion state. It must not block or perform lengthy processing.
 */
static int prvDmaISR(int irqn, void *priv)
{
    unsigned int ch_idx;

    (void)irqn;
    (void)priv;

    /*
     * Do NOT wait for DMAC_INTSTATUS here.
     *
     * The channel interrupt status is the authoritative source for
     * DMA_TFR_DONE / BLOCK_TFR_DONE.  If the common/global status path
     * is masked or routed differently, spinning in the ISR would hide
     * the real channel status.
     */
    // uint32_t ch_status;
    for (ch_idx = 0; ch_idx < DW_DMA_CH_NUM; ++ch_idx)
    {
        dma_int_res.ch_status[ch_idx] = dmac->ch[ch_idx].intstatus;
    }

    // uint32_t global_status;
    // uint32_t global_status_2;
    dma_int_res.global_status = dmac->intstatus;

    // global_status = dma_int_res.global_status;
    // global_status_2 = dma_int_res.global_status;
    // for (int ch_idx = 0; ch_idx < DW_DMA_CH_NUM; ++ch_idx)
    //     debug_dmac("ch%d: 0x%x\n", ch_idx, dma_int_res.ch_status[ch_idx]);

    // debug_dmac("Entering prvDmaISR, IRQ=%d CH STATUS=%x GLOBAL=0x%08x\\n",
    //        irqn, dma_int_res.ch_status[2], global_status);

    // debug_dmac("Check GLOBAL AGAIN: %x\n", global_status_2);
    // debug_dmac("Check GLOBAL AGAIN (PRINT WITH DIRECTLY): %x\n", dmac->intstatus);

    /*
     * Read every channel's CHx_INTSTATUS.
     *
     * We intentionally do not gate this loop on global_status.
     */
    for (ch_idx = 0; ch_idx < DW_DMA_CH_NUM; ++ch_idx)
    {
        volatile struct dmac_ch_reg *ch = &dmac->ch[ch_idx];
        uint32_t status = dma_int_res.ch_status[ch_idx];

        if (status == 0)
            continue;

        // debug_dmac("DMA ISR: CH%d INTSTATUS=0x%08x\\n",
        //        ch_idx, status);

        dma_irq_status[ch_idx] = status;

        /*
         * Clear exactly what hardware reported.
         */
        ch->intclear = status;

        if (status & DMAC_CH_INTSTAT_DMA_TFR_DONE)
        {
            dma_irq_result[ch_idx] = 0;
            dma_irq_done[ch_idx] = 1;
        }
        else if (status & DMAC_CH_INTSTAT_BLOCK_TFR_DONE)
        {
            /*
             * Also treat BLOCK_TFR_DONE as completion for robustness.
             */
            dma_irq_result[ch_idx] = 0;
            dma_irq_done[ch_idx] = 1;
        }

        if (status & DMAC_CH_INTSTAT_ERR_MASK)
        {
            dma_irq_result[ch_idx] = -1;
            dma_irq_done[ch_idx] = 1;
        }
    }

    /*
     * Return from the ISR without spinning or touching unrelated
     * common interrupt registers.
     */
    return 0;
}

/*-----------------------------------------------------------*/
/* DMA interrupt initialisation                                */
/*
 * Enables the DMAC global interrupt and registers the system
 * DMA interrupt handler. Polling mode remains available because
 * dma_init() does not enable the global interrupt bit.
 */
/*-----------------------------------------------------------*/

/**
 * @brief Initialises the DMA interrupt path.
 *
 * Registers the system DMA interrupt handler once and enables the
 * DMAC global interrupt generation.
 *
 * @return 0 on success, or -1 if the interrupt handler cannot be registered.
 */
int dma_irq_init(void)
{
    int ret;

    /*
     * Make sure all common interrupt signals are disabled.
     */
    dmac->common_intsignal_enable = 0;

    /*
     * Make sure all common interrupt status generation is disabled.
     */
    dmac->common_intstatus_enable = 0;

    /*
     * Clear any stale common interrupt status.
     */
    dmac->common_intclear = 0;

    /*
     * Enable DMAC global interrupt generation.
     */
    dmac->cfg |= DMAC_CFG_INT_EN;

    /*
     * Register IRQ25 only once.
     *
     * CPU2 System DMA IRQ = 25.
     */
    if (!dma_irq_registered)
    {
        ret = request_irq(
            SDMA_INTR_CPU2,
            prvDmaISR,
            0,
            "sdma",
            NULL
        );

        if (ret != 0)
        {
            dmac->cfg &= ~DMAC_CFG_INT_EN;
            return -1;
        }

        dma_irq_registered = 1;
    }

    return 0;
}


/*-----------------------------------------------------------*/
/* Enable DMA interrupt for one channel                         */
/*-----------------------------------------------------------*/

/**
 * @brief Enables interrupt reporting for one DMA channel.
 *
 * @param[in] uxChannel DMA channel number.
 *
 * @return 0 on success, or -1 if the channel number is invalid.
 *
 * @note Clears stale status, enables transfer-done generation and signal
 *       propagation, and routes the channel interrupt to CPU2.
 */
int dma_irq_enable(unsigned int ch_idx)
{
    volatile struct dmac_ch_reg *ch;

    if (ch_idx >= DW_DMA_CH_NUM)
        return -1;

    ch = &dmac->ch[ch_idx];

    dma_irq_done[ch_idx] = 0;
    dma_irq_result[ch_idx] = -2;
    dma_irq_status[ch_idx] = 0;

    /*
     * Clear stale completion/error status BEFORE enabling the interrupt
     * sources.  This avoids carrying an old status into a new transfer.
     */
    ch->intclear =
        DMAC_CH_INTCLR_BLOCK_TFR_DONE |
        DMAC_CH_INTCLR_DMA_TFR_DONE |
        DMAC_CH_INTCLR_ERR_MASK;

    /*
     * INTSTATUS_ENABLE controls generation of status bits in CHx_INTSTATUS.
     */
    ch->intstatus_enable =
        DMAC_CH_INTEN_DMA_TFR_DONE;

    /*
     * INTSIGNAL_ENABLE controls propagation to the DMA interrupt output.
     */
    ch->intsignal_enable =
        DMAC_CH_INTSIG_DMA_TFR_DONE;

    /*
     * Route this channel to CPU2.
     */
    dma_irq_route_enable(ch_idx);

    debug_dmac("DMA IRQ CONFIG CH%u: INT_EN=0x%08x SIG_EN=0x%08x CFG=0x%08x MUX=0x%08x\n",
           ch_idx,
           ch->intstatus_enable,
           ch->intsignal_enable,
           dmac->cfg,
           *dma_int_mux);

    return 0;
}

/*-----------------------------------------------------------*/
/* Disable DMA interrupt for one channel                        */
/*-----------------------------------------------------------*/

/**
 * @brief Disables interrupt reporting for one DMA channel.
 *
 * @param[in] uxChannel DMA channel number.
 *
 * @return 0 on success, or -1 if the channel number is invalid.
 */
int dma_irq_disable(unsigned int ch_idx)
{
    volatile struct dmac_ch_reg *ch;

    if (ch_idx >= DW_DMA_CH_NUM)
        return -1;

    ch = &dmac->ch[ch_idx];

    /*
     * Disable outward interrupt signal first.
     */
    ch->intsignal_enable = 0;

    /*
     * Clear pending interrupt status.
     */
    ch->intclear =
        DMAC_CH_INTCLR_DMA_TFR_DONE |
        DMAC_CH_INTCLR_ERR_MASK;

    /*
     * Disable interrupt status generation.
     *
     * This is important because otherwise the per-channel
     * interrupt source remains enabled even though the
     * output signal has been disabled.
     */
    ch->intstatus_enable = 0;

    /*
     * Remove this channel from CPU2 routing.
     */
    dma_irq_route_disable(ch_idx);

    /*
     * Reset software state.
     */
    dma_irq_done[ch_idx] = 0;
    dma_irq_result[ch_idx] = -2;
    dma_irq_status[ch_idx] = 0;
    
    dma_int_res.global_status = 0;
    for (int i = 0; i < DW_DMA_CH_NUM; ++i)
        dma_int_res.ch_status[i] = 0;

    return 0;
}

/**
 * @brief Waits for completion of an interrupt-driven DMA transfer.
 *
 * @param[in] uxChannel DMA channel number.
 *
 * @return The DMA result recorded by the ISR.
 *
 * @note The ISR normally updates the per-channel completion state. The
 *       additional status check is retained to aid diagnosis if hardware
 *       status becomes visible before the expected software state is observed.
 */
int dma_wait_irq(unsigned int ch_idx)
{
    volatile struct dmac_ch_reg *ch;
    volatile uint32_t timeout = 1000000;

    if (ch_idx >= DW_DMA_CH_NUM)
        return -1;

    while (!dma_irq_done[ch_idx])
    {
        /*
         * Diagnostic fallback:
         * if CHx_INTSTATUS is set but the ISR did not update the
         * software flag yet, capture it here.  This also helps distinguish
         * "DMA completed but IRQ routing failed" from "DMA never completed".
         */

        uint32_t status = dma_int_res.ch_status[ch_idx];
        if (dma_irq_done[ch_idx] && status != 0)
        {
            debug_dmac("DMA WAIT: CH%u INTSTATUS=0x%08x GLOBAL=0x%08x\\n",
                   ch_idx, status, dma_int_res.global_status);

            ch = &dmac->ch[ch_idx];
            dma_irq_status[ch_idx] = status;
            ch->intclear = status;

            if (status & (DMAC_CH_INTSTAT_DMA_TFR_DONE |
                          DMAC_CH_INTSTAT_BLOCK_TFR_DONE))
            {
                dma_irq_result[ch_idx] = 0;
                dma_irq_done[ch_idx] = 1;
                break;
            }

            if (status & DMAC_CH_INTSTAT_ERR_MASK)
            {
                dma_irq_result[ch_idx] = -1;
                dma_irq_done[ch_idx] = 1;
                break;
            }
        }
    
        // if (--timeout == 0)
        // {
        //     /*
        //      * IMPORTANT: ch is only assigned inside the
        //      * "dma_irq_done[ch_idx] && status != 0" branch above.
        //      * If we time out without ever entering that branch,
        //      * ch is an uninitialized pointer -- dereferencing it
        //      * below would read garbage / fault. Assign it here
        //      * before using it.
        //      */
        //     ch = &dmac->ch[ch_idx];

        //     debug_dmac("\nDMA IRQ TIMEOUT\n");
        //     debug_dmac("  CH%u INTSTATUS        = 0x%08x\n",
        //            ch_idx, ch->intstatus);
        //     debug_dmac("  CH%u INTSTATUS_ENABLE = 0x%08x\n",
        //            ch_idx, ch->intstatus_enable);
        //     debug_dmac("  CH%u INTSIGNAL_ENABLE = 0x%08x\n",
        //            ch_idx, ch->intsignal_enable);

        //     debug_dmac("  DMAC INTSTATUS       = 0x%08x\n",
        //            dmac->intstatus);

        //     debug_dmac("  DMAC CFG             = 0x%08x\n",
        //            dmac->cfg);

        //     debug_dmac("  DMA INT MUX          = 0x%08x\n",
        //            *dma_int_mux);

        //     debug_dmac("  CHEN                 = 0x%016llx\n",
        //            (unsigned long long)dmac->chen);

        //     debug_dmac("  CH%u CFG (raw)        = 0x%016llx\n",
        //            ch_idx, (unsigned long long)ch->cfg);

        //     debug_dmac("  CH%u CTL (raw)        = 0x%016llx\n",
        //            ch_idx, (unsigned long long)ch->ctl);

        //     return -2;
        // }
    }

    return dma_irq_result[ch_idx];
}