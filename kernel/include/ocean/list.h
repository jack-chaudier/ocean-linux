/*
 * Ocean Kernel - Doubly Linked List Implementation
 *
 * Linux-style circular doubly linked list macros.
 */

#ifndef _OCEAN_LIST_H
#define _OCEAN_LIST_H

#include <ocean/types.h>
#include <ocean/defs.h>

/*
 * List head structure
 *
 * Both list_head and list entries use this structure.
 * An empty list has head->next == head->prev == head.
 */
struct list_head {
    struct list_head *next;
    struct list_head *prev;
};

/*
 * Static initializer for list head
 */
#define LIST_HEAD_INIT(name) { &(name), &(name) }

/*
 * Declare and initialize a list head
 */
#define LIST_HEAD(name) \
    struct list_head name = LIST_HEAD_INIT(name)

/*
 * Initialize a list head at runtime
 */
static __always_inline void INIT_LIST_HEAD(struct list_head *list)
{
    list->next = list;
    list->prev = list;
}

/*
 * Internal: Insert a new entry between two known consecutive entries.
 */
static __always_inline void __list_add(struct list_head *new,
                                       struct list_head *prev,
                                       struct list_head *next)
{
    next->prev = new;
    new->next = next;
    new->prev = prev;
    prev->next = new;
}

/*
 * list_add - Add a new entry after the specified head
 * @new: new entry to be added
 * @head: list head to add it after
 *
 * Insert a new entry after the specified head.
 * This is good for implementing stacks.
 */
static __always_inline void list_add(struct list_head *new,
                                     struct list_head *head)
{
    __list_add(new, head, head->next);
}

/*
 * list_add_tail - Add a new entry before the specified head
 * @new: new entry to be added
 * @head: list head to add it before
 *
 * Insert a new entry before the specified head.
 * This is useful for implementing queues.
 */
static __always_inline void list_add_tail(struct list_head *new,
                                          struct list_head *head)
{
    __list_add(new, head->prev, head);
}

/*
 * Internal: Delete a list entry by making the prev/next entries
 * point to each other.
 */
static __always_inline void __list_del(struct list_head *prev,
                                       struct list_head *next)
{
    next->prev = prev;
    prev->next = next;
}

/*
 * list_del - Deletes entry from list
 * @entry: the element to delete from the list
 *
 * Note: list_empty() on entry does not return true after this,
 * the entry is in an undefined state.
 */
static __always_inline void list_del(struct list_head *entry)
{
    __list_del(entry->prev, entry->next);
    entry->next = NULL;
    entry->prev = NULL;
}

/*
 * list_del_init - Deletes entry from list and reinitialize it
 * @entry: the element to delete from the list
 */
static __always_inline void list_del_init(struct list_head *entry)
{
    __list_del(entry->prev, entry->next);
    INIT_LIST_HEAD(entry);
}

/*
 * list_replace - Replace old entry by new one
 * @old: the element to be replaced
 * @new: the new element to insert
 */
static __always_inline void list_replace(struct list_head *old,
                                         struct list_head *new)
{
    new->next = old->next;
    new->next->prev = new;
    new->prev = old->prev;
    new->prev->next = new;
}

/*
 * list_replace_init - Replace old entry by new one and initialize the old one
 */
static __always_inline void list_replace_init(struct list_head *old,
                                              struct list_head *new)
{
    list_replace(old, new);
    INIT_LIST_HEAD(old);
}

/*
 * list_move - Delete from one list and add as another's head
 * @list: the entry to move
 * @head: the head that will precede our entry
 */
static __always_inline void list_move(struct list_head *list,
                                      struct list_head *head)
{
    __list_del(list->prev, list->next);
    list_add(list, head);
}

/*
 * list_move_tail - Delete from one list and add as another's tail
 * @list: the entry to move
 * @head: the head that will follow our entry
 */
static __always_inline void list_move_tail(struct list_head *list,
                                           struct list_head *head)
{
    __list_del(list->prev, list->next);
    list_add_tail(list, head);
}

/*
 * list_empty - Tests whether a list is empty
 * @head: the list to test
 */
static __always_inline bool list_empty(const struct list_head *head)
{
    return head->next == head;
}

/*
 * list_is_singular - Tests whether a list has just one entry
 * @head: the list to test
 */
static __always_inline bool list_is_singular(const struct list_head *head)
{
    return !list_empty(head) && (head->next == head->prev);
}

/*
 * list_entry - Get the struct containing this list entry
 * @ptr: the &struct list_head pointer
 * @type: the type of the struct this is embedded in
 * @member: the name of the list_head within the struct
 */
#define list_entry(ptr, type, member) \
    container_of(ptr, type, member)

/*
 * list_first_entry - Get the first element from a list
 * @ptr: the list head to take the element from
 * @type: the type of the struct this is embedded in
 * @member: the name of the list_head within the struct
 *
 * Note: list must not be empty.
 */
#define list_first_entry(ptr, type, member) \
    list_entry((ptr)->next, type, member)

