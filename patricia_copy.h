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

#define RULE_PROTO_NUM 3 /* 传输层协议分类槽位总数：ICMP=0, TCP=1, UDP=2 */
#define PATRICIA_MAX_DEPTH 128

/* ============================================================================
 * 1. rule_t：外部安全策略资产结构体
 * ============================================================================
 * 表达纯粹的、只读的业务拦截规则（例如：允许访问 TCP 8000-9000 端口）。
 * 核心设计：该结构体内部不包含任何侵入式（Intrusive）的树拓扑或链表指针。
 * 这样做允许在发生 IP 范围切分（Range Split）时，将同一个 rule_t 物理堆内存实例
 * 安全地、零拷贝地同时挂载到前缀树的不同二进制路径节点中。
 * 最终由调用方维护的扁平账本一锅端平铺释放，彻底免除 Double-Free 的物理隐患。
 * ============================================================================ */
typedef struct
{
	uint8_t proto;	  /* 传输层协议类型标识 (标准网络栈度量：IPPROTO_TCP, IPPROTO_UDP, IPPROTO_ICMP 等) */
	uint16_t port_lo; /* 端口过滤区间的闭区间下限 (控制面全放行时，由高层规范化对齐为 0) */
	uint16_t port_hi; /* 端口过滤区间的闭区间上限 (控制面全放行时对齐为 65535；仅单端口匹配时 port_lo == port_hi) */
} rule_t;

/* ============================================================================
 * 2. rule_link_t：容器内部专用包装链表节点
 * ============================================================================
 * 专门属于前缀树容器内部维护的骨架。其生命周期所有权完全归属于前缀树。
 * 业务语义：横向同级并集链表。
 * 当网络管理员在【完全相同的 IP 网段及掩码】且【相同的协议槽位】下，连续录入多条
 * 离散的端口控制策略时，前缀树控制面将通过“头插法”在此处挂载、串联这些不同的规则。
 * 数据面快速路径（acl_lookup）在命中该网段节点时，会顺着 next 轴线进行盲测，
 * 从而在不改变底层轻量级结构体的前提下，演进出多离散区间最大兼容的“并集（或门）”拦截判定。
 * ============================================================================ */
typedef struct rule_link
{
	rule_t *rule;			/* 指向外部物理业务策略资产 rule_t 实体内存的只读指针 */
	struct rule_link *next; /* 指向挂载在同一个 IP 节点、同协议下的下一条并集包装节点，链表尾部为 NULL */
} rule_link_t;

/* ============================================================================
 * 3. patricia_node_t：Patricia Trie 前缀树物理节点结构体
 * ============================================================================
 * 构建起 1-bit 压缩基数树（Radix Tree）分叉索引拓扑的核心骨架。
 * 它承担了纵向二进制线性路径下钻与横向分布式策略存储的双重职责。
 * ============================================================================ */
typedef struct patricia_node
{
	struct patricia_node *left;			/* 左子节点指针：代表当前测试位（bit）检查结果为 0 的二进制分支 */
	struct patricia_node *right;		/* 右子节点指针：代表当前测试位（bit）检查结果为 1 的二进制分支 */
	int bit;							/* 二进制位测试索引（Bit Index）：指示当前中间节点应该对传入 IP 的第几位执行按位提取分叉测试 */
	int is_end;							/* 终点有效标记：1 代表该节点是一个真实的网络前缀策略承载节点；0 代表该节点纯粹是路径压缩衍生出的内部路由分叉占位符 */
	rule_link_t *rules[RULE_PROTO_NUM]; /* 按协议索引的微型包装链表阵列：rules[0]->ICMP并集链表头, rules[1]->TCP并集链表头, rules[2]->UDP并集链表头 */
	uint8_t *key;						/* 动态分配的物理二进制网络前缀缓冲区（大端网络字节序，保证与传入查询 IP 的物理单调对齐） */
	uint8_t key_len;					/* key 缓冲区的物理内存字节长度：IPv4 节点固定为 4 字节，IPv6 节点固定为 16 字节 */
	int prefix_bits;					/* 当前节点真正承载、代表的网络掩码有效前缀位长（CIDR 长度，如 /24, /64, /128 等） */
} patricia_node_t;

/* ============================================================================
 * 4. patricia_table_t：前缀树 ACL 核心控制总表
 * ============================================================================
 * 顶层总控路由管理容器。它将双栈防火墙的 IPv4 索引拓扑与 IPv6 索引拓扑完全解耦，
 * 在底层划分为两个完全互不干扰的独立物理分叉树进行全上线速检索。
 * ============================================================================ */
