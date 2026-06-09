#include "patricia.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void *(*patricia_alloc)(size_t) = malloc;
void (*patricia_free)(void *) = free;
rule_l3vpn_t g_l3vpn_rules = {0};
patricia_table_t g_ip_table = {0};
static patricia_node_t *alloc_node(int bit)
{
    patricia_node_t *node = patricia_alloc(sizeof(*node));
    if (node == NULL)
        return NULL;
    memset(node, 0, sizeof(*node));
    node->bit = bit;
    return node;
}

static inline int is_rule_duplicate(rule_link_t *head, rule_t *rule)
{
    while (head != NULL)
    {
        if (head->rule != NULL)
        {
            if (head->rule->port_lo == rule->port_lo &&
                head->rule->port_hi == rule->port_hi)
            {
                return 1;
            }
        }
        head = head->next;
    }
    return 0;
}

static patricia_node_t *make_leaf(const void *key, int bit_len, rule_t *rule, int proto_idx)
{
    int key_bytes = (bit_len + 7) / 8;
    patricia_node_t *node = alloc_node(bit_len - 1);
    if (node == NULL)
        return NULL;

    rule_link_t *link = patricia_alloc(sizeof(*link));
    if (link == NULL)
    {
        patricia_free(node);
        return NULL;
    }
    link->rule = rule;
    link->next = NULL;

    node->key = patricia_alloc(key_bytes);
    if (node->key == NULL)
    {
        patricia_free(link);
        patricia_free(node);
        return NULL;
    }
    memcpy(node->key, key, key_bytes);
    node->key_len = key_bytes;
    node->prefix_bits = bit_len;
    node->is_end = 1;
    node->rules[proto_idx] = link;
    return node;
}

static int prefix_match(const void *key, int key_bits, const uint8_t *prefix, int prefix_bits)
{
    int nbits = (key_bits < prefix_bits) ? key_bits : prefix_bits;
    int nbytes = nbits / 8;
    int rem = nbits % 8;

    if (nbytes > 0 && memcmp(key, prefix, nbytes) != 0)
        return 0;
    if (rem > 0)
    {
        uint8_t mask = (uint8_t)(0xFF << (8 - rem));
        if ((((const uint8_t *)key)[nbytes] & mask) != (prefix[nbytes] & mask))
            return 0;
    }
    return 1;
}

static int find_first_diff_bit(const uint8_t *a, int a_bits, const uint8_t *b, int b_bits)
{
    int max = (a_bits < b_bits) ? a_bits : b_bits;
    for (int i = 0; i < max; i++)
    {
        if (BIT_TEST(a, i) != BIT_TEST(b, i))
            return i;
    }
    return max;
}

/* 核心优化：改用显式栈执行非递归迭代搜索，彻底杜绝 IPv6 深树下的栈溢出隐患 */
static patricia_node_t *find_any_is_end(patricia_node_t *node)
{
    if (node == NULL)
        return NULL;
    patricia_node_t *stack[PATRICIA_MAX_DEPTH * 2];
    int top = 0;
    stack[top++] = node;

    while (top > 0)
    {
        patricia_node_t *curr = stack[--top];
        if (curr->is_end)
            return curr;
        if (curr->right != NULL)
            stack[top++] = curr->right;
        if (curr->left != NULL)
            stack[top++] = curr->left;
    }
    return NULL;
}

void patricia_init_table(patricia_table_t *table) { memset(table, 0, sizeof(*table)); }

void patricia_destroy(patricia_node_t *root)
{
    if (root == NULL)
        return;
    patricia_destroy(root->left);
    patricia_destroy(root->right);

    for (int i = 0; i < RULE_PROTO_NUM; i++)
    {
        rule_link_t *link = root->rules[i];
        while (link != NULL)
        {
            rule_link_t *next_link = link->next;
            patricia_free(link);
            link = next_link;
        }
    }

    if (root->key != NULL)
        patricia_free(root->key);
    patricia_free(root);
}

