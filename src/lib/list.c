#include <lib/list.h>

void init_list_node(ListNode *node)
{
	node->prev = node;
	node->next = node;
}

ListNode *_merge_list(ListNode *node1, ListNode *node2)
{
	if (!node1)
		return node2;
	if (!node2)
		return node1;

	// before: (arrow is the next pointer)
	//   ... --> node1 --> node3 --> ...
	//   ... <-- node2 <-- node4 <-- ...
	//
	// after:
	//   ... --> node1 --+  +-> node3 --> ...
	//                   |  |
	//   ... <-- node2 <-+  +-- node4 <-- ...

	ListNode *node3 = node1->next;
	ListNode *node4 = node2->prev;

	node1->next = node2;
	node2->prev = node1;
	node4->next = node3;
	node3->prev = node4;

	return node1;
}

ListNode *_detach_from_list(ListNode *node)
{
	ListNode *prev = node->prev;

	node->prev->next = node->next;
	node->next->prev = node->prev;
	init_list_node(node);

	if (prev == node)
		return NULL;
	return prev;
}

QueueNode *add_to_queue(QueueNode **head, QueueNode *node)
{
	do
		node->next = *head;
	while (!__atomic_compare_exchange_n(head, &node->next, node, true,
					    __ATOMIC_ACQ_REL,
					    __ATOMIC_RELAXED));
	return node;
}

QueueNode *fetch_from_queue(QueueNode **head)
{
	QueueNode *node;
	do
		node = *head;
	while (node && !__atomic_compare_exchange_n(head, &node, node->next,
						    true, __ATOMIC_ACQ_REL,
						    __ATOMIC_RELAXED));
	return node;
}

QueueNode *fetch_all_from_queue(QueueNode **head)
{
	return __atomic_exchange_n(head, NULL, __ATOMIC_ACQ_REL);
}

void queue_init(Queue *x)
{
	x->begin = x->end = 0;
	x->size = 0;
	init_spinlock(&x->lock);
}

void queue_lock(Queue *x)
{
	acquire_spinlock(&x->lock);
}

void queue_unlock(Queue *x)
{
	release_spinlock(&x->lock);
}

void queue_push(Queue *x, ListNode *item)
{
	init_list_node(item);
	if (x->size == 0) {
		x->begin = x->end = item;
	} else {
		_merge_list(x->end, item);
		x->end = item;
	}
	x->size++;
}

void queue_pop(Queue *x)
{
	if (x->size == 0)
		PANIC();
	if (x->size == 1) {
		x->begin = x->end = 0;
	} else {
		auto t = x->begin;
		x->begin = x->begin->next;
		_detach_from_list(t);
	}
	x->size--;
}

ListNode *queue_front(Queue *x)
{
	if (!x || !x->begin)
		PANIC();
	return x->begin;
}

bool queue_empty(Queue *x)
{
	return x->size == 0;
}

void list_init(List *l)
{
	l->head = NULL;
	l->size = 0;
	init_spinlock(&l->lock);
}

void list_lock(List *l)
{
	acquire_spinlock(&l->lock);
}

void list_unlock(List *l)
{
	release_spinlock(&l->lock);
}

/* Insert the item before the target. */
void list_insert(List *l, ListNode *item, ListNode *target)
{
	ASSERT(target);
	ASSERT(target->prev);
	ASSERT(target->prev->next);

	init_list_node(item);

	if (l->size == 0) {
		l->head = item;
	} else {
		target->prev->next = item;
		item->prev = target->prev;
		item->next = target;
		target->prev = item;
	}
	l->size++;
}

void list_remove(List *l, ListNode *item)
{
	ASSERT(item->prev);
	ASSERT(item->next);

	if (l->size == 1) {
		if (item != l->head) {
			return;
		}
		l->head = NULL;
		l->size = 0;
		return;
	}

	if (item == l->head) {
		ASSERT(l->head->next != l->head);
		l->head = l->head->next;
	}
	_detach_from_list(item);
	l->size--;
}

void list_push_head(List *l, ListNode *item)
{
	ASSERT(l->head);
	ASSERT(item);

	init_list_node(item);

	list_insert(l, item, l->head);
	l->head = item;
}

void list_push_back(List *l, ListNode *item)
{
	init_list_node(item);

	if (l->head == NULL) {
		l->head = item;
		l->size++;
	} else {
		list_insert(l, item, l->head);
	}
}

void list_pop_head(List *l)
{
	list_remove(l, l->head);
}

void list_pop_back(List *l)
{
	list_remove(l, l->head->prev);
}