#ifndef DLIST_H
#define DLIST_H

#include <stddef.h>

typedef struct dlist_node {
	struct dlist_node *prev;
	struct dlist_node *next;
} dlist_node_t;

#define DLIST_HEAD_INIT(name) { &(name), &(name) }
#define DLIST_HEAD(name) dlist_node_t name = DLIST_HEAD_INIT(name)

#define dlist_entry(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))

static inline void dlist_init(dlist_node_t *head)
{
	head->next = head;
	head->prev = head;
}

static inline int dlist_empty(const dlist_node_t *head)
{
	return head->next == head;
}

static inline void __dlist_add(dlist_node_t *node,
				dlist_node_t *prev,
				dlist_node_t *next)
{
	next->prev = node;
	node->next = next;
	node->prev = prev;
	prev->next = node;
}

static inline void dlist_add(dlist_node_t *node, dlist_node_t *head)
{
	__dlist_add(node, head, head->next);
}

static inline void dlist_add_tail(dlist_node_t *node, dlist_node_t *head)
{
	__dlist_add(node, head->prev, head);
}

static inline void __dlist_del(dlist_node_t *prev, dlist_node_t *next)
{
	next->prev = prev;
	prev->next = next;
}

static inline void dlist_del(dlist_node_t *node)
{
	__dlist_del(node->prev, node->next);
}

static inline void dlist_del_init(dlist_node_t *node)
{
	__dlist_del(node->prev, node->next);
	dlist_init(node);
}

static inline void dlist_move(dlist_node_t *node, dlist_node_t *head)
{
	__dlist_del(node->prev, node->next);
	dlist_add(node, head);
}

static inline void dlist_move_tail(dlist_node_t *node, dlist_node_t *head)
{
	__dlist_del(node->prev, node->next);
	dlist_add_tail(node, head);
}

static inline int dlist_is_last(const dlist_node_t *node,
				const dlist_node_t *head)
{
	return node->next == head;
}

static inline void __dlist_splice(const dlist_node_t *list,
				   dlist_node_t *prev,
				   dlist_node_t *next)
{
	dlist_node_t *first = list->next;
	dlist_node_t *last = list->prev;

	first->prev = prev;
	prev->next = first;
	last->next = next;
	next->prev = last;
}

static inline void dlist_splice(const dlist_node_t *list, dlist_node_t *head)
{
	if (!dlist_empty(list)) {
		__dlist_splice(list, head, head->next);
	}
}

static inline void dlist_splice_tail(const dlist_node_t *list,
				      dlist_node_t *head)
{
	if (!dlist_empty(list)) {
		__dlist_splice(list, head->prev, head);
	}
}

static inline void dlist_splice_init(dlist_node_t *list, dlist_node_t *head)
{
	if (!dlist_empty(list)) {
		__dlist_splice(list, head, head->next);
		dlist_init(list);
	}
}

static inline void dlist_splice_tail_init(dlist_node_t *list,
					   dlist_node_t *head)
{
	if (!dlist_empty(list)) {
		__dlist_splice(list, head->prev, head);
		dlist_init(list);
	}
}

#define dlist_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); pos = pos->next)

#define dlist_for_each_prev(pos, head) \
	for (pos = (head)->prev; pos != (head); pos = pos->prev)

#define dlist_for_each_safe(pos, n, head) \
	for (pos = (head)->next, n = pos->next; pos != (head); \
	     pos = n, n = pos->next)

#define dlist_for_each_entry(pos, head, member) \
	for (pos = dlist_entry((head)->next, __typeof__(*pos), member); \
	     &pos->member != (head); \
	     pos = dlist_entry(pos->member.next, __typeof__(*pos), member))

#define dlist_for_each_entry_safe(pos, n, head, member) \
	for (pos = dlist_entry((head)->next, __typeof__(*pos), member), \
	     n = dlist_entry(pos->member.next, __typeof__(*pos), member); \
	     &pos->member != (head); \
	     pos = n, n = dlist_entry(n->member.next, __typeof__(*pos), member))

#define dlist_for_each_entry_reverse(pos, head, member) \
	for (pos = dlist_entry((head)->prev, __typeof__(*pos), member); \
	     &pos->member != (head); \
	     pos = dlist_entry(pos->member.prev, __typeof__(*pos), member))

#endif /* DLIST_H */
