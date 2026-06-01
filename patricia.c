#include "patricia.h"
#include <stdlib.h>
#include <string.h>

void *(*patricia_alloc)(size_t) = malloc;
void (*patricia_free)(void *) = free;

static patricia_node_t *alloc_node(int bit)
{
    patricia_node_t *node = patricia_alloc(sizeof(*node));
    if (node == NULL)
        return NULL;
    memset(node, 0, sizeof(*node));
    node->bit = bit;
    return node;
}

static patricia_node_t *make_leaf(const void *key, int bit_len, rule_t *rule, int proto_idx)
{
    int key_bytes = (bit_len + 7) / 8;
    patricia_node_t *node = alloc_node(bit_len - 1);
    if (node == NULL)
        return NULL;

    node->key = patricia_alloc(key_bytes);
    if (node->key == NULL)
    {
        patricia_free(node);
        return NULL;
    }
    memcpy(node->key, key, key_bytes);
    node->key_len = key_bytes;
    node->prefix_bits = bit_len; /* 精准记录位长，攻克任意位限制 */
    node->is_end = 1;
    node->rules[proto_idx] = rule;
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

static patricia_node_t *find_any_is_end(patricia_node_t *node)
{
    if (node == NULL)
        return NULL;
    if (node->is_end)
        return node;
    patricia_node_t *found = find_any_is_end(node->left);
    return (found != NULL) ? found : find_any_is_end(node->right);
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
        if (root->rules[i] != NULL)
            patricia_free(root->rules[i]);
    }
    if (root->key != NULL)
        patricia_free(root->key);
    patricia_free(root);
}