int patricia_insert(patricia_node_t **root, const void *key, int bit_len, rule_t *rule, int proto_idx)
{
    if (root == NULL || key == NULL || bit_len <= 0 || bit_len > 128)
        return -1;
    if (proto_idx < 0 || proto_idx >= RULE_PROTO_NUM)
        return -1;

    if (*root == NULL)
    {
        *root = make_leaf(key, bit_len, rule, proto_idx);
        return (*root != NULL) ? 0 : -1;
    }

    patricia_node_t *node = *root;
    while (node->left != NULL || node->right != NULL)
    {
        int b = BIT_TEST(key, node->bit);
        if (b == 0)
        {
            if (node->left == NULL)
                break;
            node = node->left;
        }
        else
        {
            if (node->right == NULL)
                break;
            node = node->right;
        }
    }

    patricia_node_t *closest = node->is_end ? node : find_any_is_end(node);
    if (closest == NULL)
        closest = find_any_is_end(*root);

    int diff_bit = find_first_diff_bit(key, bit_len, closest->key, closest->prefix_bits);

    /* 路径 1：精确匹配去重并执行头插法 */
    if (diff_bit >= bit_len && diff_bit >= closest->prefix_bits)
    {
        if (is_rule_duplicate(closest->rules[proto_idx], rule))
            return 0;
        rule_link_t *link = patricia_alloc(sizeof(*link));
        if (link == NULL)
            return -1;
        link->rule = rule;
        link->next = closest->rules[proto_idx];
        closest->rules[proto_idx] = link;
        closest->is_end = 1;
        return 0;
    }

    int insert_bit = (diff_bit >= bit_len) ? (bit_len - 1) : diff_bit;

    /* 路径 2：根节点升级去重并执行头插法 */
    if ((*root)->bit == insert_bit && diff_bit >= bit_len)
    {
        if (is_rule_duplicate((*root)->rules[proto_idx], rule))
            return 0;
        if (!(*root)->is_end)
        {
            int key_bytes = (bit_len + 7) / 8;
            (*root)->key = patricia_alloc(key_bytes);
            if ((*root)->key == NULL)
                return -1;
            memcpy((*root)->key, key, key_bytes);
            (*root)->key_len = key_bytes;
            (*root)->prefix_bits = bit_len;
        }
        rule_link_t *link = patricia_alloc(sizeof(*link));
        if (link == NULL)
            return -1;
        link->rule = rule;
        link->next = (*root)->rules[proto_idx];
        (*root)->rules[proto_idx] = link;
        (*root)->is_end = 1;
        return 0;
    }

    patricia_node_t *p = *root;
    while (p->bit < insert_bit)
    {
        int b = BIT_TEST(key, p->bit);
        patricia_node_t *next = (b == 0) ? p->left : p->right;
        if (next == NULL || next->bit >= insert_bit)
            break;
        p = next;
    }

    int b_p = BIT_TEST(key, p->bit);
    patricia_node_t *child = (b_p == 0) ? p->left : p->right;

    /* 路径 3：子节点升级去重并执行头插法 */
    if (child != NULL && child->bit == insert_bit && diff_bit >= bit_len)
    {
        if (is_rule_duplicate(child->rules[proto_idx], rule))
            return 0;
        if (!child->is_end)
        {
            int key_bytes = (bit_len + 7) / 8;
            child->key = patricia_alloc(key_bytes);
            if (child->key == NULL)
                return -1;
            memcpy(child->key, key, key_bytes);
            child->key_len = key_bytes;
            child->prefix_bits = bit_len;
        }
        rule_link_t *link = patricia_alloc(sizeof(*link));
        if (link == NULL)
            return -1;
        link->rule = rule;
        link->next = child->rules[proto_idx];
        child->rules[proto_idx] = link;
        child->is_end = 1;
        return 0;
    }

    patricia_node_t *leaf = make_leaf(key, bit_len, rule, proto_idx);
    if (leaf == NULL)
        return -1;

    if ((*root)->bit > insert_bit)
    {
        int b_exist = BIT_TEST(closest->key, insert_bit);
        if (b_exist == 0)
            leaf->left = *root;
        else
            leaf->right = *root;
        leaf->bit = insert_bit;
        *root = leaf;
        return 0;
    }

    if (diff_bit >= bit_len)
    {
        int b_exist = BIT_TEST(closest->key, insert_bit);
        if (b_exist == 0)
            leaf->left = child;
        else
            leaf->right = child;
        leaf->bit = insert_bit;
        if (b_p == 0)
            p->left = leaf;
        else
            p->right = leaf;
    }
    else
    {
        int b_new = BIT_TEST(key, diff_bit);
        if (child != NULL && child->bit == diff_bit)
        {
            if (b_new == 0)
                child->left = leaf;
            else
                child->right = leaf;
            return 0;
        }

        patricia_node_t *new_internal = alloc_node(diff_bit);
        if (new_internal == NULL)
        {
            patricia_destroy(leaf);
            return -1;
        }
        new_internal->left = (b_new == 0) ? leaf : child;
        new_internal->right = (b_new == 1) ? leaf : child;

        if (b_p == 0)
            p->left = new_internal;
        else
            p->right = new_internal;
    }
    return 0;
}

