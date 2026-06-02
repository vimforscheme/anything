#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <netinet/ip6.h>
#include <netinet/in.h>
#endif
#include "patricia_copy.h"

typedef enum
{
    PROTO_UNKNOWN = 0,
    PROTO_TCP = 6,
    PROTO_UDP = 17,
    PROTO_ICMP = 58
} ProtocolType;

/**
 * @brief 深度解析 IPv6 报文，纵向穿透所有已知扩展头，精准提取 L4 最终协议和 L3 物理总长度
 * @param packet 原始网络报文首地址
 * @param len 当前报文缓存区的物理剩余安全长度
 * @param ip6_head_len [输出参数] 最终计算出的 IPv6 整体头部总长度（基础头 + 所有扩展头的物理合并跨度）
 * @return uint8_t 最终的 L4 协议号（如 TCP/UDP/ICMPv6），若解析失败、越界或遇到未知畸形头则返回 0xFF
 */
uint8_t get_ipv6_final_next_header(const uint8_t *packet, size_t len, int *ip6_head_len)
{
    if (packet == NULL || ip6_head_len == NULL || len < sizeof(struct ip6_hdr))
        return 0xFF;

    const struct ip6_hdr *ip6 = (const struct ip6_hdr *)packet;
    uint8_t next = ip6->ip6_nxt;
    size_t offset = sizeof(struct ip6_hdr);

    while (1)
    {
        switch (next)
        {
        /* ─── 1. 最终上层协议终点（或者显式声明的无后续载荷） ─── */
        case IPPROTO_TCP:
        case IPPROTO_UDP:
        case IPPROTO_ICMPV6:
        case IPPROTO_ICMP:
        case IPPROTO_NONE:               /* 协议号 59: 正确闭环 IPv6 无后续头部的合法边界 */
            *ip6_head_len = (int)offset; /* 核心修补：对所有终点一视同仁写入偏移量 */
            return next;

        /* ─── 2. 标准 8 字节单位扩展头 (Hop-by-Hop, Routing, Destination Options) ─── */
        case IPPROTO_HOPOPTS:
        case IPPROTO_ROUTING:
        case IPPROTO_DSTOPTS:
        {
            if (offset + 2 > len)
                return 0xFF;

            uint8_t ext_len = packet[offset + 1];
            size_t hdr_len = (size_t)(ext_len + 1) * 8; /* RFC 8200: 长度 = (ext_len + 1) * 8 字节 */

            if (offset + hdr_len > len)
                return 0xFF;

            next = packet[offset]; /* 下一个头部的类型常驻在扩展头的第 0 字节 */
            offset += hdr_len;
            break; /* 状态机步进，继续探测 */
        }

        /* ─── 3. 固定 8 字节的分片扩展头 ─── */
        case IPPROTO_FRAGMENT:
        {
            if (offset + 8 > len)
                return 0xFF;

            /* 提取 16 比特的分片偏移与标志位 */
            uint16_t frag_off_flags = (uint16_t)(packet[offset + 2] << 8) | packet[offset + 3];
            uint16_t frag_offset = (frag_off_flags & 0xFFF8) >> 3;

            /* 核心安全审计：非首片（分片偏移不为 0）的报文在物理上根本不携带 L4 头部，
             * 无法提取端口信息，直接实施无伤安全拦截返回 */
            if (frag_offset != 0)
                return 0xFF;

            next = packet[offset];
            offset += 8;
            break;
        }

        /* ─── 4. 认证头 (AH Header, 4 字节单位) ─── */
        case IPPROTO_AH:
        {
            if (offset + 2 > len)
                return 0xFF;

            uint8_t ah_len = packet[offset + 1];
            size_t hdr_len = (size_t)(ah_len + 2) * 4; /* RFC 4302: AH 长度 = (Payload Len + 2) * 4 字节 */

            if (offset + hdr_len > len)
                return 0xFF;

            next = packet[offset];
            offset += hdr_len;
            break;
        }

        /* ─── 5. 加密安全载荷 (ESP Header) 或 其它未知/不支持扩展头 ─── */
        case IPPROTO_ESP:
            /* ESP (协议号 50) 后续载荷已被强行加密，在没有解密密钥的前提下，
             * 转发面根本无法继续穿透刺探其 L4 端口，安全断链返回失败 */
        default:
            return 0xFF;
        }
    }
}

ProtocolType detect_protocol(const uint8_t *packet, size_t len, int *ip6_head_len)
{
    uint8_t proto = PROTO_UNKNOWN;
    if (packet == NULL || len < 1)
        return proto;
    int version = packet[0] >> 4;
    if (version == 4)
    {
        proto = packet[9];
    }
    if (version == 6)
    {
        proto = get_ipv6_final_next_header(packet, len, ip6_head_len);
    }
    if (proto == 1 || proto == 58)
    {
        proto = PROTO_ICMP;
    }
    return proto;
}

