#include "lib/rbtree.h"
#include "aarch64/intrinsic.h"
#include "lib/rc.h"
#include "lib/spinlock.h"
#include "test.h"
#include <kernel/printk.h>

struct mytype {
    struct rb_node_ node;
    int key;
    int data;
};

static bool rb_cmp(rb_node n1, rb_node n2) {
    return container_of(n1, struct mytype, node)->key <
           container_of(n2, struct mytype, node)->key;
}

static struct mytype p[4][1000], tmp;
static RBTree rbt;

#define FAIL(...)                                                              \
    {                                                                          \
        printk(__VA_ARGS__);                                                   \
        while (1)                                                              \
            ;                                                                  \
    }

static RefCount x;

void rbtree_test() {
    int cid = cpuid();
    for (int i = 0; i < 1000; i++) {
        p[cid][i].key = cid * 100000 + i;
        p[cid][i].data = -p[cid][i].key;
    }
    if (cid == 0) rbtree_init(&rbt);
    arch_dsb_sy();
    increment_rc(&x);
    while (x.count < 4)
        ;
    arch_dsb_sy();
    for (int i = 0; i < 1000; i++) {
        rbtree_lock(&rbt);
        int ok = rbtree_insert(&rbt, &p[cid][i].node, rb_cmp);
        if (ok) FAIL("insert failed!\n");
        rbtree_unlock(&rbt);
    }
    rbtree_lock(&rbt);
    rb_node np = rbtree_first(&rbt);
    if (np == NULL) FAIL("NULL\n");
    rbtree_unlock(&rbt);
    for (int i = 0; i < 1000; i++) {
        rbtree_lock(&rbt);
        tmp.key = cid * 100000 + i;
        rb_node np = rbtree_lookup(&rbt, &tmp.node, rb_cmp);
        if (np == NULL) FAIL("NULL\n");
        if (tmp.key != -container_of(np, struct mytype, node)->data) {
            FAIL("data error! %d %d %d\n", tmp.key,
                 container_of(np, struct mytype, node)->key,
                 container_of(np, struct mytype, node)->data);
        }
        rbtree_unlock(&rbt);
    }
    arch_dsb_sy();
    increment_rc(&x);
    while (x.count < 8)
        ;
    arch_dsb_sy();
    if (cid == 0) {
        printk("rbtree_test PASS\n");
    }
}