/* 优化：精确匹配具备唯一性，命中后立即提前返回，避免冗余下钻 */
rule_link_t **patricia_search(patricia_node_t *root, const void *key, int bit_len)
{
    patricia_node_t *node = root;
    while (node != NULL && node->bit < bit_len)
    {
        if (node->is_end && node->prefix_bits == bit_len && memcmp(node->key, key, node->key_len) == 0)
        {
            return node->rules;
        }
        node = (BIT_TEST(key, node->bit) == 0) ? node->left : node->right;
    }
    return NULL;
}

rule_link_t **patricia_lpm(patricia_node_t *root, const void *key, int bit_len)
{
    patricia_node_t *node = root;
    rule_link_t **best = NULL;

    while (node != NULL && node->bit < bit_len)
    {
        if (node->is_end && prefix_match(key, bit_len, node->key, node->prefix_bits))
        {
            best = node->rules;
        }
        node = (BIT_TEST(key, node->bit) == 0) ? node->left : node->right;
    }
    return best;
}

/* 优化：引入刚性运行期防御拦截，完美防止路径深度突破边界 */
int patricia_delete(patricia_node_t **root, const void *key, int bit_len, int proto_idx, rule_t *rule)
{
    patricia_node_t *path[PATRICIA_MAX_DEPTH * 2], *target = NULL, *node = *root;
    int dirs[PATRICIA_MAX_DEPTH * 2], depth = 0, target_depth = -1;

    if (root == NULL || *root == NULL || key == NULL || rule == NULL)
        return -1;

    while (node != NULL && node->bit < bit_len)
    {
        if (depth >= PATRICIA_MAX_DEPTH * 2)
            return -1; /* 刚性核心安全防护 */
        if (node->is_end && node->prefix_bits == bit_len && memcmp(node->key, key, node->key_len) == 0)
        {
            target = node;
            target_depth = depth;
        }
        path[depth] = node;
        int b = BIT_TEST(key, node->bit);
        dirs[depth] = b;
        depth++;
        node = (b == 0) ? node->left : node->right;
    }

    if (target == NULL)
        return -1;

    rule_link_t **pp = &target->rules[proto_idx];
    int found = 0;
    while (*pp != NULL)
    {
        if ((*pp)->rule == rule)
        {
            rule_link_t *to_free = *pp;
            *pp = (*pp)->next;
            patricia_free(to_free);
            found = 1;
            break;
        }
        pp = &(*pp)->next;
    }

    if (!found)
        return -1;

    for (int i = 0; i < RULE_PROTO_NUM; i++)
    {
        if (target->rules[i] != NULL)
            return 0;
    }

    target->is_end = 0;
    patricia_free(target->key);
    target->key = NULL;

    if ((target->left != NULL) != (target->right != NULL))
    {
        patricia_node_t *child = (target->left != NULL) ? target->left : target->right;
        if (target_depth == 0)
            *root = child;
        else if (dirs[target_depth - 1] == 0)
            path[target_depth - 1]->left = child;
        else
            path[target_depth - 1]->right = child;
        patricia_free(target);
        if (target_depth >= 0)
            path[target_depth] = NULL;
    }
    else if (target->left == NULL && target->right == NULL)
    {
        if (target_depth == 0)
            *root = NULL;
        else if (dirs[target_depth - 1] == 0)
            path[target_depth - 1]->left = NULL;
        else
            path[target_depth - 1]->right = NULL;
        patricia_free(target);
        if (target_depth >= 0)
            path[target_depth] = NULL;
    }

    for (int i = target_depth - 1; i >= 0; i--)
    {
        patricia_node_t *p = path[i];
        if (p->is_end || (p->left != NULL && p->right != NULL))
            break;
        patricia_node_t *child = (p->left != NULL) ? p->left : p->right;
        patricia_node_t *gp = (i > 0) ? path[i - 1] : NULL;
        if (gp == NULL)
            *root = child;
        else if (dirs[i - 1] == 0)
            gp->left = child;
        else
            gp->right = child;
        patricia_free(p);
        path[i] = NULL;
    }
    return 0;
}