typedef struct
{
	patricia_node_t *v4_root; /* 指向独立 IPv4 前缀树根节点的物理指针（当没有任何 IPv4 策略灌入、空树时为 NULL） */
	patricia_node_t *v6_root; /* 指向独立 IPv6 前缀树根节点的物理指针（当没有任何 IPv6 策略灌入、空树时为 NULL） */
} patricia_table_t;

/* ============================================================================
 * 5. patricia_insert_meta_t：控制面插入元数据暂存中间结构体
 * ============================================================================
 * 专用于控制面（慢速配置路径）解析网络前缀字符串时的上下文环境擦写。
 * 将上层多变、离散的 CIDR 字符串输入（如 "172.16.3.5/24"）统一规范化提取，
 * 并抹除主机位、应用网络掩码，归一化输出为数据面纯净、规整的二进制网络字节度量。
 * ============================================================================ */
typedef struct
{
	int af;				 /* 地址族标记：标准网络套接字度量，AF_INET (代表 IPv4) 或 AF_INET6 (代表 IPv6) */
	int bit_len;		 /* 从字符串中显式或隐式解析出的网络掩码有效位长，IPv4 限制区间 [1,32]，IPv6 限制区间 [1,128] */
	uint8_t key_bin[16]; /* 规范化、应用掩码截断后的纯净二进制网络前缀数组（最大兼容支持 16 字节物理空间，主机位已被清除为 0） */
} patricia_insert_meta_t;

/* ─── 内存分配包装钩子 ─── */
extern void *(*patricia_alloc)(size_t); /* 动态内存分配函数指针，默认指向标准库 malloc，支持动态注入自定义的高效内存池（如 DPDK rte_malloc） */
extern void (*patricia_free)(void *);	/* 动态内存释放函数指针，默认指向标准库 free，需与分配钩子配对使用 */

/* ─── 快速路径宏：测试大端内存数组的特定比特位 ─── */
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

/* ============================================================================
 * 6. 核心基础设施 API 函数详细描述
 * ============================================================================ */

/**
 * @brief 初始化顶层双栈总控制表
 * @param table 指向待刷新的顶层总表的物理指针
 * @note 时间复杂度 O(1)。在控制面启动初期调用，将 v4 和 v6 的根拓扑刚性强行清空为 NULL，准备接受策略灌单。
 */
void patricia_init_table(patricia_table_t *table);

/**
 * @brief 深度递归销毁前缀树索引拓扑 skeleton
 * @param root 当前准备释放的分支子树根节点（通常传入 table->v4_root 或 table->v6_root）
 * @note 控制面（慢速路径）函数。时间复杂度 O(N)。
 * 核心所有权行为：该函数会顺着树拓扑把所有的内部物理骨架 node 及其 rules 槽位挂载的 rule_link_t
 * 单链表包装壳全部 free 清空，【但绝不越权触碰释放外部调用方管理的 rule_t 实体】。
 * 最大递归深度由 IP 协议位宽限制（IPv4=32, IPv6=128），系统压栈开销恒定在 4KB 以内，绝无爆栈风险。
 */
void patricia_destroy(patricia_node_t *root);

/**
 * @brief 向二进制前缀树灌入一条底层标准 CIDR 规则
 * @param root 指向树根节点指针的二级指针，支持原位动态改写根拓扑
 * @param key 指向已规范化（网络字节序）的纯净二进制前缀数组的指针
 * @param bit_len 当前策略的网络掩码位长（CIDR 长度，禁止灌入 /0 全网通策略，限制区间 v4[1,32], v6[1,128]）
 * @param rule 外部只读安全资产实体的内存地址
 * @param proto_idx 映射转换后的协议阵列槽位索引（0=ICMP, 1=TCP, 2=UDP）
 * @return int 状态码：0 代表成功灌入；-1 代表发生非法越界拦截或内存耗尽
 * @note 控制面（慢速路径）核心函数。时间复杂度 O(bit_len)。
 * 具备三大核心防御性行为：
 * 1. 包含 is_rule_duplicate 指针去重保护，同一规则重复灌入会自动拦截并返回成功，不增加重复包装壳。
 * 2. 当发生精确命中（网段和掩码位长完全一致）时，改写原位覆盖为【单链表头插法串联】，实现同网段横向并集拓扑。
 * 3. 包含 Patch 1 和 Patch 2 路径压缩中间节点原地升级机制，避免冗余割裂。
 */
int patricia_insert(patricia_node_t **root, const void *key, int bit_len, rule_t *rule, int proto_idx);

