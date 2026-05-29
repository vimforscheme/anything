#include <stdio.h>
#include <stdlib.h>
#include "dlist.h"

struct person {
	int id;
	const char *name;
	dlist_node_t node;
};

static void print_list(dlist_node_t *head)
{
	struct person *p;
	printf("  list (head=%p):", (void *)head);
	dlist_for_each_entry(p, head, node) {
		printf(" [id=%d name=%s]", p->id, p->name);
	}
	if (dlist_empty(head)) {
		printf(" (empty)");
	}
	printf("\n");
}

static struct person *person_new(int id, const char *name)
{
	struct person *p = malloc(sizeof(*p));
	if (p == NULL) {
		return NULL;
	}
	p->id = id;
	p->name = name;
	return p;
}

int main(void)
{
	DLIST_HEAD(people);
	dlist_node_t others;
	dlist_init(&others);

	printf("=== 1. Insert at front and back ===\n");
	struct person *alice = person_new(1, "Alice");
	struct person *bob   = person_new(2, "Bob");
	struct person *carol = person_new(3, "Carol");
	struct person *dave  = person_new(4, "Dave");

	dlist_add(&bob->node, &people);       /* Bob */
	dlist_add(&alice->node, &people);      /* Alice -> Bob */
	dlist_add_tail(&dave->node, &people);  /* Alice -> Bob -> Dave */
	dlist_add_tail(&carol->node, &people); /* Alice -> Bob -> Dave -> Carol */
	print_list(&people);

	printf("\n=== 2. Forward traversal ===\n");
	struct person *p;
	dlist_for_each_entry(p, &people, node) {
		printf("  person: id=%d name=%s\n", p->id, p->name);
	}

	printf("\n=== 3. Reverse traversal ===\n");
	dlist_for_each_entry_reverse(p, &people, node) {
		printf("  person: id=%d name=%s\n", p->id, p->name);
	}

	printf("\n=== 4. Delete Bob by traversing safely ===\n");
	struct person *n;
	dlist_for_each_entry_safe(p, n, &people, node) {
		if (p->id == 2) {
			dlist_del(&p->node);
			free(p);
		}
	}
	print_list(&people);

	printf("\n=== 5. Move Dave to the other list ===\n");
	dlist_move(&dave->node, &others);
	printf("  people:");  print_list(&people);
	printf("  others:");  print_list(&others);

	printf("\n=== 6. Splice others into people at tail ===\n");
	dlist_splice_tail_init(&others, &people);
	printf("  people:");  print_list(&people);
	printf("  others:");  print_list(&others);

	printf("\n=== 7. Clean up remaining nodes ===\n");
	dlist_for_each_entry_safe(p, n, &people, node) {
		dlist_del(&p->node);
		free(p);
	}
	print_list(&people);

	return 0;
}
