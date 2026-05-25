#ifndef _RBTREE_H
#define _RBTREE_H 1

#include <stddef.h>

#ifndef containerof
#define containerof(p,t,m) ((t *)((char *)(p) - offsetof(t, m)))
#endif // containerof

typedef enum rb_color {
    RB_BLACK = 0,
    RB_RED = 1
} rb_color_t;

typedef struct rb_node {
    struct rb_node *parent;
    struct rb_node *left;
    struct rb_node *right;
    rb_color_t color;
} rb_node_t;

typedef int (*rb_comparator_t)(const void *, const void *);

typedef struct rb_tree {
    rb_node_t *root;
    rb_comparator_t compare;
    size_t size;
} rb_tree_t;

#define RB_TREE_INIT(compare_func) { .root = NULL, .compare = compare_func, .size = 0 }

int rb_insert(rb_tree_t *tree, rb_node_t *node);
void rb_remove(rb_tree_t *tree, rb_node_t *node);

rb_node_t *rb_find(rb_tree_t *tree, const rb_node_t *key);

rb_node_t *rb_next(const rb_node_t *node);
rb_node_t *rb_prev(const rb_node_t *node);

rb_node_t *rb_first(const rb_tree_t *tree);
rb_node_t *rb_last (const rb_tree_t *tree);

rb_node_t *rb_upper_bound(const rb_tree_t *tree, const rb_node_t *key);
rb_node_t *rb_lower_bound(const rb_tree_t *tree, const rb_node_t *key);

#define rb_for_each(node, tree) \
    for ((node) = rb_first(tree); (node) != NULL; (node) = rb_next(node))
 
#define rb_for_each_safe(node, tmp, tree) \
    for ((node) = rb_first(tree); \
         (node) != NULL && ((tmp) = rb_next(node), 1); \
         (node) = (tmp))

#endif // _RBTREE_H