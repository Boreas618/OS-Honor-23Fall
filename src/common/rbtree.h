#ifndef _MYRBTREE_H
#define _MYRBTREE_H
#include "common/defines.h"
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
    rb_root root;
    SpinLock rblock;
} RBTree;

void rbtree_init(RBTree* rbtree) {
    init_spinlock(&rbtree->rblock);
}

int rbtree_insert(RBTree* rbtree, rb_node node, rb_root root, bool (*cmp)(rb_node lnode,rb_node rnode)) {
    int r = 0;
    _acquire_spinlock(&rbtree->rblock);
    r = _rb_insert(node, rbtree->root, cmp);
    _release_spinlock(&rbtree->rblock);
    return r;
}

void rbtree_erase(RBTree* rbtree, rb_node node) {
    _acquire_spinlock(&rbtree->rblock);
    _rb_erase(node, rbtree->root);
    _release_spinlock(&rbtree->rblock);
}

void rbtree_lookup(RBTree* rbtree, rb_node node, rb_root root, bool (*cmp)(rb_node lnode,rb_node rnode)) {
    int r = 0;
    _acquire_spinlock(&rbtree->rblock);
    r = _rb_lookup(node, rbtree->root, cmp);
    _release_spinlock(&rbtree->rblock);
    return r;
}

rb_node rbtree_first(RBTree* rbtree) {
    rb_node r = NULL;
    _acquire_spinlock(&rbtree->rblock);
    r = _rb_first(&rbtree->root);
    _release_spinlock(&rbtree->rblock);
    return r;
}

#endif