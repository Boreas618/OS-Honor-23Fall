#ifndef _MYRBTREE_H
#define _MYRBTREE_H
#include <common/defines.h>
#include <common/spinlock.h>

struct rb_node_ {
    unsigned long __rb_parent_color;
    struct rb_node_ *rb_right;
    struct rb_node_ *rb_left;
} __attribute__((aligned(sizeof(long))));

typedef struct rb_node_ *rb_node;

struct rb_root_ {
    rb_node rb_node;
};

typedef struct rb_root_ *rb_root;

WARN_RESULT int _rb_insert(rb_node node, rb_root root, bool (*cmp)(rb_node lnode,rb_node rnode));

void _rb_erase(rb_node node, rb_root root);

rb_node _rb_lookup(rb_node node, rb_root rt, bool (*cmp)(rb_node lnode,rb_node rnode));

rb_node _rb_first(rb_root root);

typedef struct {
    struct rb_root_ _root;
    rb_root root;
    SpinLock rblock;
} RBTree;

void rbtree_init(RBTree* rbtree);

void rbtree_lock(RBTree* rbtree);

void rbtree_unlock(RBTree* rbtree);

int rbtree_insert(RBTree* rbtree, rb_node node, bool (*cmp)(rb_node lnode,rb_node rnode));

void rbtree_erase(RBTree* rbtree, rb_node node);

rb_node rbtree_lookup(RBTree* rbtree, rb_node node, bool (*cmp)(rb_node lnode,rb_node rnode));

rb_node rbtree_first(RBTree* rbtree);

#endif