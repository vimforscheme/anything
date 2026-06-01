#ifndef PATRICIA_H
#define PATRICIA_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <arpa/inet.h>

#define RULE_PROTO_NUM 3  /* ICMP=0, TCP=1, UDP=2 */

typedef struct {
	uint8_t  proto;
	uint16_t port_lo;
	uint16_t port_hi;
} rule_t;

typedef struct patricia_node {
	struct patricia_node *left;
	struct patricia_node *right;
	int                   bit;
	int                   is_end;
	rule_t               *rules[RULE_PROTO_NUM];
	uint8_t              *key;
	uint8_t               key_len;
	int                   prefix_bits;
} patricia_node_t;

typedef struct {
	patricia_node_t *v4_root;
	patricia_node_t *v6_root;
} patricia_table_t;

/* ─── Custom allocator hooks ───────────────────────────────── */

extern void *(*patricia_alloc)(size_t);
extern void  (*patricia_free)(void *);

/* ─── Bit test ──────────────────────────────────────────────── */

#define BIT_TEST(addr, bit) \
	((((const uint8_t *)(addr))[(bit) >> 3] >> (7 - ((bit) & 7))) & 1)

/* ─── Protocol index ───────────────────────────────────────── */

static inline int proto_to_idx(uint8_t proto)
{
	if (proto == IPPROTO_ICMP) return 0;
	if (proto == IPPROTO_TCP)  return 1;
	if (proto == IPPROTO_UDP)  return 2;
	return -1;
}

/* ─── API ──────────────────────────────────────────────────── */

void  patricia_init_table(patricia_table_t *table);
void  patricia_destroy(patricia_node_t *root);

int   patricia_insert(patricia_node_t **root,
		      const void *key, int bit_len,
		      rule_t *rule, int proto_idx);

rule_t **patricia_search(patricia_node_t *root,
			  const void *key, int bit_len);

rule_t **patricia_lpm(patricia_node_t *root,
		       const void *key, int bit_len);

int   patricia_delete(patricia_node_t **root,
		      const void *key, int bit_len,
		      int proto_idx);

rule_t *acl_lookup(patricia_table_t *table,
		   const void *addr, int af,
		   uint8_t proto, uint16_t port);

#endif /* PATRICIA_H */
