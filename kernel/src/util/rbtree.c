#include <util/rbtree.h>

#include <log/log.h>
#include <util/memory.h>

static inline rb_color_t _rb_node_color(const rb_node_t *node) {
    return node == NULL ? RB_BLACK : node->color;
}

static inline int _is_red(const rb_node_t *node) {
    return (node != NULL) && (node->color == RB_RED);
}

static inline void _replace_child(rb_tree_t *tree, rb_node_t *parent, rb_node_t *old_child, rb_node_t *new_child) {
    if (parent == NULL) {
        tree->root = new_child;
    } else if (parent->left == old_child) {
        parent->left = new_child;
    } else {
        parent->right = new_child;
    }
    if (new_child != NULL) {
        new_child->parent = parent;
    }
}

/*
 *      P                 R
 *     / \               / \
 *    L   R    =>       P   RR
 *       / \           / \
 *      RL  RR        L  RL
 */
static void _rotate_left(rb_tree_t *tree, rb_node_t *node) {
    rb_node_t *r = node->right;
    rb_node_t *rl = r->left;

    r->left = node;
    node->right = rl;

    if (rl) {
        rl->parent = node;
    }

    r->parent = node->parent;
    _replace_child(tree, node->parent, node, r);
    node->parent = r;
}

/*
 *        P               L
 *       / \             / \
 *      L   R   =>      LL   P
 *     / \                  / \
 *    LL  LR               LR   R
 */
static void _rotate_right(rb_tree_t *tree, rb_node_t *node) {
    rb_node_t *l = node->left;
    rb_node_t *lr = l->right;

    l->right = node;
    node->left = lr;

    if (lr) {
        lr->parent = node;
    }

    l->parent = node->parent;
    _replace_child(tree, node->parent, node, l);
    node->parent = l;
}

static void _insert_fixup(rb_tree_t *tree, rb_node_t *node) {
    while (_is_red(node->parent)) {
        if (node->parent == node->parent->parent->left) {
            rb_node_t *uncle = node->parent->parent->right;
            if (_is_red(uncle)) {
                node->parent->color = RB_BLACK;
                uncle->color = RB_BLACK;
                node->parent->parent->color = RB_RED;
                node = node->parent->parent;
            } else {
                if (node == node->parent->right) {
                    node = node->parent;
                    _rotate_left(tree, node);
                }
                node->parent->color = RB_BLACK;
                node->parent->parent->color = RB_RED;
                _rotate_right(tree, node->parent->parent);
            }
        } else {
            rb_node_t *uncle = node->parent->parent->left;
            if (_is_red(uncle)) {
                node->parent->color = RB_BLACK;
                uncle->color = RB_BLACK;
                node->parent->parent->color = RB_RED;
                node = node->parent->parent;
            } else {
                if (node == node->parent->left) {
                    node = node->parent;
                    _rotate_right(tree, node);
                }
                node->parent->color = RB_BLACK;
                node->parent->parent->color = RB_RED;
                _rotate_left(tree, node->parent->parent);
            }
        }
    }
    tree->root->color = RB_BLACK;
}

int rb_insert(rb_tree_t *tree, rb_node_t *node) {
    rb_node_t *parent = NULL;
    rb_node_t **link = &tree->root;
 
    while (*link != NULL) {
        int cmp = tree->compare(node, *link);

        parent = *link;
 
        if (cmp < 0) {
            link = &(*link)->left;
        } else if (cmp > 0) {
            link = &(*link)->right;
        } else {
            return -1;
        }
    }
 
    node->parent = parent;
    node->left = NULL;
    node->right = NULL;
    node->color = RB_RED;

    *link = node;
    tree->size++;
 
    _insert_fixup(tree, node);
    return 0;
}

static void _transplant(rb_tree_t *tree, rb_node_t *u, rb_node_t *v) {
    _replace_child(tree, u->parent, u, v);
    if (v != NULL) {
        v->parent = u->parent;
    }
}