/**
 * @brief 在二进制树内部执行精确网段前缀搜索（Exact Match）
 * @param root 前缀树分支根节点
 * @param key 指向待比对二进制网段大端缓冲区的指针
 * @param bit_len 待检索的目标网段掩码位长
 * @return rule_link_t** 成功则返回该精确节点内部协议策略链表阵列首地址（二级指针 rules）；未命中则返回 NULL
 * @note 控制面（慢速路径）辅助工具。时间复杂度 O(bit_len)。
 * 只有当树中存在一个节点的【前缀位长完全等于 bit_len】且【网络前缀完全一致】时才视为命中。
 */
rule_link_t **patricia_search(patricia_node_t *root, const void *key, int bit_len);

/**
 * @brief 在二进制树内部执行标准路由最长前缀匹配（Longest Prefix Match）
 * @param root 前缀树分支根节点
 * @param key 待检索的完整原始主机二进制 IP 地址指针（网络字节序）
 * @param bit_len 主机最大探测位宽深度（IPv4=32, IPv6=128）
 * @return rule_link_t** 返回沿二进制下钻路径上，网络掩码掩盖范围最长、最精确匹配的那个节点的 rules 策略槽位首地址；未命中则返回 NULL
 * @note 控制面与通用工具层标准路由查询。时间复杂度 O(bit_len)。
 * 它会忠实地沿路径下钻搜寻最佳网络前缀匹配，并返回该节点对应的整个多协议链表包装阵列。
 */
rule_link_t **patricia_lpm(patricia_node_t *root, const void *key, int bit_len);

/**
 * @brief 从前缀树特定网段中精准摘除并解绑某一条特定的安全策略
 * @param root 指向树根节点指针的二级指针，若摘空退化可能触发整棵树的拓扑级联塌陷与路径压缩折叠
 * @param key 目标网段的二进制大端缓冲区指针
 * @param bit_len 目标网段的掩码位长
 * @param proto_idx 协议槽位索引
 * @param rule 准备精准踢出的外部 rule_t 物理内存指针
 * @return int 状态码：0 代表精准摘除成功，且如果节点被彻底薅空，已自动触发路径收缩折叠；-1 代表未找到该策略
 * @note 控制面动态上下线维护函数。时间复杂度 O(bit_len) + O(局部链表长度)。
 * 核心所有权行为：它在单链表中通过指针逐个比对，【只释放树自身拥有的 rule_link_t 包装架，绝不 free 传入的 rule_t 实体】。
 * 只有当该节点下的所有协议（ICMP/TCP/UDP）以及同级并集链表被彻底拔空时，才会触发 target->is_end = 0，
 * 并释放节点内部的 key 缓冲区，同时向上级联折叠合并单亲分支，确保树的拓扑紧凑性。
 */
int patricia_delete(patricia_node_t **root, const void *key, int bit_len, int proto_idx, rule_t *rule);

/**
 * @brief 核心数据面（Fast Path）线速网络流量并集过滤总路由接口
 * @param table 顶层双栈总控制表指针
 * @param addr 进站报文网络层的原始大端二进制源 IP 地址指针（物理单调对齐查询）
 * @param af 地址族（AF_INET 或 AF_INET6），引导流量自动分流至对应的独立树骨架
 * @param proto 传输层网络协议号（标准度量：IPPROTO_TCP, IPPROTO_UDP, IPPROTO_ICMP 等）
 * @param port 传输层流量的解包目标端口号（大端或主机序需与录入时的 port_lo/hi 保持刚性对齐）
 * @return rule_t* 放行凭证指针。若返回非 NULL，代表当前流量命中了允许放行的并集策略，返回的是第一个高喊“YES”的只读规则实体指针；返回 NULL 代表拒绝或未命中，流量一律丢弃（Drop）
 * @note 数据面（快速路径 Fast Path）绝对核心。时间复杂度严格锁死在单向线性下钻的 O(32) 或 O(128)。
 * 【两横一纵分布式并集状态机落地形态】：
 * 1. 纵向：while 循环顺着二进制路径单向线性下钻，将大网段套小网段（如 /16 到 /24）沿途路过的所有有效节点进行网络前缀比对。
 * 2. 横向：一旦在下钻沿途撞上 prefix_match 命中的节点，内层 while 会无缝顺着横向包装链表（curr_link）向后盲测。
 * 3. 需求保底：ICMP 流量自动跳过端口区间判定直接短路放行；常规 TCP/UDP 流量通过通用区间 (port >= lo && port <= hi)
 * 单行无分支流水线指令盲测。全放行(0~65535)天然恒真，精准端口0(0~0)天然只放行0，完美演绎分布式“或门电路”业务并集效果。
 */