rule_t *acl_lookup(patricia_table_t *table, const void *addr, int af, uint8_t proto, uint16_t port)
{
    if (table == NULL || addr == NULL)
        return NULL;

    patricia_node_t *node = (af == AF_INET6) ? table->v6_root : table->v4_root;
    int bit_len = (af == AF_INET6) ? 128 : 32;
    int proto_idx = proto_to_idx(proto);
    if (proto_idx < 0)
        return NULL;

    while (node != NULL && node->bit < bit_len)
    {
        if (node->is_end && prefix_match(addr, bit_len, node->key, node->prefix_bits))
        {
            rule_link_t *curr_link = node->rules[proto_idx];
            while (curr_link != NULL)
            {
                rule_t *curr_rule = curr_link->rule;
                if (curr_rule != NULL)
                {
                    if (proto == IPPROTO_ICMP || proto == IPPROTO_ICMPV6)
                        return curr_rule;
                    if (port >= curr_rule->port_lo && port <= curr_rule->port_hi)
                        return curr_rule;
                }
                curr_link = curr_link->next;
            }
        }
        node = (BIT_TEST(addr, node->bit) == 0) ? node->left : node->right;
    }
    return NULL;
}

rule_t *acl_lookup_lpm(patricia_table_t *table, const void *addr, int af, uint8_t proto, uint16_t port)
{
    if (table == NULL || addr == NULL)
        return NULL;

    /* 1. 直接利用原有的 patricia_lpm 顺着压缩路径瞬间抓出最精确的那个节点的 rules 槽位阵列 */
    patricia_node_t *root = (af == AF_INET6) ? table->v6_root : table->v4_root;
    int bit_len = (af == AF_INET6) ? 128 : 32;

    rule_link_t **best_rules = patricia_lpm(root, addr, bit_len);
    if (best_rules == NULL)
        return NULL; /* 连任何基础网络前缀都没撞上，直接拒绝 */

    int proto_idx = proto_to_idx(proto);
    if (proto_idx < 0)
        return NULL;

    /* 2. 单一匹配处理：有且仅对这一个最合适节点内部的横向链表执行区间测试 */
    rule_link_t *curr_link = best_rules[proto_idx];
    while (curr_link != NULL)
    {
        rule_t *curr_rule = curr_link->rule;
        if (curr_rule != NULL)
        {
            /* ICMP 协议无端口概念，直接放行 */
            if (proto == IPPROTO_ICMP || proto == IPPROTO_ICMPV6)
            {
                return curr_rule;
            }
            /* 干净利落的通用区间盲测 */
            if (port >= curr_rule->port_lo && port <= curr_rule->port_hi)
            {
                return curr_rule;
            }
        }
        curr_link = curr_link->next;
    }

    /* 核心差异：如果该最精确节点下的端口规则没对上，直接返回 NULL 拦截！
     * 坚决不向路径上任何外层的、宏观的大前缀网段妥协，实现局部绝对特判 */
    return NULL;
}

