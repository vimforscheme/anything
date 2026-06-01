#ifndef SKIPLIST_H
#define SKIPLIST_H

#include <stddef.h>
#include <stdlib.h>   /* rand() */
#include <string.h>   /* memset */

/*
 * NOTE: The traversal macros (skiplist_for_each_entry) use __typeof__,
 * a GCC/Clang extension.  This header assumes a GCC-compatible compiler.
 */

#ifndef SKIPLIST_MAX_LEVEL
#define SKIPLIST_MAX_LEVEL 16
#endif

typedef struct skiplist_node {
	struct skiplist_node *prev;
	struct skiplist_node *next[SKIPLIST_MAX_LEVEL];
	int level;
} skiplist_node_t;

typedef struct {
	skiplist_node_t *head[SKIPLIST_MAX_LEVEL];
	int level;
} skiplist_t;

#define skiplist_entry(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))

/* ─── Init / Empty ───────────────────────────────────────────── */

static inline void skiplist_init(skiplist_t *list)
{
	int i;

	for (i = 0; i < SKIPLIST_MAX_LEVEL; i++) {
		list->head[i] = NULL;
	}
	list->level = 0;
}

static inline int skiplist_empty(const skiplist_t *list)
{
	return list->head[0] == NULL;
}

/* ─── Random level generator ──────────────────────────────────── */

/*
 * Coin-flip geometric distribution (p = 1/2), capped at MAX_LEVEL.
 * Caller sets node->level = skiplist_random_level() before insert.
 */
static inline int skiplist_random_level(void)
{
	int lvl = 1;

	while (lvl < SKIPLIST_MAX_LEVEL && (rand() & 1) != 0) {
		lvl++;
	}
	return lvl;
}

/* ─── First / Last / Next / Prev ─────────────────────────────── */

static inline skiplist_node_t *skiplist_first(const skiplist_t *list)
{
	return list->head[0];
}

/*
 * Find the last (rightmost) node in the skip list.
 *
 * Walks from the highest level down, always going as far right
 * as possible at each level before dropping down.
 * The key invariant of a skip list guarantees that if x is
 * the rightmost reachable node at level i, then x is also
 * present at level i-1 (higher levels are subsets of lower
 * levels).  Therefore switching from x->next[i] at level i
 * to x->next[i-1] at level i-1 never misses nodes that
 * appear only at lower levels.
 */
static inline skiplist_node_t *skiplist_last(const skiplist_t *list)
{
	skiplist_node_t *x = NULL;
	int i;

	for (i = list->level - 1; i >= 0; i--) {
		skiplist_node_t *next;

		next = (x == NULL) ? list->head[i] : x->next[i];
		while (next != NULL) {
			x = next;
			next = x->next[i];
		}
	}
	return x;
}

static inline skiplist_node_t *skiplist_next(const skiplist_node_t *node)
{
	return node->next[0];
}

static inline skiplist_node_t *skiplist_prev(const skiplist_node_t *node)
{
	return node->prev;
}

/* ─── Find ──────────────────────────────────────────────────── */

static inline skiplist_node_t *
skiplist_find(const void *key, const skiplist_t *list,
	      int (*cmp)(const void *, const skiplist_node_t *))
{
	skiplist_node_t *x = NULL;
	int i;

	for (i = list->level - 1; i >= 0; i--) {
		skiplist_node_t *next;
		int c;

		next = (x == NULL) ? list->head[i] : x->next[i];
		while (next != NULL) {
			c = cmp(key, next);
			if (c < 0) {
				break;
			}
			if (c == 0) {
				return next;
			}
			x = next;
			next = x->next[i];
		}
	}
	return NULL;
}

/* ─── Find predecessor (≤) ───────────────────────────────────── */

/*
 * Returns the rightmost node whose key is <= search_key,
 * or NULL if no such node exists.
 */
static inline skiplist_node_t *
skiplist_find_le(const void *key, const skiplist_t *list,
		 int (*cmp)(const void *, const skiplist_node_t *))
{
	skiplist_node_t *x = NULL;
	int i;

	for (i = list->level - 1; i >= 0; i--) {
		skiplist_node_t *next;
		int c;

		next = (x == NULL) ? list->head[i] : x->next[i];
		while (next != NULL) {
			c = cmp(key, next);
			if (c < 0) {
				break;
			}
			x = next;
			if (c == 0) {
				return x;
			}
			next = x->next[i];
		}
	}
	return x;
}

/* ─── Insert ────────────────────────────────────────────────── */

