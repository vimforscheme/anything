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
        int b = BIT_TEST(key, node->bit); // 始终按 key 的实际位走
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
    if (proto != IPPROTO_ICMP && proto != IPPROTO_ICMPV6)
    {
        if (port < best->port_lo || port > best->port_hi)
            return NULL;
    }
    return best;
}

/**
 * 下述内容均为外部副作用函数，将startip-endip形式转换为前缀树支持的形式
 *
 */

/**
 * 内部辅助函数：自动对二进制 Key 进行网络掩码裁剪，防止主机位污染边界
 */
static void apply_cidr_mask(uint8_t *key, int bit_len, int max_bytes)
{
    int bytes = bit_len / 8;
    int rem = bit_len % 8;

    if (bytes < max_bytes)
    {
        if (rem > 0)
        {
            uint8_t mask = (uint8_t)(0xFF << (8 - rem));
            key[bytes] &= mask; /* 清洗当前非整字节的剩余主机位 */
            bytes++;
        }
        /* 将完全属于主机位的后续字节全部清零 */
        while (bytes < max_bytes)
        {
            key[bytes++] = 0;
        }
    }
}

/**
 * 跨平台 CIDR 字符串解析器
 * @param cidr_str 输入的原始 IP 字符串（例如 "192.168.3.5/22" 或 "2001:db8::/64" 或单点主机 "10.1.1.1"）
 * @param out_meta 转换要素的落地方
 * @return 0 成功, -1 格式非法或解析失败
 */
