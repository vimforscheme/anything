#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "patricia_copy.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif

/* 声明外部高级特判查找接口 */
rule_t *acl_lookup_lpm(patricia_table_t *table, const void *addr, int af, uint8_t proto, uint16_t port);

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

int main(void)
{
	patricia_table_t table;
	uint32_t ip4;

	patricia_init_table(&table);
	memset(&ip4, 0, sizeof(ip4));

	printf("=== Test 1: empty tree, insert first rule ===\n");
	inet_pton(AF_INET, "192.168.3.0", &ip4);
	rule_t *r1 = rule_new(IPPROTO_TCP, 8000, 9000);
	check(patricia_insert(&table.v4_root, &ip4, 24, r1, 1) == 0,
		  "insert /24");
	check(table.v4_root != NULL && table.v4_root->is_end,
		  "root exists and is_end");

	printf("\n=== Test 2: horizontal union & deduplication defense ===\n");
	rule_t *r2 = rule_new(IPPROTO_UDP, 53, 53);
	check(patricia_insert(&table.v4_root, &ip4, 24, r2, 2) == 0,
		  "insert UDP on same /24 node");
	check(table.v4_root->rules[1] != NULL && table.v4_root->rules[1]->rule == r1, "TCP rule slot preserved");
	check(table.v4_root->rules[2] != NULL && table.v4_root->rules[2]->rule == r2, "UDP rule slot added");

	rule_t *r1_b = rule_new(IPPROTO_TCP, 4000, 5000);
	check(patricia_insert(&table.v4_root, &ip4, 24, r1_b, 1) == 0,
		  "insert second TCP rule (4000-5000) on same /24 node");
	check(table.v4_root->rules[1]->rule == r1_b, "head of chain is r1_b");
	check(table.v4_root->rules[1]->next != NULL && table.v4_root->rules[1]->next->rule == r1, "r1 linked behind");

	check(patricia_insert(&table.v4_root, &ip4, 24, r1_b, 1) == 0, "re-insert duplicate rule pointer");
	check(table.v4_root->rules[1]->next->next == NULL, "deduplication confirmed: chain length remains 2");

	printf("\n=== Test 3: fork insert (longer prefix) ===\n");
	inet_pton(AF_INET, "192.168.3.0", &ip4);
	rule_t *r3 = rule_new(IPPROTO_TCP, 9000, 10000);
	check(patricia_insert(&table.v4_root, &ip4, 32, r3, 1) == 0,
		  "insert /32 fork");
	check(table.v4_root != NULL && table.v4_root->is_end == 1 && table.v4_root->right != NULL,
		  "root /24 retained, /32 leaf hung as child");

	printf("\n=== Test 4: ancestor insert (shorter prefix replaces root) ===\n");
	inet_pton(AF_INET, "192.168.0.0", &ip4);
	rule_t *r4 = rule_new(IPPROTO_TCP, 1, 65535);
	check(patricia_insert(&table.v4_root, &ip4, 16, r4, 1) == 0,
		  "insert /16 ancestor");
	check(table.v4_root->rules[1] != NULL && table.v4_root->rules[1]->rule == r4, "/16 is now the new root");

	printf("\n=== Test 5: LPM - Return wrapper chain headers ===\n");
	inet_pton(AF_INET, "192.168.3.5", &ip4);
	{
		rule_link_t **res_links = patricia_lpm(table.v4_root, &ip4, 32);
		check(res_links != NULL, "LPM found rules");
		check(res_links[1] != NULL, "TCP rule link chain exists");
		check(res_links[1]->rule == r1_b, "LPM correctly hit 192.168.3.0/24 node");
	}
	inet_pton(AF_INET, "192.168.3.0", &ip4);
	{
		rule_link_t **res_links = patricia_lpm(table.v4_root, &ip4, 32);
		check(res_links != NULL && res_links[1] != NULL, "192.168.3.0 matches /32 node");
		check(res_links[1]->rule == r3, "Successfully hit /32 host rule r3");
	}

	printf("\n=== Test 6: LPM - ICMP rule ===\n");
	inet_pton(AF_INET, "10.0.0.0", &ip4);
	rule_t *r5 = rule_new(IPPROTO_ICMP, 0, 0);
	check(patricia_insert(&table.v4_root, &ip4, 8, r5, 0) == 0,
		  "insert /8 ICMP");
	inet_pton(AF_INET, "10.1.2.3", &ip4);
	{
		rule_link_t **res_links = patricia_lpm(table.v4_root, &ip4, 32);
		check(res_links != NULL && res_links[0] != NULL, "ICMP rule found at /8");
		check(res_links[0]->rule == r5, "ICMP rule matches r5");
	}

	printf("\n=== Test 7: delete /32 with explicit rule pointer ===\n");
	inet_pton(AF_INET, "192.168.3.0", &ip4);
	check(patricia_delete(&table.v4_root, &ip4, 32, 1, r3) == 0, "delete /32 TCP r3");
	inet_pton(AF_INET, "192.168.3.5", &ip4);
	{
		rule_link_t **res_links = patricia_lpm(table.v4_root, &ip4, 32);
		check(res_links != NULL && res_links[1] != NULL, "192.168.3.5 matches /24 after /32 host removed");
	}

	printf("\n=== Test 8: Partial chain clear prevents premature merge ===\n");
	rule_t *r6 = rule_new(IPPROTO_UDP, 123, 123);
	{
		uint32_t a16, a24;
		inet_pton(AF_INET, "192.168.0.0", &a16);
		inet_pton(AF_INET, "192.168.3.0", &a24);
		check(patricia_insert(&table.v4_root, &a16, 16, r6, 2) == 0, "add UDP to /16");
		check(patricia_delete(&table.v4_root, &a24, 24, 1, r1) == 0, "delete /24 TCP r1");
		check(table.v4_root != NULL, "node structure intact: r1_b and r2 protect from deletion");
		check(patricia_delete(&table.v4_root, &a24, 24, 1, r1_b) == 0, "delete /24 TCP r1_b");
	}

	printf("\n=== Test 9: Complete tear down, tree empty ===\n");
	{
		uint32_t a10, a192_16, a192_24;
		inet_pton(AF_INET, "10.0.0.0", &a10);
		inet_pton(AF_INET, "192.168.0.0", &a192_16);
		inet_pton(AF_INET, "192.168.3.0", &a192_24);

		check(patricia_delete(&table.v4_root, &a10, 8, 0, r5) == 0, "delete /8 ICMP r5");
		check(patricia_delete(&table.v4_root, &a192_16, 16, 1, r4) == 0, "delete /16 TCP r4");
		check(patricia_delete(&table.v4_root, &a192_16, 16, 2, r6) == 0, "delete /16 UDP r6");
		check(patricia_delete(&table.v4_root, &a192_24, 24, 2, r2) == 0, "delete /24 UDP r2");
		check(table.v4_root == NULL, "v4 tree is now completely empty and collapsed");
	}

	printf("\n=== Test 10: IPv6 /64 insert and LPM ===\n");
	{
		uint8_t v6addr[16];
		memset(v6addr, 0, sizeof(v6addr));
		v6addr[0] = 0x20;
		v6addr[1] = 0x01;
		v6addr[2] = 0x0d;
		v6addr[3] = 0xb8;

		rule_t *r7 = rule_new(IPPROTO_TCP, 443, 443);
		check(patricia_insert(&table.v6_root, v6addr, 64, r7, 1) == 0, "insert IPv6 /64");
		check(table.v6_root != NULL, "v6 root exists");
		check(table.v6_root->bit == 63, "v6 node bit compressed perfectly");
		{
			rule_link_t **res_links = patricia_lpm(table.v6_root, v6addr, 128);
			check(res_links != NULL && res_links[1] != NULL && res_links[1]->rule == r7, "v6 LPM finds /64 rule link");
		}

		/* 核心修补：在 free 之前，必须调用 delete 将资产从容器骨架中摘除，让树自然坍缩折叠 */
		check(patricia_delete(&table.v6_root, v6addr, 64, 1, r7) == 0, "delete /64 IPv6 r7");
		check(table.v6_root == NULL, "v6 tree is now completely empty and collapsed");

		free(r7); /* 树里没它了，此时才可以安全地回归外部账本释放物理资产 */
	}

	printf("\n=== Test 11: ACL lookup with horizontal chain interval match ===\n");
	inet_pton(AF_INET, "192.168.3.0", &ip4);
	rule_t *r8_high = rule_new(IPPROTO_TCP, 8000, 9000);
	rule_t *r8_low = rule_new(IPPROTO_TCP, 4000, 5000);

	patricia_insert(&table.v4_root, &ip4, 24, r8_high, 1);
	patricia_insert(&table.v4_root, &ip4, 24, r8_low, 1);

	inet_pton(AF_INET, "192.168.3.5", &ip4);
	check(acl_lookup(&table, &ip4, AF_INET, IPPROTO_TCP, 8500) == r8_high, "port 8500 hits r8_high -> hit");
	check(acl_lookup(&table, &ip4, AF_INET, IPPROTO_TCP, 4500) == r8_low, "port 4500 hits r8_low (horizontal chain skip) -> hit");
	check(acl_lookup(&table, &ip4, AF_INET, IPPROTO_TCP, 7000) == NULL, "port 7000 out of all ranges -> miss");

	printf("\n=== Test 12: ICMP skips port check ===\n");
	inet_pton(AF_INET, "10.0.0.0", &ip4);
	rule_t *r9 = rule_new(IPPROTO_ICMP, 0, 0);
	patricia_insert(&table.v4_root, &ip4, 8, r9, 0);
	inet_pton(AF_INET, "10.1.2.3", &ip4);
	check(acl_lookup(&table, &ip4, AF_INET, IPPROTO_ICMP, 999) == r9, "ICMP port=999 ignored -> hit");

	printf("\n=== Test 13: acl_lookup_lpm Exclusive Specific Match Proof ===\n");
	uint32_t ip4_broad, ip4_narrow, ip4_test;
	inet_pton(AF_INET, "192.168.0.0", &ip4_broad);
	inet_pton(AF_INET, "192.168.3.0", &ip4_narrow);
	inet_pton(AF_INET, "192.168.3.100", &ip4_test);

	rule_t *r_broad = rule_new(IPPROTO_TCP, 80, 80);
	rule_t *r_narrow = rule_new(IPPROTO_TCP, 443, 443);

	patricia_insert(&table.v4_root, &ip4_broad, 16, r_broad, 1);
	patricia_insert(&table.v4_root, &ip4_narrow, 24, r_narrow, 1);

	check(acl_lookup(&table, &ip4_test, AF_INET, IPPROTO_TCP, 80) == r_broad,
		  "acl_lookup [Broad Union]: Port 80 matches parent /16 -> HIT");
	check(acl_lookup_lpm(&table, &ip4_test, AF_INET, IPPROTO_TCP, 80) == NULL,
		  "acl_lookup_lpm [Strict One]: Port 80 on narrowest /24 is unmapped -> MISS (Blocked)");
	check(acl_lookup_lpm(&table, &ip4_test, AF_INET, IPPROTO_TCP, 443) == r_narrow,
		  "acl_lookup_lpm [Strict One]: Port 443 on narrowest /24 -> HIT");

	/* ─── 🛠️ 重点扩容一：Test 14 IPv4 任意不合规范围自动化切碎与批量合并测试 ─── */
	printf("\n=== Test 14: patricia_insert_range_v4 Split & Lookup Verification ===\n");
	/* 192.168.1.3 到 192.168.1.9 (主机序传入) */
	uint32_t r4_start = (192 << 24) | (168 << 16) | (1 << 8) | 3;
	uint32_t r4_end = (192 << 24) | (168 << 16) | (1 << 8) | 9;
	rule_t *r_range_v4 = rule_new(IPPROTO_TCP, 8080, 8080);

	check(patricia_insert_range_v4(&table.v4_root, r4_start, r4_end, r_range_v4, 1) == 0,
		  "insert arbitrary range v4 (192.168.1.3 - 192.168.1.9)");

	uint32_t ip_test_v4;
	/* 区间下限外部点测试 */
	inet_pton(AF_INET, "192.168.1.2", &ip_test_v4);
	check(acl_lookup(&table, &ip_test_v4, AF_INET, IPPROTO_TCP, 8080) == NULL, "192.168.1.2 (Below Range Boundary) -> MISS");

	/* 区间下限临界点测试 (/32 独立块) */
	inet_pton(AF_INET, "192.168.1.3", &ip_test_v4);
	check(acl_lookup(&table, &ip_test_v4, AF_INET, IPPROTO_TCP, 8080) == r_range_v4, "192.168.1.3 (Start Border Block) -> HIT");

	/* 区间内部隐藏跨度点测试 (/30 复合大块内部点) */
	inet_pton(AF_INET, "192.168.1.6", &ip_test_v4);
	check(acl_lookup(&table, &ip_test_v4, AF_INET, IPPROTO_TCP, 8080) == r_range_v4, "192.168.1.6 (Inside Compressed Block) -> HIT");

	/* 区间上限临界点测试 (/31 尾部块) */
	inet_pton(AF_INET, "192.168.1.9", &ip_test_v4);
	check(acl_lookup(&table, &ip_test_v4, AF_INET, IPPROTO_TCP, 8080) == r_range_v4, "192.168.1.9 (End Border Block) -> HIT");

	/* 区间上限外部点测试 */
	inet_pton(AF_INET, "192.168.1.10", &ip_test_v4);
	check(acl_lookup(&table, &ip_test_v4, AF_INET, IPPROTO_TCP, 8080) == NULL, "192.168.1.10 (Above Range Boundary) -> MISS");

	/* ─── 🛠️ 重点扩容二：Test 15 IPv6 128位大数范围自动化切碎与批量合并测试 ─── */
	printf("\n=== Test 15: patricia_insert_range_v6 Split & Lookup Verification ===\n");
	/* 2001:db8::3 到 2001:db8::9 (大端内存数组传入) */
	uint8_t r6_start[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x03};
	uint8_t r6_end[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x09};
	rule_t *r_range_v6 = rule_new(IPPROTO_TCP, 8443, 8443);

	check(patricia_insert_range_v6(&table.v6_root, r6_start, r6_end, r_range_v6, 1) == 0,
		  "insert arbitrary range v6 (2001:db8::3 - 2001:db8::9)");

	uint8_t ip_test_v6[16];
	/* IPv6 区间下限外部点测试 */
	inet_pton(AF_INET6, "2001:db8::2", ip_test_v6);
	check(acl_lookup(&table, ip_test_v6, AF_INET6, IPPROTO_TCP, 8443) == NULL, "2001:db8::2 (Below Range Boundary) -> MISS");

	/* IPv6 区间下限临界点测试 */
	inet_pton(AF_INET6, "2001:db8::3", ip_test_v6);
	check(acl_lookup(&table, ip_test_v6, AF_INET6, IPPROTO_TCP, 8443) == r_range_v6, "2001:db8::3 (Start Border Block) -> HIT");

	/* IPv6 区间内部隐藏跨度点测试 */
	inet_pton(AF_INET6, "2001:db8::6", ip_test_v6);
	check(acl_lookup(&table, ip_test_v6, AF_INET6, IPPROTO_TCP, 8443) == r_range_v6, "2001:db8::6 (Inside Compressed Block) -> HIT");

	/* IPv6 区间上限临界点测试 */
	inet_pton(AF_INET6, "2001:db8::9", ip_test_v6);
	check(acl_lookup(&table, ip_test_v6, AF_INET6, IPPROTO_TCP, 8443) == r_range_v6, "2001:db8::9 (End Border Block) -> HIT");

	/* IPv6 区间上限外部点测试 */
	inet_pton(AF_INET6, "2001:db8::a", ip_test_v6);
	check(acl_lookup(&table, ip_test_v6, AF_INET6, IPPROTO_TCP, 8443) == NULL, "2001:db8::a (Above Range Boundary) -> MISS");

	printf("\n=== Cleanup ===\n");
	patricia_destroy(table.v4_root);
	patricia_destroy(table.v6_root);

	printf("All 15 tests complete.\n");
	free(r1);
	free(r1_b);
	free(r2);
	free(r3);
	free(r4);
	free(r5);
	free(r6);
	free(r8_high);
	free(r8_low);
	free(r9);
	free(r_broad);
	free(r_narrow);
	free(r_range_v4);
	free(r_range_v6); /* 清洗释放新增区间测试的物理资产 */
	return 0;
}