static inline skiplist_node_t *
skiplist_insert(skiplist_node_t *node, skiplist_t *list,
		int (*cmp)(const skiplist_node_t *,
			   const skiplist_node_t *))
{
	skiplist_node_t *update[SKIPLIST_MAX_LEVEL];
	skiplist_node_t *x = NULL;
	skiplist_node_t *next0;
	int i, lvl;

	memset(update, 0, sizeof(update));
	lvl = node->level;
	if (lvl > SKIPLIST_MAX_LEVEL) {
		lvl = SKIPLIST_MAX_LEVEL;
		node->level = lvl;
	}

	/* Find insertion position at each level */
	for (i = list->level - 1; i >= 0; i--) {
		skiplist_node_t *next;
		int c;

		next = (x == NULL) ? list->head[i] : x->next[i];
		while (next != NULL) {
			c = cmp(node, next);
			if (c <= 0) {
				break;
			}
			x = next;
			next = x->next[i];
		}
		update[i] = x;
	}

	/* Check for duplicate at level 0 */
	next0 = (update[0] == NULL) ? list->head[0] : update[0]->next[0];
	if (next0 != NULL && cmp(node, next0) == 0) {
		return NULL;
	}

	/* New levels beyond current max level: predecessor is head */
	for (i = list->level; i < lvl; i++) {
		update[i] = NULL;
	}
	if (lvl > list->level) {
		list->level = lvl;
	}

	/* Link node into each level 0 .. lvl-1 */
	for (i = 0; i < lvl; i++) {
		if (update[i] == NULL) {
			node->next[i] = list->head[i];
			list->head[i] = node;
		} else {
			node->next[i] = update[i]->next[i];
			update[i]->next[i] = node;
		}
	}

	/* Backward pointer at level 0 */
	node->prev = update[0];
	if (node->next[0] != NULL) {
		node->next[0]->prev = node;
	}

	return node;
}

/* ─── Erase ─────────────────────────────────────────────────── */

/*
 * Requires cmp because the skiplist has no parent pointers — we must
 * re-search from the head at every level to find the predecessors.
 */
static inline void
skiplist_erase(skiplist_node_t *node, skiplist_t *list,
	       int (*cmp)(const skiplist_node_t *,
			  const skiplist_node_t *))
{
	skiplist_node_t *update[SKIPLIST_MAX_LEVEL];
	skiplist_node_t *x = NULL;
	int i;

	/* Find the predecessor at each level */
	for (i = list->level - 1; i >= 0; i--) {
		skiplist_node_t *next;
		int c;

		next = (x == NULL) ? list->head[i] : x->next[i];
		while (next != NULL) {
			c = cmp(node, next);
			if (c <= 0) {
				break;
			}
			x = next;
			next = x->next[i];
		}
		update[i] = x;
	}

	/* Unlink node from each level it participates in */
	for (i = 0; i < node->level; i++) {
		if (update[i] == NULL) {
			if (list->head[i] == node) {
				list->head[i] = node->next[i];
			}
		} else {
			if (update[i]->next[i] == node) {
				update[i]->next[i] = node->next[i];
			}
		}
	}

	/* Repair backward pointer at level 0 */
	if (node->next[0] != NULL) {
		node->next[0]->prev = node->prev;
	}

	/* Shrink list level when top levels become empty */
	while (list->level > 0
	       && list->head[list->level - 1] == NULL) {
		list->level--;
	}
}

/* ─── Traversal macros ──────────────────────────────────────── */

#define skiplist_for_each_entry(pos, list, member) \
	for (skiplist_node_t *_sn = (list)->head[0]; \
	     _sn != NULL; \
	     _sn = _sn->next[0]) \
		for (pos = skiplist_entry(_sn, __typeof__(*pos), member); \
		     pos != NULL; \
		     pos = NULL)

/*
 * Safe variant: caches the next node pointer so the caller can
 * skiplist_erase(pos) inside the loop body.
 */
#define skiplist_for_each_entry_safe(pos, n, list, member) \
	for (skiplist_node_t *_sn = (list)->head[0], \
	     *_sn_next = NULL; \
	     _sn != NULL && (_sn_next = _sn->next[0], 1); \
	     _sn = _sn_next) \
		for (pos = skiplist_entry(_sn, __typeof__(*pos), member), \
		     n = (_sn_next != NULL) \
		         ? skiplist_entry(_sn_next, __typeof__(*pos), member) \
		         : NULL; \
		     pos != NULL; \
		     pos = NULL)

#endif /* SKIPLIST_H */
