#ifndef RBTREE_H
#define RBTREE_H

#include <stddef.h>

#define RB_RED    0
#define RB_BLACK  1

typedef struct rbtree_node {
	struct rbtree_node *parent;
	struct rbtree_node *left;
	struct rbtree_node *right;
	int color;
} rbtree_node_t;

typedef struct {
	rbtree_node_t *root;
} rbtree_root_t;

#define rbtree_entry(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))

/* ─── Rotation ────────────────────────────────────────────────── */

static inline void rbtree_rotate_left(rbtree_root_t *root, rbtree_node_t *node)
{
	rbtree_node_t *right = node->right;
	rbtree_node_t *parent = node->parent;

	node->right = right->left;
	if (right->left != NULL) {
		right->left->parent = node;
	}
	right->parent = parent;
	if (parent == NULL) {
		root->root = right;
	} else if (node == parent->left) {
		parent->left = right;
	} else {
		parent->right = right;
	}
	right->left = node;
	node->parent = right;
}

static inline void rbtree_rotate_right(rbtree_root_t *root, rbtree_node_t *node)
{
	rbtree_node_t *left = node->left;
	rbtree_node_t *parent = node->parent;

	node->left = left->right;
	if (left->right != NULL) {
		left->right->parent = node;
	}
	left->parent = parent;
	if (parent == NULL) {
		root->root = left;
	} else if (node == parent->right) {
		parent->right = left;
	} else {
		parent->left = left;
	}
	left->right = node;
	node->parent = left;
}

/* ─── Min / Max / Next / Prev ─────────────────────────────────── */

static inline rbtree_node_t *rbtree_first(const rbtree_root_t *root)
{
	rbtree_node_t *n = root->root;
	rbtree_node_t *prev = NULL;

	while (n != NULL) {
		prev = n;
		n = n->left;
	}
	return prev;
}

static inline rbtree_node_t *rbtree_last(const rbtree_root_t *root)
{
	rbtree_node_t *n = root->root;
	rbtree_node_t *prev = NULL;

	while (n != NULL) {
		prev = n;
		n = n->right;
	}
	return prev;
}

static inline rbtree_node_t *rbtree_next(const rbtree_node_t *node)
{
	rbtree_node_t *n;

	if (node->right != NULL) {
		n = node->right;
		while (n->left != NULL) {
			n = n->left;
		}
		return n;
	}
	n = node->parent;
	while (n != NULL && node == n->right) {
		node = n;
		n = n->parent;
	}
	return n;
}

static inline rbtree_node_t *rbtree_prev(const rbtree_node_t *node)
{
	rbtree_node_t *n;

	if (node->left != NULL) {
		n = node->left;
		while (n->right != NULL) {
			n = n->right;
		}
		return n;
	}
	n = node->parent;
	while (n != NULL && node == n->left) {
		node = n;
		n = n->parent;
	}
	return n;
}

/* ─── Find ────────────────────────────────────────────────────── */

static inline rbtree_node_t *rbtree_find(const void *key,
					  const rbtree_root_t *root,
					  int (*cmp)(const void *,
						     const rbtree_node_t *))
{
	rbtree_node_t *node = root->root;
	int c;

	while (node != NULL) {
		c = cmp(key, node);
		if (c < 0) {
			node = node->left;
		} else if (c > 0) {
			node = node->right;
		} else {
			return node;
		}
	}
	return NULL;
}

/* ─── Insert fixup helpers ────────────────────────────────────── */

/*
 * node's parent is the LEFT child of grandparent.
 * Returns the next node to examine (for Case 1),
 * or NULL when the tree is balanced.
 */
static inline rbtree_node_t *
rbtree_insert_fixup_left(rbtree_root_t *root, rbtree_node_t *node)
{
	rbtree_node_t *parent = node->parent;
	rbtree_node_t *grand = parent->parent;
	rbtree_node_t *uncle = grand->right;

	if (uncle != NULL && uncle->color == RB_RED) {
		parent->color = RB_BLACK;
		uncle->color = RB_BLACK;
		grand->color = RB_RED;
		return grand;
	}
	if (node == parent->right) {
		node = parent;
		rbtree_rotate_left(root, parent);
	}
	{
		rbtree_node_t *p = node->parent;
		rbtree_node_t *g = p->parent;

		p->color = RB_BLACK;
		g->color = RB_RED;
		rbtree_rotate_right(root, g);
	}
	return NULL;
}

/*
 * node's parent is the RIGHT child of grandparent (mirror).
 */
