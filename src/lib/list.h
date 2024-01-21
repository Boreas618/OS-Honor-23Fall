#pragma once

#include <lib/defines.h>
#include <lib/spinlock.h>

/* ListNode represents one node on a circular list. */
struct list_node {
    struct list_node *prev;
    struct list_node *next;
};

typedef struct list_node ListNode;

/* Initialize a sigle node circular list. */
void init_list_node(struct list_node *node);

/**
 * Merge the list containing `node1` and the list containing `node2` into one
 * list. It guarantees `node1->next == node2`. Both lists can be empty. This
 * function will return the merged list.
 *
 * It's a list operation without locks: USE IT CAREFULLY.
 */
struct list_node *_merge_list(struct list_node *node1, struct list_node *node2);

/**
 * Insert a single new node into the list
 *
 * It's a list operation without locks: USE IT CAREFULLY.
 */
#define _insert_into_list(list, node)                                          \
    (init_list_node(node), _merge_list(list, node))

/**
 * Remove `node` from the list, and then `node` becomes a single
 * node list. It usually returns `node->prev`. If `node` is
 * the last one in the list, it will return NULL.
 *
 * It's a list operation without locks: USE IT CAREFULLY.
 */
struct list_node *_detach_from_list(ListNode *node);

/**
 * Iterate the list.
 *
 * It's a list operation without locks: USE IT CAREFULLY.
 */
#define _for_in_list(valptr, list)                                             \
    for (ListNode *__flag = (list), *valptr = __flag->next; valptr;            \
         valptr = valptr == __flag ? (void *)0 : valptr->next)

/**
 * Test if the list is empty.
 *
 * It's a list operation without locks: USE IT CAREFULLY.
 */
#define _empty_list(list) ((list)->next == (list))

#define merge_list(lock, node1, node2)                                         \
    ({                                                                         \
        acquire_spinlock(lock);                                               \
        ListNode *__t = _merge_list(node1, node2);                             \
        release_spinlock(lock);                                               \
        __t;                                                                   \
    })

#define insert_into_list(lock, list, node)                                     \
    ({                                                                         \
        acquire_spinlock(lock);                                               \
        ListNode *__t = _insert_into_list(list, node);                         \
        release_spinlock(lock);                                               \
        __t;                                                                   \
    })

#define detach_from_list(lock, node)                                           \
    ({                                                                         \
        acquire_spinlock(lock);                                               \
        ListNode *__t = _detach_from_list(node);                               \
        release_spinlock(lock);                                               \
        __t;                                                                   \
    })

/* Lockfree queue: implemented as a lock-free single linked list. */
struct queue_node {
    struct queue_node *next;
};

typedef struct queue_node QueueNode;

/* Add a node to the queue and return the added node. */
struct queue_node *add_to_queue(struct queue_node **head,
                                struct queue_node *node);

/* Remove the last added node from the queue and return it. */
struct queue_node *fetch_from_queue(struct queue_node **head);

/* Remove all nodes from the queue and return them as a single list. */
struct queue_node *fetch_all_from_queue(struct queue_node **head);

struct queue {
    struct list_node *begin;
    struct list_node *end;
    int size;
    struct spinlock lock;
};

typedef struct queue Queue;

void queue_init(struct queue *x);
void queue_lock(struct queue *x);
void queue_unlock(struct queue *x);
void queue_push(struct queue *x, struct list_node *item);
void queue_pop(struct queue *x);
struct list_node *queue_front(struct queue *x);
bool queue_empty(struct queue *x);

struct list {
    struct list_node *head;
    usize size;
    struct spinlock lock;
};

typedef struct list List;

void list_init(struct list *l);
void list_lock(struct list *l);
void list_unlock(struct list *l);
void list_insert(struct list *l, struct list_node *item,
                 struct list_node *target);
void list_remove(struct list *l, struct list_node *item);
void list_push_back(struct list *l, struct list_node *item);
void list_pop_head(struct list *l);
void list_pop_back(struct list *l);

#define list_forall(valptr, list)                                              \
    for (struct list_node *__flag = (list.head),                               \
                          *valptr = __flag ? __flag->next : NULL;              \
         valptr; valptr = valptr == __flag ? (void *)0 : valptr->next)