/* ─── 完美闭环的两阶段（Two-Pass）插入 ─── */
int patricia_insert(patricia_node_t **root, const void *key, int bit_len, rule_t *rule, int proto_idx)
{
    if (root == NULL || key == NULL || bit_len <= 0)
        return -1;
    if (proto_idx < 0 || proto_idx >= RULE_PROTO_NUM)
        return -1;

    if (*root == NULL)
    {
        *root = make_leaf(key, bit_len, rule, proto_idx);
        return (*root != NULL) ? 0 : -1;
    }

    /* Phase 1: 一沉到底，基于位决策找到拓扑上最邻近的节点 */
    patricia_node_t *node = *root;
    while (node->left != NULL || node->right != NULL)
    {
        int b = (node->bit < bit_len) ? BIT_TEST(key, node->bit) : 0;
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

    /* Phase 2: 比对最邻近节点，锁定真实分叉位 */
    int diff_bit = find_first_diff_bit(key, bit_len, closest->key, closest->prefix_bits);

    /* 场景 A: 精确命中路径上已有的规则节点 (原位覆盖或填充规则) */
    if (diff_bit >= bit_len && diff_bit >= closest->prefix_bits)
    {
        closest->is_end = 1;
        if (closest->rules[proto_idx] != NULL)
            patricia_free(closest->rules[proto_idx]);
        closest->rules[proto_idx] = rule;
        return 0;
    }

    int insert_bit = (diff_bit >= bit_len) ? (bit_len - 1) : diff_bit;

    /* ─── 补丁 1: 根节点恰好是我们要找的相同 bit 纯内部节点 -> 直接原地升级 ─── */
    if ((*root)->bit == insert_bit && diff_bit >= bit_len)
    {
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
        (*root)->is_end = 1;
        if ((*root)->rules[proto_idx] != NULL)
            patricia_free((*root)->rules[proto_idx]);
        (*root)->rules[proto_idx] = rule;
        return 0;
    }

    /* 场景 C: 沿途下钻，精确定位分裂点 */
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

    /* ─── 补丁 2: 子节点恰好是相同 bit 纯内部节点 -> 原地升级，拒绝夹塞避免断链 ─── */
    if (child != NULL && child->bit == insert_bit && diff_bit >= bit_len)
    {
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
        child->is_end = 1;
        if (child->rules[proto_idx] != NULL)
            patricia_free(child->rules[proto_idx]);
        child->rules[proto_idx] = rule;
        return 0;
    }

    /* ─── 只有上述原地升级都不触发时，才真正建立新 leaf 节点进行夹塞或标准分叉 ─── */
    patricia_node_t *leaf = make_leaf(key, bit_len, rule, proto_idx);
    if (leaf == NULL)
        return -1;

    /* 场景 B: 作为全新的根祖先插入（缩短现有整树的前缀） */
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
        /* 纯祖先夹塞：此时 child 必然是严格大于 insert_bit 的深层子树，完美夹在 p 和 child 之间 */
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
        /* 标准分叉：新 key 和最邻近节点在 diff_bit 处分道扬镳 */
        int b_new = BIT_TEST(key, diff_bit);

        /* ─── 终极精确优化 ───
         * 如果下层 child 节点的测试位正好等于分叉位 diff_bit，
         * 说明路由占位符已存在。根据严格的拓扑反证法，其 b_new 方向必为空槽。
         * 直接安全地原位填空，拒绝繁衍冗余节点，且绝不破坏已有子树！ */
        if (child != NULL && child->bit == diff_bit)
        {
            if (b_new == 0)
                child->left = leaf;
            else
                child->right = leaf;
            return 0;
        }

        /* ─── 常规分叉 ───
         * 如果 child == NULL 或者 child->bit > diff_bit，说明此处是一片空白，
         * 必须老老实实繁衍一个新的内部路由节点来挑起两边的分叉。 */
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

rule_t **patricia_search(patricia_node_t *root, const void *key, int bit_len)
{
    patricia_node_t *node = root;
    patricia_node_t *target = NULL;

    while (node != NULL && node->bit < bit_len)
    {
        if (node->is_end && node->prefix_bits == bit_len && memcmp(node->key, key, node->key_len) == 0)
        {
            target = node;
        }
        node = (BIT_TEST(key, node->bit) == 0) ? node->left : node->right;
    }
    return (target != NULL) ? target->rules : NULL;
}

rule_t **patricia_lpm(patricia_node_t *root, const void *key, int bit_len)
{
    patricia_node_t *node = root;
    rule_t **best = NULL;

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

int patricia_delete(patricia_node_t **root, const void *key, int bit_len, int proto_idx)
{
    patricia_node_t *path[128], *target = NULL, *node = *root;
    int dirs[128], depth = 0, target_depth = -1;

    if (root == NULL || *root == NULL || key == NULL)
        return -1;

    while (node != NULL && node->bit < bit_len)
    {
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

    if (target == NULL || target->rules[proto_idx] == NULL)
        return -1;

    patricia_free(target->rules[proto_idx]);
    target->rules[proto_idx] = NULL;
    for (int i = 0; i < RULE_PROTO_NUM; i++)
    {
        if (target->rules[i] != NULL)
            return 0; /* 还有其他协议存活 */
    }

    target->is_end = 0;
    patricia_free(target->key);
    target->key = NULL;

    /* 闭环处理：节点收缩与路径压缩 */
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
    }

    /* 向上级联收缩 */
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
    }
    return 0;
}

rule_t *acl_lookup(patricia_table_t *table, const void *addr, int af, uint8_t proto, uint16_t port)
{
    patricia_node_t *root = (af == AF_INET6) ? table->v6_root : table->v4_root;
    int bit_len = (af == AF_INET6) ? 128 : 32;
    int proto_idx = proto_to_idx(proto);
    if (proto_idx < 0)
        return NULL;

    rule_t **rules = patricia_lpm(root, addr, bit_len);
    if (rules == NULL)
        return NULL;

    rule_t *best = rules[proto_idx];
    if (best == NULL)
        return NULL;
    if (proto != IPPROTO_ICMP)
    {
        if (port < best->port_lo || port > best->port_hi)
            return NULL;
    }
    return best;
}