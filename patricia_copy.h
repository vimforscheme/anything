#ifndef PATRICIA_H
#define PATRICIA_H

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif

#include <stdint.h>
#include <stddef.h>

#define RULE_PROTO_NUM 3 /* ICMP=0, TCP=1, UDP=2 */
#define PATRICIA_MAX_DEPTH 128

/* 外部策略资产结构体：解耦无侵入指针，支持多处复用挂载 */
typedef struct
{
	uint8_t proto;
	uint16_t port_lo;
	uint16_t port_hi;
} rule_t;

/* 容器内部专用包装链表节点：用于串联同网段同协议的多条并集规则 */
typedef struct rule_link
{
	rule_t *rule;
	struct rule_link *next;
} rule_link_t;

typedef struct patricia_node
{
	struct patricia_node *left;
	struct patricia_node *right;
	int bit;
	int is_end;
	rule_link_t *rules[RULE_PROTO_NUM];
	uint8_t *key;
	uint8_t key_len;
	int prefix_bits;
} patricia_node_t;

typedef struct
{
	patricia_node_t *v4_root;
	patricia_node_t *v6_root;
} patricia_table_t;

typedef struct
{
	int af;
	int bit_len;
	uint8_t key_bin[16];
} patricia_insert_meta_t;

/* ─── 内存分配钩子与位测试宏 ─── */
extern void *(*patricia_alloc)(size_t);
extern void (*patricia_free)(void *);

#define BIT_TEST(addr, bit) \
	((((const uint8_t *)(addr))[(bit) >> 3] >> (7 - ((bit) & 7))) & 1)

static inline int proto_to_idx(uint8_t proto)
{
	if (proto == IPPROTO_ICMP || proto == IPPROTO_ICMPV6)
		return 0;
	if (proto == IPPROTO_TCP)
		return 1;
	if (proto == IPPROTO_UDP)
		return 2;
	return -1;
}

/* ─── 核心核心 API 声明 ─── */
void patricia_init_table(patricia_table_t *table);
void patricia_destroy(patricia_node_t *root);

int patricia_insert(patricia_node_t **root, const void *key, int bit_len, rule_t *rule, int proto_idx);
rule_link_t **patricia_search(patricia_node_t *root, const void *key, int bit_len);
rule_link_t **patricia_lpm(patricia_node_t *root, const void *key, int bit_len);
int patricia_delete(patricia_node_t **root, const void *key, int bit_len, int proto_idx, rule_t *rule);

rule_t *acl_lookup(patricia_table_t *table, const void *addr, int af, uint8_t proto, uint16_t port);
int acl_add_rule_string(patricia_table_t *table, const char *cidr_str, rule_t *rule);
int acl_add_rule_universal(patricia_table_t *table, const char *input_str, rule_t *rule);

int patricia_insert_range_v4(patricia_node_t **root, uint32_t start_host, uint32_t end_host, rule_t *rule, int proto_idx);
int patricia_insert_range_v6(patricia_node_t **root, const uint8_t *start_net, const uint8_t *end_net, rule_t *rule, int proto_idx);

#endif /* PATRICIA_H */