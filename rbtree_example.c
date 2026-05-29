#include <stdio.h>
#include <stdlib.h>
#include "rbtree.h"

struct entry {
	int key;
	int val;
	rbtree_node_t rb;
};

static int cmp_node(const rbtree_node_t *a, const rbtree_node_t *b)
{
	const struct entry *ea = rbtree_entry(a, struct entry, rb);
	const struct entry *eb = rbtree_entry(b, struct entry, rb);
	return (ea->key > eb->key) - (ea->key < eb->key);
}

static int cmp_key(const void *key, const rbtree_node_t *node)
{
	int k = *(const int *)key;
	const struct entry *e = rbtree_entry(node, struct entry, rb);
	return (k > e->key) - (k < e->key);
}

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

static void print_tree(const char *label, rbtree_root_t *root)
{
	struct entry *e;
	printf("%s:", label);
	rbtree_for_each_entry(e, root, rb) {
		printf(" [k=%d v=%d c=%s]", e->key, e->val,
		       e->rb.color == RB_RED ? "R" : "B");
	}
	if (root->root == NULL) {
		printf(" (empty)");
	}
	printf("\n");
}

int main(void)
{
	rbtree_root_t tree = { NULL };

	printf("=== 1. Insert nodes ===\n");
	struct entry *e[10];
	int keys[] = {50, 30, 70, 20, 40, 60, 80, 10, 35, 65};
	int vals[] = {5, 3, 7, 2, 4, 6, 8, 1, 35, 65};
	int i;

	for (i = 0; i < 10; i++) {
		e[i] = entry_new(keys[i], vals[i]);
		rbtree_insert(&e[i]->rb, &tree, cmp_node);
	}
	print_tree("Tree", &tree);

	printf("\n=== 2. Find keys ===\n");
	rbtree_node_t *found;
	int search_keys[] = {40, 100, 10};
	for (i = 0; i < 3; i++) {
		found = rbtree_find(&search_keys[i], &tree, cmp_key);
		if (found != NULL) {
			struct entry *ent = rbtree_entry(found,
							  struct entry, rb);
			printf("  key=%d → found, val=%d\n",
			       search_keys[i], ent->val);
		} else {
			printf("  key=%d → not found\n", search_keys[i]);
		}
	}

	printf("\n=== 3. First/Last + Next/Prev traversal ===\n");
	{
		rbtree_node_t *node_ptr;
		node_ptr = rbtree_first(&tree);
		printf("  first: k=%d\n",
		       rbtree_entry(node_ptr, struct entry, rb)->key);
		node_ptr = rbtree_last(&tree);
		printf("  last:  k=%d\n",
		       rbtree_entry(node_ptr, struct entry, rb)->key);

		printf("  full next-chain: ");
		for (node_ptr = rbtree_first(&tree);
		     node_ptr != NULL;
		     node_ptr = rbtree_next(node_ptr)) {
			printf("%d ",
			       rbtree_entry(node_ptr, struct entry, rb)->key);
		}
		printf("\n");

		printf("  full prev-chain: ");
		for (node_ptr = rbtree_last(&tree);
		     node_ptr != NULL;
		     node_ptr = rbtree_prev(node_ptr)) {
			printf("%d ",
			       rbtree_entry(node_ptr, struct entry, rb)->key);
		}
		printf("\n");
	}

	printf("\n=== 4. Replace node (swap key=30 with key=300) ===\n");
	struct entry *repl = entry_new(300, 999);
	rbtree_replace(&e[1]->rb, &repl->rb, &tree);
	free(e[1]);
	e[1] = repl;
	print_tree("After replace", &tree);

	printf("\n=== 5. Erase nodes (leaf, single-child, two-child) ===\n");

	/* Erase leaf: key=10 */
	rbtree_erase(&e[7]->rb, &tree);
	free(e[7]);
	printf("  erased leaf (k=10):\n");
	print_tree("  ", &tree);

	/* Erase node with one child: key=80 */
	rbtree_erase(&e[6]->rb, &tree);
	free(e[6]);
	printf("  erased single-child (k=80):\n");
	print_tree("  ", &tree);

	/* Erase node with two children: key=50 (root) */
	rbtree_erase(&e[0]->rb, &tree);
	free(e[0]);
	printf("  erased two-child root (k=50):\n");
	print_tree("  ", &tree);

	printf("\n=== 6. Clean up remaining (safe traversal) ===\n");
	struct entry *pos, *n;
	rbtree_for_each_entry_safe(pos, n, &tree, rb) {
		(void)n;
		rbtree_erase(&pos->rb, &tree);
		free(pos);
	}
	print_tree("Tree", &tree);

	return 0;
}