static inline rbtree_node_t *
rbtree_insert_fixup_right(rbtree_root_t *root, rbtree_node_t *node)
{
	rbtree_node_t *parent = node->parent;
	rbtree_node_t *grand = parent->parent;
	rbtree_node_t *uncle = grand->left;

	if (uncle != NULL && uncle->color == RB_RED) {
		parent->color = RB_BLACK;
		uncle->color = RB_BLACK;
		grand->color = RB_RED;
		return grand;
	}
	if (node == parent->left) {
		node = parent;
		rbtree_rotate_right(root, parent);
	}
	{
		rbtree_node_t *p = node->parent;
		rbtree_node_t *g = p->parent;

		p->color = RB_BLACK;
		g->color = RB_RED;
		rbtree_rotate_left(root, g);
	}
	return NULL;
}

/* ─── Insert ──────────────────────────────────────────────────── */

static inline void rbtree_insert_fixup(rbtree_root_t *root,
					rbtree_node_t *node)
{
	rbtree_node_t *parent;

	while ((parent = node->parent) != NULL
		&& parent->color == RB_RED) {
		if (parent == parent->parent->left) {
			node = rbtree_insert_fixup_left(root, node);
		} else {
			node = rbtree_insert_fixup_right(root, node);
		}
		if (node == NULL) {
			break;
		}
	}
	root->root->color = RB_BLACK;
}

static inline rbtree_node_t *
rbtree_insert(rbtree_node_t *node, rbtree_root_t *root,
	      int (*cmp)(const rbtree_node_t *, const rbtree_node_t *))
{
	rbtree_node_t *parent = NULL;
	rbtree_node_t **link = &root->root;
	int c;

	while (*link != NULL) {
		parent = *link;
		c = cmp(node, parent);
		if (c < 0) {
			link = &parent->left;
		} else if (c > 0) {
			link = &parent->right;
		} else {
			return NULL;
		}
	}
	node->parent = parent;
	node->left = NULL;
	node->right = NULL;
	node->color = RB_RED;
	*link = node;
	rbtree_insert_fixup(root, node);
	return node;
}

/* ─── Erase fixup helpers ─────────────────────────────────────── */

static inline rbtree_node_t *
rbtree_erase_fixup_left(rbtree_root_t *root, rbtree_node_t *node,
			rbtree_node_t **parent_ptr)
{
	rbtree_node_t *parent = *parent_ptr;
	rbtree_node_t *sibling = parent->right;

	/* Case 1: sibling is red */
	if (sibling->color == RB_RED) {
		sibling->color = RB_BLACK;
		parent->color = RB_RED;
		rbtree_rotate_left(root, parent);
		sibling = parent->right;
	}
	/* Case 2: sibling is black, both children black */
	if ((sibling->left == NULL
	     || sibling->left->color == RB_BLACK)
	    && (sibling->right == NULL
		|| sibling->right->color == RB_BLACK)) {
		sibling->color = RB_RED;
		node = parent;
		*parent_ptr = parent->parent;
		return node;
	}
	/* Case 3: far child is black */
	if (sibling->right == NULL
	    || sibling->right->color == RB_BLACK) {
		sibling->left->color = RB_BLACK;
		sibling->color = RB_RED;
		rbtree_rotate_right(root, sibling);
		sibling = parent->right;
	}
	/* Case 4: far child is red */
	sibling->color = parent->color;
	parent->color = RB_BLACK;
	sibling->right->color = RB_BLACK;
	rbtree_rotate_left(root, parent);
	node = root->root;
	*parent_ptr = NULL;
	return node;
}

static inline rbtree_node_t *
rbtree_erase_fixup_right(rbtree_root_t *root, rbtree_node_t *node,
			 rbtree_node_t **parent_ptr)
{
	rbtree_node_t *parent = *parent_ptr;
	rbtree_node_t *sibling = parent->left;

	if (sibling->color == RB_RED) {
		sibling->color = RB_BLACK;
		parent->color = RB_RED;
		rbtree_rotate_right(root, parent);
		sibling = parent->left;
	}
	if ((sibling->left == NULL
	     || sibling->left->color == RB_BLACK)
	    && (sibling->right == NULL
		|| sibling->right->color == RB_BLACK)) {
		sibling->color = RB_RED;
		node = parent;
		*parent_ptr = parent->parent;
		return node;
	}
	if (sibling->left == NULL
	    || sibling->left->color == RB_BLACK) {
		sibling->right->color = RB_BLACK;
		sibling->color = RB_RED;
		rbtree_rotate_left(root, sibling);
		sibling = parent->left;
	}
	sibling->color = parent->color;
	parent->color = RB_BLACK;
	sibling->left->color = RB_BLACK;
	rbtree_rotate_right(root, parent);
	node = root->root;
	*parent_ptr = NULL;
	return node;
}

