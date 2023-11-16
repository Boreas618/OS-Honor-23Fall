#pragma once

#include <common/defines.h>
#include <common/spinlock.h>

/* ListNode represents one node on a circular list. */
typedef struct ListNode {
    struct ListNode *prev, *next;
} ListNode;

/* Initialize a sigle node circular list. */
void init_list_node(ListNode* node);

/*
 * Merge the list containing `node1` and the list containing `node2` into one list.
 * It guarantees `node1->next == node2`. Both lists can be empty. This function will return the merged list.
 * 
 * It's a list operation without locks: USE IT CAREFULLY.
 */
ListNode* _merge_list(ListNode* node1, ListNode* node2);

/*
 * Insert a single new node into the list
 *
 * It's a list operation without locks: USE IT CAREFULLY.
 */
#define _insert_into_list(list, node) (init_list_node(node), _merge_list(list, node))

/* 
 * Remove `node` from the list, and then `node` becomes a single
 * node list. It usually returns `node->prev`. If `node` is
 * the last one in the list, it will return NULL.
 * 
 * It's a list operation without locks: USE IT CAREFULLY.
 */
ListNode* _detach_from_list(ListNode* node);

/*
 * Iterate the list.
 * 
 * It's a list operation without locks: USE IT CAREFULLY.
 */
#define _for_in_list(valptr, list) \
    for (ListNode* __flag = (list), *valptr = __flag->next; valptr; \
         valptr = valptr == __flag ? (void*)0 : valptr->next)

/*
 * Test if the list is empty.
 * 
 * It's a list operation without locks: USE IT CAREFULLY.
 */
#define _empty_list(list) ((list)->next == (list))

#define merge_list(lock, node1, node2) \
    ({ \
        _acquire_spinlock(lock); \
        ListNode* __t = _merge_list(node1, node2); \
        _release_spinlock(lock); \
        __t; \
    })

#define insert_into_list(lock, list, node) \
    ({ \
        _acquire_spinlock(lock); \
        ListNode* __t = _insert_into_list(list, node); \
        _release_spinlock(lock); \
        __t; \
    })

#define detach_from_list(lock, node) \
    ({ \
        _acquire_spinlock(lock); \
        ListNode* __t = _detach_from_list(node); \
        _release_spinlock(lock); \
        __t; \
    })

/* Lockfree Queue: implemented as a lock-free single linked list. */
typedef struct QueueNode {
    struct QueueNode* next;
} QueueNode;

/* Add a node to the queue and return the added node. */
QueueNode* add_to_queue(QueueNode** head, QueueNode* node);

/* Remove the last added node from the queue and return it. */
QueueNode* fetch_from_queue(QueueNode** head);

/* Remove all nodes from the queue and return them as a single list. */
QueueNode* fetch_all_from_queue(QueueNode** head);

typedef struct Queue {
    ListNode* begin;
    ListNode* end;
    int size;
    SpinLock lock;
} Queue;

void queue_init(Queue* x);
void queue_lock(Queue* x);
void queue_unlock(Queue* x);
void queue_push(Queue* x, ListNode* item);
void queue_pop(Queue* x);
ListNode* queue_front(Queue* x);
bool queue_empty(Queue* x);

typedef struct List {
    ListNode* head;
    int size;
    SpinLock lock;
} List;

void list_init(List* l);
void list_lock(List* l);
void list_unlock(List *l);
void list_insert(List* l, ListNode* item, ListNode* target);
void list_remove(List* l, ListNode* item);
void list_push_head(List* l, ListNode* item);
void list_push_back(List* l, ListNode* item);
void list_pop_head(List* l);
void list_pop_back(List* l);

#define list_size(l) (l.size)
#define list_head(l) (l.head)
#define is_empty(l) (l.size == 0)