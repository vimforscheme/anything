#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "skiplist.h"

struct entry {
	int key;
	int val;
	skiplist_node_t sn;
};

/* ─── Comparison helpers ────────────────────────────────────── */

static int cmp_node(const skiplist_node_t *a, const skiplist_node_t *b)
{
	const struct entry *ea = skiplist_entry(a, struct entry, sn);
	const struct entry *eb = skiplist_entry(b, struct entry, sn);
	return (ea->key > eb->key) - (ea->key < eb->key);
}

static int cmp_key(const void *key, const skiplist_node_t *node)
{
	int k = *(const int *)key;
	const struct entry *e = skiplist_entry(node, struct entry, sn);
	return (k > e->key) - (k < e->key);
}

/* ─── Random level (coin-flip, p = 1/2) ─────────────────────── */

static int random_level(void)
{
	int lvl = 1;
	while ((rand() & 1) && lvl < SKIPLIST_MAX_LEVEL) {
		lvl++;
	}
	return lvl;
}

/* ─── Helpers ───────────────────────────────────────────────── */

static struct entry *entry_new(int key, int val)
{
	struct entry *e = malloc(sizeof(*e));
	if (e == NULL) {
		return NULL;
	}
	e->key = key;
	e->val = val;
	return e;
}

static void print_list(const char *label, const skiplist_t *list)
{
	struct entry *e;
	int count = 0;

	printf("%s:", label);
	skiplist_for_each_entry(e, list, sn) {
		printf(" [k=%d v=%d L%d]", e->key, e->val, e->sn.level);
		count++;
	}
	if (count == 0) {
		printf(" (empty)");
	}
	printf("  (level=%d count=%d)\n", list->level, count);
}

/* ─── Main ──────────────────────────────────────────────────── */

int main(void)
{
	skiplist_t list;

	srand((unsigned int)time(NULL));
	skiplist_init(&list);

	printf("=== 1. Insert 12 nodes ===\n");
	struct entry *entries[12];
	int keys[] = {50, 30, 70, 20, 40, 60, 80, 10, 35, 65, 90, 55};
	int vals[] = {5, 3, 7, 2, 4, 6, 8, 1, 35, 65, 9, 55};
	int i;

	for (i = 0; i < 12; i++) {
		entries[i] = entry_new(keys[i], vals[i]);
		entries[i]->sn.level = random_level();
		if (skiplist_insert(&entries[i]->sn, &list, cmp_node)
		    == NULL) {
			printf("  dup detected for key=%d\n", keys[i]);
		}
	}
	print_list("  List", &list);

	printf("\n=== 2. Find ===\n");
	skiplist_node_t *found;
	int search[] = {40, 100, 10};
	for (i = 0; i < 3; i++) {
		found = skiplist_find(&search[i], &list, cmp_key);
		if (found != NULL) {
			struct entry *e2 = skiplist_entry(found,
							    struct entry, sn);
			printf("  key=%d → found, val=%d level=%d\n",
			       search[i], e2->val, e2->sn.level);
		} else {
			printf("  key=%d → not found\n", search[i]);
		}
	}

	printf("\n=== 3. First / Last / Next / Prev ===\n");
	skiplist_node_t *node;
	node = skiplist_first(&list);
	printf("  first: k=%d\n",
	       skiplist_entry(node, struct entry, sn)->key);
	node = skiplist_last(&list);
	printf("  last:  k=%d\n",
	       skiplist_entry(node, struct entry, sn)->key);

	printf("  forward:  ");
	for (node = skiplist_first(&list);
	     node != NULL;
	     node = skiplist_next(node)) {
		struct entry *e2 = skiplist_entry(node, struct entry, sn);
		printf("%d ", e2->key);
	}
	printf("\n");

	printf("  backward: ");
	for (node = skiplist_last(&list);
	     node != NULL;
	     node = skiplist_prev(node)) {
		struct entry *e2 = skiplist_entry(node, struct entry, sn);
		printf("%d ", e2->key);
	}
	printf("\n");

	printf("\n=== 4. Erase nodes (k=20 leaf, k=80 with 1 prev) ===\n");
	skiplist_erase(&entries[3]->sn, &list, cmp_node);
	free(entries[3]);
	printf("  erased k=20:\n");
	print_list("  ", &list);

	skiplist_erase(&entries[6]->sn, &list, cmp_node);
	free(entries[6]);
	printf("  erased k=80:\n");
	print_list("  ", &list);

	printf("\n=== 5. Try duplicate insert (k=50) ===\n");
	{
		struct entry *dup = entry_new(50, 999);
		dup->sn.level = random_level();
		skiplist_node_t *r;
		r = skiplist_insert(&dup->sn, &list, cmp_node);
		if (r == NULL) {
			printf("  correctly rejected duplicate k=50\n");
			free(dup);
		}
	}
	print_list("  ", &list);

	printf("\n=== 6. Safe traversal: erase all ===\n");
	struct entry *pos, *n;
	skiplist_for_each_entry_safe(pos, n, &list, sn) {
		(void)n;
		skiplist_erase(&pos->sn, &list, cmp_node);
		free(pos);
	}
	print_list("  List", &list);

	return 0;
}
