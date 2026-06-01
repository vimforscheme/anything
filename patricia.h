#pragma once
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
#include <netinet/in.h> /* 包含 IPPROTO_ICMP, IPPROTO_ICMPV6 */
#include <arpa/inet.h>	/* 包含 inet_ntop, inet_pton */
#endif

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>

#define RULE_PROTO_NUM 3 /* ICMP=0, TCP=1, UDP=2 */

typedef struct
{
	uint8_t proto;
	uint16_t port_lo;
	uint16_t port_hi;
} rule_t;

typedef struct patricia_node
{
	struct patricia_node *left;
	struct patricia_node *right;
	int bit;
	int is_end;
	rule_t *rules[RULE_PROTO_NUM];
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
	int af;				 /* 地址族：AF_INET 或 AF_INET6 */
	int bit_len;		 /* 掩码位长：v4范围[0,32], v6范围[0,128] */
	uint8_t key_bin[16]; /* 转换后的网络字节序二进制数组（v4用前4字节，v6用满16字节） */
} patricia_insert_meta_t;

int patricia_insert_range_v4(patricia_node_t **root,
							 uint32_t start_host, uint32_t end_host,
							 rule_t *rule, int proto_idx);

int patricia_insert_range_v6(patricia_node_t **root,
							 const uint8_t *start_net, const uint8_t *end_net,
							 rule_t *rule, int proto_idx);

/* ─── Custom allocator hooks ───────────────────────────────── */

extern void *(*patricia_alloc)(size_t);
extern void (*patricia_free)(void *);

/* ─── Bit test ──────────────────────────────────────────────── */

#define BIT_TEST(addr, bit) \
	((((const uint8_t *)(addr))[(bit) >> 3] >> (7 - ((bit) & 7))) & 1)

/* ─── Protocol index ───────────────────────────────────────── */

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

/* ─── API ──────────────────────────────────────────────────── */

void patricia_init_table(patricia_table_t *table);
void patricia_destroy(patricia_node_t *root);

int patricia_insert(patricia_node_t **root,
					const void *key, int bit_len,
					rule_t *rule, int proto_idx);

rule_t **patricia_search(patricia_node_t *root,
						 const void *key, int bit_len);

rule_t **patricia_lpm(patricia_node_t *root,
					  const void *key, int bit_len);

int patricia_delete(patricia_node_t **root,
					const void *key, int bit_len,
					int proto_idx);

rule_t *acl_lookup(patricia_table_t *table,
				   const void *addr, int af,
				   uint8_t proto, uint16_t port);

#endif /* PATRICIA_H */