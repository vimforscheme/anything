#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "patricia.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

static rule_t *rule_new(uint8_t proto, uint16_t port_lo, uint16_t port_hi)
{
	rule_t *r = malloc(sizeof(*r));
	r->proto = proto;
	r->port_lo = port_lo;
	r->port_hi = port_hi;
	return r;
}

static int check(int cond, const char *msg)
{
	printf("  %s: %s\n", cond ? "PASS" : "FAIL", msg);
	if (!cond)
		exit(1);
	return cond;
}

// 编写一个跨平台安全的 IPv4 字符串转二进制辅助函数
uint32_t parse_ipv4(const char *ip_str)
{
	struct in_addr addr;
	// 使用 inet_pton 替代 inet_addr
	if (inet_pton(AF_INET, ip_str, &addr) <= 0)
	{
		fprintf(stderr, "Error: Invalid IPv4 address format: %s\n", ip_str);
		return 0; // 或者根据业务返回错误码
	}
	return addr.s_addr; // 返回的就是标准网络字节序二进制
}

int main(void)
{
	patricia_table_t table;
	uint32_t ip4;

	patricia_init_table(&table);
	memset(&ip4, 0, sizeof(ip4));

	printf("=== Test 1: empty tree, insert first rule ===\n");
	ip4 = inet_addr("192.168.3.0");
	rule_t *r1 = rule_new(IPPROTO_TCP, 8000, 9000);
	check(patricia_insert(&table.v4_root, &ip4, 24, r1, 1) == 0,
		  "insert /24");
	check(table.v4_root != NULL && table.v4_root->is_end,
		  "root exists and is_end");

	printf("\n=== Test 2: exact match (same prefix, second proto) ===\n");
	rule_t *r2 = rule_new(IPPROTO_UDP, 53, 53);
	check(patricia_insert(&table.v4_root, &ip4, 24, r2, 2) == 0,
		  "insert UDP on same /24");
	check(table.v4_root->rules[1] == r1, "TCP rule slot preserved");
	check(table.v4_root->rules[2] == r2, "UDP rule slot added");

	printf("\n=== Test 3: fork insert (longer prefix) ===\n");
	ip4 = inet_addr("192.168.3.0");
	rule_t *r3 = rule_new(IPPROTO_TCP, 9000, 10000);
	check(patricia_insert(&table.v4_root, &ip4, 32, r3, 1) == 0,
		  "insert /32 fork");
	check(table.v4_root != NULL && table.v4_root->is_end == 1 && table.v4_root->right != NULL,
		  "root /24 retained, /32 leaf hung as child");

	printf("\n=== Test 4: ancestor insert (shorter prefix replaces root) ===\n");
	ip4 = inet_addr("192.168.0.0");
	rule_t *r4 = rule_new(IPPROTO_TCP, 1, 65535);
	check(patricia_insert(&table.v4_root, &ip4, 16, r4, 1) == 0,
		  "insert /16 ancestor");
	check(table.v4_root->rules[1] == r4, "/16 is now root");

	printf("\n=== Test 5: LPM - /16 and /24 and /32 ===\n");
	ip4 = inet_addr("192.168.3.5");
	{
		rule_t **rules = patricia_lpm(table.v4_root, &ip4, 32);
		check(rules != NULL, "LPM found rules");
		check(rules[1] != NULL, "TCP rule exists");
		check(rules[1]->port_lo == 8000 && rules[1]->port_hi == 9000,
			  "LPM for 192.168.3.5 returned /24 (not /32 or /16)");
	}
	/* 192.168.3.0/32 should match /32 */
	ip4 = inet_addr("192.168.3.0");
	{
		rule_t **rules = patricia_lpm(table.v4_root, &ip4, 32);
		check(rules != NULL && rules[1] != NULL,
			  "192.168.3.0 matches /32");
	}

	printf("\n=== Test 6: LPM - ICMP rule ===\n");
	ip4 = inet_addr("10.0.0.0");
	rule_t *r5 = rule_new(IPPROTO_ICMP, 0, 0);
	check(patricia_insert(&table.v4_root, &ip4, 8, r5, 0) == 0,
		  "insert /8 ICMP");
	ip4 = inet_addr("10.1.2.3");
	{
		rule_t **rules = patricia_lpm(table.v4_root, &ip4, 32);
		check(rules != NULL && rules[0] != NULL,
			  "ICMP rule found at /8");
	}

	printf("\n=== Test 7: delete /32, verify merge ===\n");
	ip4 = inet_addr("192.168.3.0");
	check(patricia_delete(&table.v4_root, &ip4, 32, 1) == 0,
		  "delete /32 TCP");
	/* Now /32 node should be merged away */
	ip4 = inet_addr("192.168.3.5");
	{
		rule_t **rules = patricia_lpm(table.v4_root, &ip4, 32);
		check(rules != NULL && rules[1] != NULL,
			  "192.168.3.5 now matches /24 after /32 removed");
	}

	printf("\n=== Test 8: is_end prevents merge ===\n");
	{
		uint32_t a16 = inet_addr("192.168.0.0");
		uint32_t a24 = inet_addr("192.168.3.0");
		rule_t *r6 = rule_new(IPPROTO_UDP, 123, 123);
		check(patricia_insert(&table.v4_root, &a16, 16, r6, 2) == 0,
			  "add UDP to /16");
		check(patricia_delete(&table.v4_root, &a24, 24, 1) == 0,
			  "delete /24 TCP (has is_end parent → no merge)");
		free(r6);
	}

	printf("\n=== Test 9: delete last rule, tree empty ===\n");
	{
		uint32_t a10 = inet_addr("10.0.0.0");
		uint32_t a192_16 = inet_addr("192.168.0.0");
		patricia_delete(&table.v4_root, &a10, 8, 0);	  /* ICMP */
		patricia_delete(&table.v4_root, &a192_16, 16, 1); /* TCP /16 */
		patricia_delete(&table.v4_root, &a192_16, 16, 2); /* UDP /16 */
	}

	printf("\n=== Test 10: IPv6 /64 insert and LPM ===\n");
	{
		uint8_t v6addr[16];
		memset(v6addr, 0, sizeof(v6addr));
		v6addr[0] = 0x20;
		v6addr[1] = 0x01; /* 2001::/16 prefix */
		v6addr[2] = 0x0d;
		v6addr[3] = 0xb8; /* 2001:db8::/32 */

		rule_t *r7 = rule_new(IPPROTO_TCP, 443, 443);
		check(patricia_insert(&table.v6_root, v6addr, 64, r7, 1) == 0,
			  "insert IPv6 /64");
		check(table.v6_root != NULL,
			  "v6 root exists");
		check(table.v6_root->bit == 63,
			  "v6 node bit = 63 (single node, 0-62 compressed)");
		/* LPM */
		{
			rule_t **rules = patricia_lpm(table.v6_root,
										  v6addr, 128);
			check(rules != NULL && rules[1] == r7,
				  "v6 LPM finds /64 rule");
		}
		free(r7);
	}

	printf("\n=== Test 11: ACL lookup with port filter ===\n");
	ip4 = inet_addr("192.168.3.0");
	rule_t *r8 = rule_new(IPPROTO_TCP, 8000, 9000);
	patricia_insert(&table.v4_root, &ip4, 24, r8, 1);
	ip4 = inet_addr("192.168.3.5");
	check(acl_lookup(&table, &ip4, AF_INET, IPPROTO_TCP, 8500) == r8,
		  "port 8500 in range → hit");
	check(acl_lookup(&table, &ip4, AF_INET, IPPROTO_TCP, 7000) == NULL,
		  "port 7000 out of range → miss");

	printf("\n=== Test 12: ICMP skips port check ===\n");
	ip4 = inet_addr("10.0.0.0");
	rule_t *r9 = rule_new(IPPROTO_ICMP, 0, 0);
	patricia_insert(&table.v4_root, &ip4, 8, r9, 0);
	ip4 = inet_addr("10.1.2.3");
	check(acl_lookup(&table, &ip4, AF_INET, IPPROTO_ICMP, 999) == r9,
		  "ICMP port=999 ignored → hit");

	printf("\n=== Cleanup ===\n");
	patricia_destroy(table.v4_root);
	patricia_destroy(table.v6_root);

	printf("All 12 tests complete.\n");
	free(r1);
	free(r2);
	free(r3);
	free(r4);
	free(r5);
	free(r8);
	free(r9);
	return 0;
}