/* ─── Erase ───────────────────────────────────────────────────── */

static inline void rbtree_erase_fixup(rbtree_root_t *root,
				       rbtree_node_t *child,
				       rbtree_node_t *parent)
{
	while (child != root->root
	       && (child == NULL || child->color == RB_BLACK)) {
		if (child == parent->left) {
			child = rbtree_erase_fixup_left(root,
							 child, &parent);
		} else {
			child = rbtree_erase_fixup_right(root,
							  child, &parent);
		}
	}
	if (child != NULL) {
		child->color = RB_BLACK;
	}
}

static inline void rbtree_erase(rbtree_node_t *node, rbtree_root_t *root)
{
	rbtree_node_t *child;
	rbtree_node_t *parent;
	rbtree_node_t *old;
	int color;

	old = node;
	/* Node has two children: find successor */
	if (node->left != NULL && node->right != NULL) {
		node = node->right;
		while (node->left != NULL) {
			node = node->left;
		}
		child = node->right;
		parent = node->parent;
		color = node->color;
		/* Link child to successor's parent */
		if (child != NULL) {
			child->parent = parent;
		}
		if (parent->left == node) {
			parent->left = child;
		} else {
			parent->right = child;
		}
		/* Transplant successor into old node's position */
		node->parent = old->parent;
		node->left = old->left;
		node->right = old->right;
		node->color = old->color;
		if (old->parent != NULL) {
			if (old->parent->left == old) {
				old->parent->left = node;
			} else {
				old->parent->right = node;
			}
		} else {
			root->root = node;
		}
		old->left->parent = node;
		if (old->right != NULL) {
			old->right->parent = node;
		}
	} else {
		/* Node has 0 or 1 child */
		child = (node->left != NULL) ? node->left : node->right;
		parent = node->parent;
		color = node->color;
		if (child != NULL) {
			child->parent = parent;
		}
		if (parent != NULL) {
			if (parent->left == node) {
				parent->left = child;
			} else {
				parent->right = child;
			}
		} else {
			root->root = child;
		}
	}
	if (color == RB_BLACK) {
		rbtree_erase_fixup(root, child, parent);
	}
}

/* ─── Replace ─────────────────────────────────────────────────── */

static inline void rbtree_replace(rbtree_node_t *old_node,
				   rbtree_node_t *new_node,
				   rbtree_root_t *root)
{
	rbtree_node_t *parent = old_node->parent;

	new_node->parent = parent;
	new_node->left = old_node->left;
	new_node->right = old_node->right;
	new_node->color = old_node->color;

	if (old_node->left != NULL) {
		old_node->left->parent = new_node;
	}
	if (old_node->right != NULL) {
		old_node->right->parent = new_node;
	}
	if (parent != NULL) {
		if (parent->left == old_node) {
			parent->left = new_node;
		} else {
			parent->right = new_node;
		}
	} else {
		root->root = new_node;
	}
}

/* ─── Traversal macro ─────────────────────────────────────────── */

/*
 * Double-for pattern: outer loop iterates over raw rbtree_node_t *,
 * inner loop runs once to compute the containing struct pointer.
 * This ensures rbtree_entry is never called with NULL, because
 * the outer loop exits when rbtree_next returns NULL.
 */
#define rbtree_for_each_entry(pos, root, member) \
	for (rbtree_node_t *_rb_n = rbtree_first(root); \
	     _rb_n != NULL; \
	     _rb_n = rbtree_next(_rb_n)) \
		for (pos = rbtree_entry(_rb_n, __typeof__(*pos), member); \
		     pos != NULL; \
		     pos = NULL)

/*
 * Safe variant: caches the next node pointer before the loop body runs,
 * allowing rbtree_erase(pos) inside the loop.  After erasing pos, the
 * caller must NOT also erase n — n is already queued as the next entry.
 */
#define rbtree_for_each_entry_safe(pos, n, root, member) \
	for (rbtree_node_t *_rb_n = rbtree_first(root), \
	     *_rb_next = NULL; \
	     _rb_n != NULL \
	         && (_rb_next = rbtree_next(_rb_n), 1); \
	     _rb_n = _rb_next) \
		for (pos = rbtree_entry(_rb_n, __typeof__(*pos), member), \
		     n = (_rb_next != NULL) \
		         ? rbtree_entry(_rb_next, __typeof__(*pos), member) \
		         : NULL; \
		     pos != NULL; \
		     pos = NULL)

#endif /* RBTREE_H */
