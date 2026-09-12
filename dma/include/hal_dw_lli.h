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

#ifndef __HAL_DW_LLI_H_
#define __HAL_DW_LLI_H_

#include "hal_dw_dma.h"
#include <stdlib.h>
#include <string.h>

/*-----------------------------------------------------------*/

/**
 * @brief Allocate and initialize a DMA linked-list descriptor.
 *
 * @param[in] sar Source address of the DMA transfer.
 * @param[in] dar Destination address of the DMA transfer.
 * @param[in] block_ts Number of data items to transfer for this descriptor.
 * @param[in] ctl Initial DMA control value.
 *
 * @return Pointer to the newly allocated descriptor, or NULL if allocation fails.
 */
static inline struct dw_desc *lli_new(void *sar,
                                      void *dar,
                                      uint32_t block_ts,
                                      uint64_t ctl)
{
    size_t lli_size = sizeof(struct dw_desc);

    lli_size = (lli_size + 63U) & ~(size_t)63U;

    struct dw_desc *new_desc =
        (struct dw_desc *)memalign(64U, lli_size);

    if (new_desc != NULL)
    {
        (void)memset(new_desc, 0, lli_size);
    }

    if (new_desc == NULL)
    {
        return NULL;
    }

    new_desc->lli.sar = (uint64_t)sar;
    new_desc->lli.dar = (uint64_t)dar;
    new_desc->lli.block_ts = block_ts;
    new_desc->lli.ctl = ctl | DMAC_CTL_SHADOWREG_OR_LLI_VALID;
    new_desc->lli.ctl &= ~DMAC_CTL_SHADOWREG_OR_LLI_LAST;

    return new_desc;
}

/*-----------------------------------------------------------*/

/**
 * @brief Allocate a DMA descriptor and add it to the head of a linked list.
 *
 * @param[in,out] head Linked-list head.
 * @param[in] sar Source address of the DMA transfer.
 * @param[in] dar Destination address of the DMA transfer.
 * @param[in] block_ts Number of data items to transfer for this descriptor.
 * @param[in] ctl Initial DMA control value.
 *
 * @return 0 when the descriptor is added successfully.
 * @return -1 when an input parameter is invalid.
 * @return -2 when descriptor allocation fails.
 */
static inline int lli_new_add(struct list_head *head,
                              void *sar,
                              void *dar,
                              uint32_t block_ts,
                              uint64_t ctl)
{
    if ((head == NULL) || (sar == NULL) || (dar == NULL))
    {
        return -1;
    }

    struct dw_desc *new_desc = lli_new(sar, dar, block_ts, ctl);

    if (new_desc == NULL)
    {
        return -2;
    }

    list_add(head, &new_desc->desc_node);

    if (new_desc->desc_node.next != head)
    {
        struct dw_desc *next_desc =
            list_entry(new_desc->desc_node.next, struct dw_desc, desc_node);

        new_desc->lli.llp = (uint64_t)next_desc;
    }
    else
    {
        new_desc->lli.ctl |= DMAC_CTL_SHADOWREG_OR_LLI_LAST;
    }

    return 0;
}

/*-----------------------------------------------------------*/

/**
 * @brief Allocate a DMA descriptor and add it to the tail of a linked list.
 *
 * @param[in,out] head Linked-list head.
 * @param[in] sar Source address of the DMA transfer.
 * @param[in] dar Destination address of the DMA transfer.
 * @param[in] block_ts Number of data items to transfer for this descriptor.
 * @param[in] ctl Initial DMA control value.
 *
 * @return 0 when the descriptor is added successfully.
 * @return -1 when an input parameter is invalid.
 * @return -2 when descriptor allocation fails.
 */
static inline int lli_new_add_tail(struct list_head *head,
                                   void *sar,
                                   void *dar,
                                   uint32_t block_ts,
                                   uint64_t ctl)
{
    if ((head == NULL) || (sar == NULL) || (dar == NULL))
    {
        return -1;
    }

    struct dw_desc *new_desc = lli_new(sar, dar, block_ts, ctl);

    if (new_desc == NULL)
    {
        return -2;
    }

    list_add_tail(head, &new_desc->desc_node);

    new_desc->lli.ctl |= DMAC_CTL_SHADOWREG_OR_LLI_LAST;

    if (new_desc->desc_node.prev != head)
    {
        struct dw_desc *prev_desc =
            list_entry(new_desc->desc_node.prev, struct dw_desc, desc_node);

        prev_desc->lli.llp = (uint64_t)new_desc;
        prev_desc->lli.ctl &= ~DMAC_CTL_SHADOWREG_OR_LLI_LAST;
    }

    return 0;
}

/*-----------------------------------------------------------*/

/**
 * @brief Add an existing DMA descriptor to the head of a linked list.
 *
 * @param[in,out] head Linked-list head.
 * @param[in,out] desc DMA descriptor to add to the list.
 *
 * @return None.
 */
static inline void lli_add(struct list_head *head,
                           struct dw_desc *desc)
{
    if ((head == NULL) || (desc == NULL))
    {
        return;
    }

    list_add(head, &desc->desc_node);

    if (desc->desc_node.next != head)
    {
        struct dw_desc *next_desc =
            list_entry(desc->desc_node.next, struct dw_desc, desc_node);

        desc->lli.llp = (uint64_t)next_desc;
    }
    else
    {
        desc->lli.ctl |= DMAC_CTL_SHADOWREG_OR_LLI_LAST;
    }

    if (desc->desc_node.prev != head)
    {
        struct dw_desc *prev_desc =
            list_entry(desc->desc_node.prev, struct dw_desc, desc_node);

        prev_desc->lli.llp = (uint64_t)desc;
    }
}

/*-----------------------------------------------------------*/

/**
 * @brief Add an existing DMA descriptor to the tail of a linked list.
 *
 * @param[in,out] head Linked-list head.
 * @param[in,out] desc DMA descriptor to add to the list.
 *
 * @return None.
 */
static inline void lli_add_tail(struct list_head *head,
                                struct dw_desc *desc)
{
    if ((head == NULL) || (desc == NULL))
    {
        return;
    }

    desc->lli.ctl |= DMAC_CTL_SHADOWREG_OR_LLI_LAST;

    list_add_tail(head, &desc->desc_node);

    if (desc->desc_node.prev != head)
    {
        struct dw_desc *prev_desc =
            list_entry(desc->desc_node.prev, struct dw_desc, desc_node);

        prev_desc->lli.llp = (uint64_t)desc;
        prev_desc->lli.ctl &= ~DMAC_CTL_SHADOWREG_OR_LLI_LAST;
    }
}

/*-----------------------------------------------------------*/

/**
 * @brief Remove a DMA descriptor from a linked list.
 *
 * @param[in,out] node List node belonging to the descriptor to remove.
 *
 * @return None.
 */
static inline void lli_del(struct list_head *node)
{
    if (node == NULL)
    {
        return;
    }

    list_del(node);

    struct dw_desc *desc = list_entry(node, struct dw_desc, desc_node);
    desc->lli.llp = 0;
}

/*-----------------------------------------------------------*/

#endif /* __HAL_DW_LLI_H_ */