static void _remove_fixup(rb_tree_t *tree, rb_node_t *node, rb_node_t *parent) {
    while ((node == NULL || node->color == RB_BLACK) && node != tree->root) {
        if (node == parent->left) {
            rb_node_t *sibling = parent->right;
            if (_is_red(sibling)) {
                sibling->color = RB_BLACK;
                parent->color = RB_RED;
                _rotate_left(tree, parent);
                sibling = parent->right;
            }
            if (_rb_node_color(sibling->left) == RB_BLACK && _rb_node_color(sibling->right) == RB_BLACK) {
                sibling->color = RB_RED;
                node = parent;
                parent = node->parent;
            } else {
                if (_rb_node_color(sibling->right) == RB_BLACK) {
                    sibling->left->color = RB_BLACK;
                    sibling->color = RB_RED;
                    _rotate_right(tree, sibling);
                    sibling = parent->right;
                }
                sibling->color = parent->color;
                parent->color = RB_BLACK;
                if (sibling->right) {
                    sibling->right->color = RB_BLACK;
                }
                _rotate_left(tree, parent);
                node = tree->root; // Exit loop
            }
        } else {
            rb_node_t *sibling = parent->left;
            if (_is_red(sibling)) {
                sibling->color = RB_BLACK;
                parent->color = RB_RED;
                _rotate_right(tree, parent);
                sibling = parent->left;
            }
            if (_rb_node_color(sibling->left) == RB_BLACK && _rb_node_color(sibling->right) == RB_BLACK) {
                sibling->color = RB_RED;
                node = parent;
                parent = node->parent;
            } else {
                if (_rb_node_color(sibling->left) == RB_BLACK) {
                    sibling->right->color = RB_BLACK;
                    sibling->color = RB_RED;
                    _rotate_left(tree, sibling);
                    sibling = parent->left;
                }
                sibling->color = parent->color;
                parent->color = RB_BLACK;
                if (sibling->left) {
                    sibling->left->color = RB_BLACK;
                }
                _rotate_right(tree, parent);
                node = tree->root; // Exit loop
            }
        }
    }
    if (node) {
        node->color = RB_BLACK;
    }
}

void rb_remove(rb_tree_t *tree, rb_node_t *node) {
    rb_node_t *y = node;
    rb_node_t *x = NULL;
    rb_node_t *x_parent = NULL;
    rb_color_t y_orig_color = y->color;
 
    if (node->left == NULL) {
        x = node->right;
        x_parent = node->parent;

        _transplant(tree, node, node->right);
    } else if (node->right == NULL) {
        x = node->left;
        x_parent = node->parent;

        _transplant(tree, node, node->left);
    } else {
        y = node->right;

        while (y->left != NULL) {
            y = y->left;
        }
 
        y_orig_color = y->color;
        x = y->right;
 
        if (y->parent == node) {
            x_parent = y;
        } else {
            x_parent = y->parent;

            _transplant(tree, y, y->right);

            y->right = node->right;
            y->right->parent = y;
        }
 
        _transplant(tree, node, y);

        y->left = node->left;
        y->left->parent = y;
        y->color = node->color;
    }
 
    tree->size--;
 
    if (y_orig_color == RB_BLACK) {
        _remove_fixup(tree, x, x_parent);
    }
}

rb_node_t *rb_find(rb_tree_t *tree, const rb_node_t *key) {
    rb_node_t *current = tree->root;

    while (current != NULL) {
        int cmp = tree->compare(key, current);
        if (cmp < 0) {
            current = current->left;
        } else if (cmp > 0) {
            current = current->right;
        } else {
            return current;
        }
    }

    return NULL;
}

rb_node_t *rb_lower_bound(const rb_tree_t *tree, const rb_node_t *key) {
    rb_node_t *current = tree->root;
    rb_node_t *result = NULL;

    while (current != NULL) {
        int cmp = tree->compare(key, current);
        if (cmp < 0) {
            result = current;
            current = current->left;
        } else if (cmp > 0) {
            current = current->right;
        } else {
            return current;
        }
    }

    return result;
}

rb_node_t *rb_upper_bound(const rb_tree_t *tree, const rb_node_t *key) {
    rb_node_t *current = tree->root;
    rb_node_t *result = NULL;

    while (current != NULL) {
        int cmp = tree->compare(key, current);
        if (cmp < 0) {
            result = current;
            current = current->left;
        } else {
            current = current->right;
        }
    }

    return result;
}

rb_node_t *rb_first(const rb_tree_t *tree) {
    rb_node_t *current = tree->root;
    if (current == NULL) {
        return NULL;
    }
    while (current->left != NULL) {
        current = current->left;
    }
    return current;
}

rb_node_t *rb_last(const rb_tree_t *tree) {
    rb_node_t *current = tree->root;
    if (current == NULL) {
        return NULL;
    }
    while (current->right != NULL) {
        current = current->right;
    }
    return current;
}

rb_node_t *rb_next(const rb_node_t *node) {
    if (node->right != NULL) {
        node = node->right;
        while (node->left != NULL) {
            node = node->left;
        }
        return (rb_node_t *)node;
    }
    rb_node_t *parent = node->parent;
    while (parent != NULL && node == parent->right) {
        node = parent;
        parent = parent->parent;
    }
    return parent;
}

rb_node_t *rb_prev(const rb_node_t *node) {
    if (node->left != NULL) {
        node = node->left;
        while (node->right != NULL) {
            node = node->right;
        }
        return (rb_node_t *)node;
    }
    rb_node_t *parent = node->parent;
    while (parent != NULL && node == parent->left) {
        node = parent;
        parent = parent->parent;
    }
    return parent;
}