int patricia_parse_cidr(const char *cidr_str, patricia_insert_meta_t *out_meta)
{
    if (cidr_str == NULL || out_meta == NULL)
    {
        return -1;
    }

    /* 1. 安全复制字符串，避免破坏用户传入的原始指针 */
    char buf[128];
    strncpy(buf, cidr_str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* 2. 检索并切分 '/' 掩码符号 */
    char *slash = strchr(buf, '/');
    int explicit_bits = -1;
    if (slash != NULL)
    {
        *slash = '\0';
        explicit_bits = atoi(slash + 1);
    }

    memset(out_meta, 0, sizeof(*out_meta));

    /* 3. 跨平台智能探测与转换 */
    // 尝试解析为 IPv4
    struct in_addr v4addr;
    if (inet_pton(AF_INET, buf, &v4addr) == 1)
    {
        out_meta->af = AF_INET;
        out_meta->bit_len = (explicit_bits >= 0) ? explicit_bits : 32;

        if (out_meta->bit_len < 0 || out_meta->bit_len > 32)
        {
            return -1; /* 掩码范围非法 */
        }

        memcpy(out_meta->key_bin, &v4addr.s_addr, 4);
        /* 自动清洗主机位（如将 192.168.3.5/22 规范化为 192.168.0.0/22） */
        apply_cidr_mask(out_meta->key_bin, out_meta->bit_len, 4);
        return 0;
    }

    // 尝试解析为 IPv6
    struct in6_addr v6addr;
    if (inet_pton(AF_INET6, buf, &v6addr) == 1)
    {
        out_meta->af = AF_INET6;
        out_meta->bit_len = (explicit_bits >= 0) ? explicit_bits : 128;

        if (out_meta->bit_len < 0 || out_meta->bit_len > 128)
        {
            return -1; /* 掩码范围非法 */
        }

        memcpy(out_meta->key_bin, &v6addr, 16);
        /* 自动清洗 IPv6 主机位 */
        apply_cidr_mask(out_meta->key_bin, out_meta->bit_len, 16);
        return 0;
    }

    return -1; /* 既不是合法的 v4 也不是合法的 v6 */
}

/**
 * 业务层统一规则配置总封装
 */
int acl_add_rule_string(patricia_table_t *table, const char *cidr_str, rule_t *rule)
{
    patricia_insert_meta_t meta;

    // 1. 调用前置转换器提取要素
    if (patricia_parse_cidr(cidr_str, &meta) != 0)
    {
        fprintf(stderr, "Error: Invalid CIDR format '%s'\n", cidr_str);
        return -1;
    }

    // 根据不同的协议类型判定插入槽位
    int proto_idx = proto_to_idx(rule->proto);
    if (proto_idx < 0)
        return -1;

    // 2. 完美无缝对接底层的 patricia_insert
    if (meta.af == AF_INET)
    {
        // 对接 v4_root 树根，传入清洗后的 key_bin 和精确位长
        return patricia_insert(&table->v4_root, meta.key_bin, meta.bit_len, rule, proto_idx);
    }
    else if (meta.af == AF_INET6)
    {
        // 对接 v6_root 树根
        return patricia_insert(&table->v6_root, meta.key_bin, meta.bit_len, rule, proto_idx);
    }

    return -1;
}

/* ─── IPv4 任意范围自动切分并安全插入（修复全网段全跨度溢出） ─── */
int patricia_insert_range_v4(patricia_node_t **root,
                             uint32_t start_host, uint32_t end_host,
                             rule_t *rule, int proto_idx)
{
    if (root == NULL || start_host > end_host)
        return -1;

    while (start_host <= end_host)
    {
        /* 提升到 64 位计算 span，完美支持全网 0x100000000ULL 跨度 */
        uint64_t span = (uint64_t)end_host - start_host + 1;

        /* 提取 start_host 末尾连续 0 能支持的最大 2 的幂次方块 */
        uint64_t block64 = start_host ? (start_host & (-start_host)) : 0x100000000ULL;

        /* 如果当前对齐块越界超出了剩余的跨度 span */
        if (block64 > span)
        {
            /* 寻找小于或等于 span 的最大 2 的幂次方块 */
            block64 = 1;
            while (block64 <= span / 2)
            {
                block64 <<= 1;
            }
        }

        /* 计算当前对齐块对应的 CIDR 掩码位长 */
        int shift = 0;
        uint64_t temp = block64;
        while (temp > 1)
        {
            temp >>= 1;
            shift++;
        }
        int bit_len = 32 - shift;

        /* 调用定版的无懈可击的两阶段插入函数 */
        uint32_t ip_net = htonl(start_host);
        if (patricia_insert(root, &ip_net, bit_len, rule, proto_idx) != 0)
        {
            return -1;
        }

        /* 如果本次插入的 block 已经完全覆盖了剩余跨度，直接完美退出 */
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

/* ─── IPv6 128位大数纯算术最优切分（完美支持 MSVC / GCC 跨平台且保证最优 CIDR 繁衍） ─── */
int patricia_insert_range_v6(patricia_node_t **root,
                             const uint8_t *start_net, const uint8_t *end_net,
                             rule_t *rule, int proto_idx)
{
    if (root == NULL || start_net == NULL || end_net == NULL)
        return -1;

    /* 解包为高低位两个 uint64_t 进行纯代数模拟 */
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
        /* 核心闭环：精准计算原始差值 diff = end - start 且绝对不加 1 避免溢出 */
        uint64_t diff_h = eh - sh;
        uint64_t diff_l = el - sl;
        if (el < sl)
            diff_h--;

        /* 提取当前 start_host 的最大潜在位对齐块 */
        uint64_t block_h = 0, block_lo = 0;
        if (sl != 0)
            block_lo = sl & (-sl);
        else if (sh != 0)
            block_h = sh & (-sh);

        /* 计算当前 block 块对应的 block - 1 掩码值 */
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
            /* start 为全 0 状态，最大掩码占满整个 128 位地球空间 */
            mask_h = 0xffffffffffffffffULL;
            mask_l = 0xffffffffffffffffULL;
        }

        /* 核心数学拦截：如果发现 block - 1 > diff，说明现有容量塞不下该大块，触发收缩 */
        if (compare_128(mask_h, mask_l, diff_h, diff_l) > 0)
        {
            /* 采用全 1 掩码递增生长法，寻找能塞进 diff 差值的最大 power-of-2 块 */
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
                {
                    break;
                }
            }
            /* 从最终的最优全 1 掩码中反向还原真实的 block 大数 */
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

        /* 计算当前最终 block 的 128 位 shift 偏移量，换算前缀位长 */
        int shift = 0;
        uint64_t th = block_h, tl = block_lo;
        while (compare_128(th, tl, 0, 1) > 0)
        {
            tl = (tl >> 1) | (th << 63);
            th >>= 1;
            shift++;
        }
        int bit_len = 128 - shift;

        /* 打包回 16 字节网络二进制矩阵 */
        uint8_t ip6_block[16];
        for (int i = 0; i < 8; i++)
        {
            ip6_block[i] = (uint8_t)(sh >> ((7 - i) * 8));
            ip6_block[i + 8] = (uint8_t)(sl >> ((7 - i) * 8));
        }

        /* 塞入定版的两阶段算法中 */
        if (patricia_insert(root, ip6_block, bit_len, rule, proto_idx) != 0)
        {
            return -1;
        }

        /* 拓扑学边界判定：如果本次计算出的掩码刚好等于 diff 差值，
         * 说明该块完美填满了剩余的所有空间，直接终结，从根本上封死 128 位大数溢出 */
        if (mask_h == diff_h && mask_l == diff_l)
            break;

        /* 步长线性推进：start += block */
        uint64_t prev_sl = sl;
        sl += block_lo;
        sh += block_h;
        if (sl < prev_sl)
            sh++; /* 跨越 64 位大数的低位向高位进位 */
    }
    return 0;
}

/**
 * 工业级全兼容业务配置总入口
 * 支持 CIDR (192.168.1.0/24)、单主机 (10.1.1.1) 以及 范围区间 (172.16.253.1-172.16.253.2)
 */
int acl_add_rule_universal(patricia_table_t *table, const char *input_str, rule_t *rule)
{
    if (table == NULL || input_str == NULL || rule == NULL)
        return -1;

    // 1. 探测是否包含范围连接符 '-'
    char *dash = strchr(input_str, '-');
    if (dash != NULL)
    {
        /* ─── 场景 A：用户输入的是 IP 范围区间 ─── */
        char start_ip[64], end_ip[64];
        size_t start_len = dash - input_str;

        if (start_len >= sizeof(start_ip) || strlen(dash + 1) >= sizeof(end_ip))
        {
            return -1;
        }

        // 切分起始和结束字符串
        strncpy(start_ip, input_str, start_len);
        start_ip[start_len] = '\0';
        strcpy(end_ip, dash + 1);

        // 智能判定是 v4 还是 v6 范围
        if (strchr(start_ip, ':') != NULL)
        {
            /* IPv6 范围插入 */
            uint8_t s_net[16], e_net[16];
            if (inet_pton(AF_INET6, start_ip, s_net) != 1 || inet_pton(AF_INET6, end_ip, e_net) != 1)
            {
                return -1;
            }
            int proto_idx = proto_to_idx(rule->proto);
            return patricia_insert_range_v6(&table->v6_root, s_net, e_net, rule, proto_idx);
        }
        else
        {
            /* IPv4 范围插入 */
            struct in_addr s_addr, e_addr;
            if (inet_pton(AF_INET, start_ip, &s_addr) != 1 || inet_pton(AF_INET, end_ip, &e_addr) != 1)
            {
                return -1;
            }
            int proto_idx = proto_to_idx(rule->proto);
            return patricia_insert_range_v4(&table->v4_root, ntohl(s_addr.s_addr), ntohl(e_addr.s_addr), rule, proto_idx);
        }
    }

    /* ─── 场景 B：用户输入的是标准的 CIDR 或单点主机，直接原封不动调用你定版的函数 ─── */
    return acl_add_rule_string(table, input_str, rule);
}