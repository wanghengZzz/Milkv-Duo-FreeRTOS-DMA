#ifndef __HAL_DW_LIST_H_
#define __HAL_DW_LIST_H_

#include <stddef.h>

#if defined(__GNUC__) || defined(__clang__) ||         \
    (defined(__STDC__) && defined(__STDC_VERSION__) && \
     (__STDC_VERSION__ >= 202311L)) /* C23 ?*/
#define __LIST_HAVE_TYPEOF 1
#else
#define __LIST_HAVE_TYPEOF 0
#endif

#ifndef container_of
#if __LIST_HAVE_TYPEOF
#define container_of(ptr, type, member) \
  __extension__({ \
    const typeof(((type *)0)->member) *__member = (ptr); \
    (type *)((char *)__member - offsetof(type, member)); \
  })

#endif
#else
#define container_of(ptr, type, member) \
    ((type *)((char *)member) - offsetof(type, member))
#endif

#define list_entry(node, type, member) container_of(node, type, member)

typedef struct list_head list_head;


struct list_head {
    list_head *next;
    list_head *prev;
};

#define LIST_HEAD(head) list_head head = { &(head), &(head) }

static inline void INIT_LIST_HEAD(struct list_head *head) {
  head->next = head;
  head->prev = head;
}

static inline void list_add(list_head *head, list_head *node)
{
    node->next = head->next;
    head->next->prev = node;
    head->next = node;
    node->prev = head;
}

static inline void list_add_tail(list_head *head, list_head *node)
{
    node->next = head;
    node->prev = head->prev;
    head->prev->next = node;
    head->prev = node;
}

static inline void list_del(list_head *node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
}

static inline void list_move_tail(list_head *head, list_head *node)
{
    list_del(node);
    list_add_tail(head, node);
}

static inline void list_move(list_head *head, list_head *node)
{
    list_del(node);
    list_add(head, node);
}

static inline int list_empty(list_head *head)
{
    return head->next == head;
}

static inline int list_singular(list_head *head)
{
    return head->next != head && head->next->next == head;
}

static inline void list_cut_tail_position(list_head *head_to,
                                          list_head *head_from,
                                          list_head *node)
{
    if (list_empty(head_from))
        return;
    
    if (head_from == node) {
        INIT_LIST_HEAD(head_to);
        return;
    }

    head_to->prev = head_from->prev;
    head_from->prev->next = head_to;

    head_from->prev = node->prev;
    node->prev->next = head_from;
    head_to->next = node;
    node->prev = head_to;

}

static inline void list_cut_position(list_head *head_to,
                                     list_head *head_from,
                                     list_head *node)
{

    if (list_empty(head_from))
        return;
    
    if (head_from == node) {
        INIT_LIST_HEAD(head_to);
        return;
    }

    head_to->next = head_from->next;
    head_from->next->prev = head_to;

    node->next->prev = head_from;
    head_from->next = node->next;
    node->next = head_to;
    head_to->prev = node;
}

static inline void list_splice(struct list_head *list, struct list_head *head)
{
    if (list_empty(list))
        return;

    list->prev->next = head->next;
    head->next->prev = list->prev;
    head->next = list->next;
    list->next->prev = head;
}

static inline void list_splice_tail(struct list_head *list, struct list_head *head)
{
    if (list_empty(list))
        return;

    list->next->prev = head->prev;
    head->prev->next = list->next;
    head->prev = list->prev;
    list->prev->next = head;
}

#endif