static void apply_cidr_mask(uint8_t *key, int bit_len, int max_bytes)
{
    int bytes = bit_len / 8;
    int rem = bit_len % 8;

    if (bytes < max_bytes)
    {
        if (rem > 0)
        {
            uint8_t mask = (uint8_t)(0xFF << (8 - rem));
            key[bytes] &= mask;
            bytes++;
        }
        while (bytes < max_bytes)
            key[bytes++] = 0;
    }
}

static int patricia_parse_cidr(const char *cidr_str, patricia_insert_meta_t *out_meta)
{
    if (cidr_str == NULL || out_meta == NULL)
        return -1;

    char buf[128];
    strncpy(buf, cidr_str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *slash = strchr(buf, '/');
    int explicit_bits = -1;
    if (slash != NULL)
    {
        *slash = '\0';
        explicit_bits = atoi(slash + 1);
    }

    memset(out_meta, 0, sizeof(*out_meta));

    struct in_addr v4addr;
    if (inet_pton(AF_INET, buf, &v4addr) == 1)
    {
        out_meta->af = AF_INET;
        out_meta->bit_len = (explicit_bits >= 0) ? explicit_bits : 32;
        if (out_meta->bit_len < 0 || out_meta->bit_len > 32)
            return -1;

        memcpy(out_meta->key_bin, &v4addr.s_addr, 4);
        apply_cidr_mask(out_meta->key_bin, out_meta->bit_len, 4);
        return 0;
    }

    struct in6_addr v6addr;
    if (inet_pton(AF_INET6, buf, &v6addr) == 1)
    {
        out_meta->af = AF_INET6;
        out_meta->bit_len = (explicit_bits >= 0) ? explicit_bits : 128;
        if (out_meta->bit_len < 0 || out_meta->bit_len > 128)
            return -1;

        memcpy(out_meta->key_bin, &v6addr, 16);
        apply_cidr_mask(out_meta->key_bin, out_meta->bit_len, 16);
        return 0;
    }
    return -1;
}

int acl_add_rule_string(patricia_table_t *table, const char *cidr_str, rule_t *rule)
{
    patricia_insert_meta_t meta;
    if (patricia_parse_cidr(cidr_str, &meta) != 0)
        return -1;

    int proto_idx = proto_to_idx(rule->proto);
    if (proto_idx < 0)
        return -1;

    if (meta.af == AF_INET)
        return patricia_insert(&table->v4_root, meta.key_bin, meta.bit_len, rule, proto_idx);
    else if (meta.af == AF_INET6)
        return patricia_insert(&table->v6_root, meta.key_bin, meta.bit_len, rule, proto_idx);
    return -1;
}

int patricia_insert_range_v4(patricia_node_t **root, uint32_t start_host, uint32_t end_host, rule_t *rule, int proto_idx)
{
    if (root == NULL || start_host > end_host)
        return -1;

    while (start_host <= end_host)
    {
        uint64_t span = (uint64_t)end_host - start_host + 1;
        uint64_t block64 = start_host ? (start_host & (-start_host)) : 0x100000000ULL;

        if (block64 > span)
        {
            block64 = 1;
            while (block64 <= span / 2)
                block64 <<= 1;
        }

        int shift = 0;
        uint64_t temp = block64;
        while (temp > 1)
        {
            temp >>= 1;
            shift++;
        }
        int bit_len = 32 - shift;

        uint32_t ip_net = htonl(start_host);
        if (patricia_insert(root, &ip_net, bit_len, rule, proto_idx) != 0)
            return -1;

        if (span == block64)
            break;
        start_host += (uint32_t)block64;
    }
    return 0;
}

static inline int compare_128(uint64_t a_hi, uint64_t a_lo, uint64_t b_hi, uint64_t b_lo)
{
    if (a_hi != b_hi)
        return (a_hi > b_hi) ? 1 : -1;
    if (a_lo != b_lo)
        return (a_lo > b_lo) ? 1 : -1;
    return 0;
}

int patricia_insert_range_v6(patricia_node_t **root, const uint8_t *start_net, const uint8_t *end_net, rule_t *rule, int proto_idx)
{
    if (root == NULL || start_net == NULL || end_net == NULL)
        return -1;

    uint64_t sh = 0, sl = 0, eh = 0, el = 0;
    for (int i = 0; i < 8; i++)
    {
        sh = (sh << 8) | start_net[i];
        sl = (sl << 8) | start_net[i + 8];
        eh = (eh << 8) | end_net[i];
        el = (el << 8) | end_net[i + 8];
    }

    if (compare_128(sh, sl, eh, el) > 0)
        return -1;

    while (compare_128(sh, sl, eh, el) <= 0)
    {
        uint64_t diff_h = eh - sh;
        uint64_t diff_l = el - sl;
        if (el < sl)
            diff_h--;

        uint64_t block_h = 0, block_lo = 0;
        if (sl != 0)
            block_lo = sl & (-sl);
        else if (sh != 0)
            block_h = sh & (-sh);

        uint64_t mask_h = 0, mask_l = 0;
        if (block_lo != 0)
        {
            mask_h = 0;
            mask_l = block_lo - 1;
        }
        else if (block_h != 0)
        {
            mask_h = block_h - 1;
            mask_l = 0xffffffffffffffffULL;
        }
        else
        {
            mask_h = 0xffffffffffffffffULL;
            mask_l = 0xffffffffffffffffULL;
        }

        if (compare_128(mask_h, mask_l, diff_h, diff_l) > 0)
        {
            mask_h = 0;
            mask_l = 0;
            while (1)
            {
                uint64_t next_h = (mask_h << 1) | (mask_l >> 63);
                uint64_t next_l = (mask_l << 1) | 1;
                if (compare_128(next_h, next_l, diff_h, diff_l) <= 0)
                {
                    mask_h = next_h;
                    mask_l = next_l;
                    if (mask_h == 0xffffffffffffffffULL && mask_l == 0xffffffffffffffffULL)
                        break;
                }
                else
                    break;
            }
            if (mask_l == 0xffffffffffffffffULL)
            {
                block_lo = 0;
                block_h = mask_h + 1;
            }
            else
            {
                block_lo = mask_l + 1;
                block_h = mask_h;
            }
        }

        int shift = 0;
        uint64_t th = block_h, tl = block_lo;
        while (compare_128(th, tl, 0, 1) > 0)
        {
            tl = (tl >> 1) | (th << 63);
            th >>= 1;
            shift++;
        }
        int bit_len = 128 - shift;

        uint8_t ip6_block[16];
        for (int i = 0; i < 8; i++)
        {
            ip6_block[i] = (uint8_t)(sh >> ((7 - i) * 8));
            ip6_block[i + 8] = (uint8_t)(sl >> ((7 - i) * 8));
        }

        if (patricia_insert(root, ip6_block, bit_len, rule, proto_idx) != 0)
            return -1;

        if (mask_h == diff_h && mask_l == diff_l)
            break;

        uint64_t prev_sl = sl;
        sl += block_lo;
        sh += block_h;
        if (sl < prev_sl)
            sh++;
    }
    return 0;
}

int acl_add_rule_universal(patricia_table_t *table, const char *input_str, rule_t *rule)
{
    if (table == NULL || input_str == NULL || rule == NULL)
        return -1;

    char *dash = strchr(input_str, '-');
    if (dash != NULL)
    {
        char start_ip[64], end_ip[64];
        size_t start_len = dash - input_str;

        if (start_len >= sizeof(start_ip) || strlen(dash + 1) >= sizeof(end_ip))
            return -1;

        strncpy(start_ip, input_str, start_len);
        start_ip[start_len] = '\0';

        /* 核心修正：统一替换为带有显式物理封底的安全函数，彻底隔绝 CWE-120 风险 */
        strncpy(end_ip, dash + 1, sizeof(end_ip) - 1);
        end_ip[sizeof(end_ip) - 1] = '\0';

        if (strchr(start_ip, ':') != NULL)
        {
            uint8_t s_net[16], e_net[16];
            if (inet_pton(AF_INET6, start_ip, s_net) != 1 || inet_pton(AF_INET6, end_ip, e_net) != 1)
                return -1;
            int proto_idx = proto_to_idx(rule->proto);
            return patricia_insert_range_v6(&table->v6_root, s_net, e_net, rule, proto_idx);
        }
        else
        {
            struct in_addr s_addr, e_addr;
            if (inet_pton(AF_INET, start_ip, &s_addr) != 1 || inet_pton(AF_INET, end_ip, &e_addr) != 1)
                return -1;
            int proto_idx = proto_to_idx(rule->proto);
            return patricia_insert_range_v4(&table->v4_root, ntohl(s_addr.s_addr), ntohl(e_addr.s_addr), rule, proto_idx);
        }
    }
    return acl_add_rule_string(table, input_str, rule);
}
void free_g_l3vpn_rules()
{
    if (g_l3vpn_rules.size > 0)
    {
        int i = 0;
        for (i = 0; i < g_l3vpn_rules.size; i++)
        {
            if (g_l3vpn_rules.rule[i])
            {
                free(g_l3vpn_rules.rule[i]);
                g_l3vpn_rules.rule[i] = NULL;
            }
        }
        free(g_l3vpn_rules.rule);
        g_l3vpn_rules.rule = NULL;
        g_l3vpn_rules.size = 0;
        g_l3vpn_rules.vaild_size = 0;
    }
}
void free_g_ip_table()
{
    patricia_destroy(g_ip_table.v4_root);
    patricia_destroy(g_ip_table.v6_root);
    patricia_init_table(&g_ip_table);
}
void free_g_rule_table()
{
    free_g_l3vpn_rules();
    free_g_ip_table();
}

rule_t *rule_new(uint8_t proto, uint16_t port_lo, uint16_t port_hi)
{
    rule_t *r = malloc(sizeof(*r));
    r->proto = proto;
    r->port_lo = port_lo;
    r->port_hi = port_hi;
    return r;
}

int insert_ip_rules(int af, unsigned char *start_ip, unsigned char *end_ip, rule_t *rule)
{
    if (rule->port_lo > rule->port_hi)
        return -1;
    if (af == AF_INET6)
    {
        // 这里只有第一个是 ip 第二个要转成int处理 当子网掩码,约定v6正常下发网段 +子网掩码 第二个46就是子网掩码
        uint8_t s_net[16];
        if (inet_pton(AF_INET6, start_ip, s_net) != 1)
            return -1;
        int mask = *(int *)(end_ip);
        int proto_idx = proto_to_idx(rule->proto);

        return patricia_insert(&g_ip_table.v6_root, s_net, 64, rule, proto_idx);
    }
    else if (af == AF_INET)
    {
        // 这里服务没办法给路由网段+子网掩码 所以这样转换处理
        struct in_addr s_addr, e_addr;
        if (inet_pton(AF_INET, start_ip, &s_addr) != 1 || inet_pton(AF_INET, end_ip, &e_addr) != 1)
            return -1;

        uint32_t s = ntohl(s_addr.s_addr);
        uint32_t e = ntohl(e_addr.s_addr);

        if (s > e)
            return -1;
        int proto_idx = proto_to_idx(rule->proto);
        return patricia_insert_range_v4(&g_ip_table.v4_root, s, e, rule, proto_idx);
    }
}