/*
 * list_last_entry - Get the last element from a list
 * @ptr: the list head to take the element from
 * @type: the type of the struct this is embedded in
 * @member: the name of the list_head within the struct
 *
 * Note: list must not be empty.
 */
#define list_last_entry(ptr, type, member) \
    list_entry((ptr)->prev, type, member)

/*
 * list_first_entry_or_null - Get the first element from a list or NULL
 * @ptr: the list head to take the element from
 * @type: the type of the struct this is embedded in
 * @member: the name of the list_head within the struct
 */
#define list_first_entry_or_null(ptr, type, member) ({ \
    struct list_head *__head = (ptr); \
    struct list_head *__pos = __head->next; \
    __pos != __head ? list_entry(__pos, type, member) : NULL; \
})

/*
 * list_next_entry - Get the next element in list
 * @pos: the type * to cursor
 * @member: the name of the list_head within the struct
 */
#define list_next_entry(pos, member) \
    list_entry((pos)->member.next, typeof(*(pos)), member)

/*
 * list_prev_entry - Get the previous element in list
 * @pos: the type * to cursor
 * @member: the name of the list_head within the struct
 */
#define list_prev_entry(pos, member) \
    list_entry((pos)->member.prev, typeof(*(pos)), member)

/*
 * list_for_each - Iterate over a list
 * @pos: the &struct list_head to use as a loop cursor
 * @head: the head for your list
 */
#define list_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)

/*
 * list_for_each_safe - Iterate over a list safe against removal
 * @pos: the &struct list_head to use as a loop cursor
 * @n: another &struct list_head to use as temporary storage
 * @head: the head for your list
 */
#define list_for_each_safe(pos, n, head) \
    for (pos = (head)->next, n = pos->next; pos != (head); \
         pos = n, n = pos->next)

/*
 * list_for_each_entry - Iterate over list of given type
 * @pos: the type * to use as a loop cursor
 * @head: the head for your list
 * @member: the name of the list_head within the struct
 */
#define list_for_each_entry(pos, head, member) \
    for (pos = list_first_entry(head, typeof(*pos), member); \
         &pos->member != (head); \
         pos = list_next_entry(pos, member))

/*
 * list_for_each_entry_safe - Iterate over list safe against removal
 * @pos: the type * to use as a loop cursor
 * @n: another type * to use as temporary storage
 * @head: the head for your list
 * @member: the name of the list_head within the struct
 */
#define list_for_each_entry_safe(pos, n, head, member) \
    for (pos = list_first_entry(head, typeof(*pos), member), \
         n = list_next_entry(pos, member); \
         &pos->member != (head); \
         pos = n, n = list_next_entry(n, member))

/*
 * list_for_each_entry_reverse - Iterate backwards over list of given type
 * @pos: the type * to use as a loop cursor
 * @head: the head for your list
 * @member: the name of the list_head within the struct
 */
#define list_for_each_entry_reverse(pos, head, member) \
    for (pos = list_last_entry(head, typeof(*pos), member); \
         &pos->member != (head); \
         pos = list_prev_entry(pos, member))

/*
 * Hash list (for hash tables with single pointer head)
 */
struct hlist_head {
    struct hlist_node *first;
};

struct hlist_node {
    struct hlist_node *next;
    struct hlist_node **pprev;
};

#define HLIST_HEAD_INIT { .first = NULL }
#define HLIST_HEAD(name) struct hlist_head name = HLIST_HEAD_INIT

static __always_inline void INIT_HLIST_HEAD(struct hlist_head *h)
{
    h->first = NULL;
}

static __always_inline void INIT_HLIST_NODE(struct hlist_node *h)
{
    h->next = NULL;
    h->pprev = NULL;
}

static __always_inline bool hlist_empty(const struct hlist_head *h)
{
    return !h->first;
}

static __always_inline void hlist_add_head(struct hlist_node *n,
                                           struct hlist_head *h)
{
    struct hlist_node *first = h->first;
    n->next = first;
    if (first) {
        first->pprev = &n->next;
    }
    h->first = n;
    n->pprev = &h->first;
}

static __always_inline void hlist_del(struct hlist_node *n)
{
    struct hlist_node *next = n->next;
    struct hlist_node **pprev = n->pprev;

    *pprev = next;
    if (next) {
        next->pprev = pprev;
    }
}

#define hlist_entry(ptr, type, member) container_of(ptr, type, member)

#define hlist_for_each(pos, head) \
    for (pos = (head)->first; pos; pos = pos->next)

#define hlist_for_each_safe(pos, n, head) \
    for (pos = (head)->first; pos && ({ n = pos->next; 1; }); pos = n)

#define hlist_for_each_entry(pos, head, member) \
    for (pos = hlist_entry((head)->first, typeof(*(pos)), member); \
         pos; \
         pos = pos->member.next ? \
               hlist_entry(pos->member.next, typeof(*(pos)), member) : NULL)

#endif /* _OCEAN_LIST_H */