/**
 * @brief 从原始网卡报文中一趟式提取目的 IP、地址族以及传输层目的端口
 * @param packet       [输入] 原始网络报文首地址
 * @param len          [输入] 当前报文缓冲区的物理安全长度
 * @param dst_ip       [输出] 接收目的 IP 的缓冲区指针（IPv4 写入 4 字节，IPv6 写入 16 字节）
 * @param af           [输出] 接收地址族标记（AF_INET 或 AF_INET6）
 * @param dst_port     [输出] 接收主机字节序的目的端口（ICMP 或未知协议自动清零为 0）
 * @return int         状态码：0 代表提取成功；-1 代表报文畸形、越界或未知协议
 */
int get_packet_dst_info(const uint8_t *packet, size_t len, void *dst_ip, int *af, uint16_t *dst_port, ProtocolType *proto_out)
{
    if (packet == NULL || dst_ip == NULL || af == NULL || dst_port == NULL || len < 1)
        return -1;

    int ip6_head_len = 0;
    /* 1. 直接调用你的核心底座，获取清洗后的协议类型，并顺手捞出 IPv6 头部总长 */
    *proto_out = detect_protocol(packet, len, &ip6_head_len);
    ProtocolType proto = *proto_out;
    if (proto == PROTO_UNKNOWN)
        return -1;

    int version = packet[0] >> 4;

    /* ─── IPv4 提取路径 ─── */
    if (version == 4)
    {
        if (len < 20)
            return -1; /* 安全刚性防线：标准 IPv4 基础头至少 20 字节 */

        *af = AF_INET;
        /* RFC 791: IPv4 目的 IP 位于固定的第 16-19 字节 */
        memcpy(dst_ip, packet + 16, 4);

        /* 提取端口（仅 TCP/UDP 存在端口） */
        if (proto == PROTO_TCP || proto == PROTO_UDP)
        {
            int ihl = (packet[0] & 0x0F) * 4; /* 动态计算 IPv4 头部实际长度（含 Options） */
            if (len < (size_t)ihl + 4)
                return -1; /* L4 载荷边界溢出保护 */

            /* TCP/UDP 目的端口均位于 L4 头部开头的第 2-3 字节，使用位移直接转为主机字节序 */
            *dst_port = (uint16_t)((packet[ihl + 2] << 8) | packet[ihl + 3]);
        }
        else
        {
            *dst_port = 0; /* 所有其他协议 */
        }
        return 0;
    }

    /* ─── IPv6 提取路径 ─── */
    if (version == 6)
    {
        if (len < 40)
            return -1; /* 安全刚性防线：标准 IPv6 基础头至少 40 字节 */

        *af = AF_INET6;
        /* RFC 8200: IPv6 目的 IP 位于固定的第 24-39 字节 */
        memcpy(dst_ip, packet + 24, 16);

        /* 提取端口 */
        if (proto == PROTO_TCP || proto == PROTO_UDP)
        {
            /* 完美复用通过你的 detect_protocol 深度探出的真实 L4 偏移量 */
            if (len < (size_t)ip6_head_len + 4)
                return -1;

            *dst_port = (uint16_t)((packet[ip6_head_len + 2] << 8) | packet[ip6_head_len + 3]);
        }
        else
        {
            *dst_port = 0; /* 所有其他协议 */
        }
        return 0;
    }

    return -1; /* 异常版本拦截 */
}

int check_one_packet_valid_2(patricia_table_t *acl_table, const uint8_t *packet, size_t len)
{
    // todo如果没有规则（这里指的是 实际上下发的ip资源内容信息） 就直接return 0就行
    uint8_t dst_ip[16];
    int af = 0;
    uint16_t dst_port = 0;

    ProtocolType proto = PROTO_UNKNOWN;
    if (get_packet_dst_info(packet, len, dst_ip, &af, &dst_port, &proto) != 0)
    {
        // 莫名奇妙的东西，不管他
        return 0;
    }
    if (proto == PROTO_UNKNOWN || proto == PROTO_ICMP)
    {
        // 非tcp udp协议无需比较
        return 0;
    }
    rule_t *match_rule = acl_lookup(acl_table, dst_ip, af, (uint8_t)proto, dst_port);

    if (match_rule != NULL)
    {
        printf("流量命中放行规则！放行端口区间: %d-%d\n", match_rule->port_lo, match_rule->port_hi);
        // 执行物理转发（Forward）
        return 0;
    }
    else
    {
        // 执行丢弃（Drop）
        return 1;
    }
}