rule_t *acl_lookup(patricia_table_t *table, const void *addr, int af, uint8_t proto, uint16_t port);

/**
 * @brief 控制面工具链：向系统中灌入一条标准 CIDR 格式的策略字符串
 * @param table 顶层双栈总控制表指针
 * @param cidr_str 标准 CIDR 字符串输入（如 "192.168.1.0/24" 或 "2001:db8::/64"）
 * @param rule 待绑定的外部只读规则资产指针
 * @return int 状态码：0 代表成功；-1 代表解析失败或被 bit_len <= 0 刚性防线拦截（拒绝 /0 全网通录入）
 * @note 控制面配置函数。内部调用专有私有工具 static 函数 patricia_parse_cidr 进行物理反转和清除主机位掩码擦写，
 * 自动识别 v4/v6 地址族并安全分流分派给底层底座 patricia_insert 执行入树。
 */
int acl_add_rule_string(patricia_table_t *table, const char *cidr_str, rule_t *rule);

/**
 * @brief 控制面智能接口：万能防火墙规则通用灌单入口
 * @param table 顶层双栈总控制表指针
 * @param input_str 支持【标准 CIDR 字符串（如 10.1.1.0/24）】或【任意横杠离散 IP 范围（如 192.168.1.5-192.168.1.20）】
 * @param rule 待绑定的外部只读规则资产指针
 * @return int 状态码：0 代表成功；-1 代表格式错误或包含全网通溢出拒绝
 * @note 控制面核心配置接口。满足安全编码规范（CWE-120），内部前置严格的缓冲区截断与终止符封底保护。
 * 如果是连字符范围，会自动提取并安全流向底层的 Range 自动化切碎合并引擎。
 */
int acl_add_rule_universal(patricia_table_t *table, const char *input_str, rule_t *rule);

/**
 * @brief IPv4 任意主机范围自动化切碎与 CIDR 批量合并插入引擎
 * @param root 指向 table.v4_root 的二级指针
 * @param start_host 起始 IPv4 主机序 32位无符号整数（如 0xC0A80105）
 * @param end_host 结束 IPv4 主机序 32位无符号整数（如 0xC0A80114）
 * @param rule 外部只读策略资产实体指针
 * @param proto_idx 协议槽位索引
 * @return int 状态码：0 代表成功；-1 代表物理范围倒置或插入失败
 * @note 控制面（慢速路径）高级算法。时间复杂度取决于分段碎片的数量。
 * 利用最高效的位运算特性 start_host & (-start_host) 提取尾部对齐零位，动态与剩余 span 跨度进行自然夹逼。
 * 能够将任意奇葩、不对齐的任意有源区间范围，自动优雅切碎并转化为【对齐的标准 CIDR 块】批量调用 patricia_insert 塞入树中。
 * 包含前置核心刚性防线：如果试图录入 0.0.0.0-255.255.255.255 全网通范围，直接返回 -1 阻断。
 */
int patricia_insert_range_v4(patricia_node_t **root, uint32_t start_host, uint32_t end_host, rule_t *rule, int proto_idx);

/**
 * @brief IPv6 128位大数范围自动化切碎与标准前缀块批量合并插入引擎
 * @param root 指向 table.v6_root 的二级指针
 * @param start_net 起始 IPv6 16字节网络字节序大端数组首地址
 * @param end_net 结束 IPv6 16字节网络字节序大端数组首地址
 * @param rule 外部只读策略资产实体指针
 * @param proto_idx 协议槽位索引
 * @return int 状态码：0 代表成功；-1 代表物理溢出拦截或插入失败
 * @note 控制面（慢速路径）高级算法。由于 C 语言原生不支持 128 位无符号大数，内部通过双 64位无符号整数（hi, lo）
 * 拼接模拟实现了大数加减、低位跨边界回绕借位判定、以及大数高低位位移收缩夹逼对齐。
 * 【大数溢出终极防御】：一旦输入的范围跨度触碰到了全宇宙空间限制（如从 :: 到 ffff:...:ffff），
 * hi 和 lo 的算术跨度将无二义性地发生溢出回绕归零（block_h == 0 && block_lo == 0）。
 * 代码在此处施加了最高级别的物理安全拦截保护，直接报错返回 -1，从根本上御敌于国门之外，彻底杜绝了底层前缀树发生节点 bit = -1 导致 addr[-1] 硬件内存穿透崩溃的任何未定义行为（Undefined Behavior）风险！
 */
int patricia_insert_range_v6(patricia_node_t **root, const uint8_t *start_net, const uint8_t *end_net, rule_t *rule, int proto_idx);

#endif /* PATRICIA_H */