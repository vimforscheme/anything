#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stddef.h>
#include <assert.h>

#define MBUF_HEADROOM 128

//共享结构体
typedef struct mbuf_shared_info {
    atomic_int refcnt; // 引用计数，用于写时复制
    unsigned char *head; // 缓冲区起始地址
    unsigned char *end; // 缓冲区结束地址（容量边界）
    unsigned char buffer[]; // 柔性数组，实际数据紧跟在结构体后
} mbuf_shared_info_t;

//视图
typedef struct mbuf {
    struct mbuf *next;          // 用于组成普通队列（packet queue）
    struct mbuf *next_frag;     // 指向下一个分片（fragment），实现 scatter-gather
    int pkt_len;                // 整个包的总长度（仅 head mbuf 有效）
    mbuf_shared_info_t *sh;     // 指向共享的缓冲区信息（支持 CoW）
    unsigned char *head;        // 本 mbuf 对应的缓冲区起始
    unsigned char *data;        // 数据起始指针（可前推留 headroom）
    unsigned char *tail;        // 数据结束指针（写入从这里开始）
    unsigned char *end;         // 缓冲区容量结束
} mbuf_t;
static inline int mbuf_len(mbuf_t *m) { return m->tail - m->data; }
static inline int mbuf_tailroom(mbuf_t *m) { return m->end - m->tail; }
static inline int mbuf_headroom(mbuf_t *m) { return m->data - m->head; }

/* ==========================================
 * 2. 内存管理 (Alloc/Free/COW)
 * ========================================== */
// ... (COW 和 Alloc/Free 代码复用之前的，为节省篇幅略去实现细节，逻辑一致) ...

void mbuf_free_chain(mbuf_t *m) {
    while (m) {
        mbuf_t *next = m->next_frag;
        mbuf_shared_info_t *sh = m->sh;
        free(m);
        if (atomic_fetch_sub(&sh->refcnt, 1) == 1) free(sh);
        m = next;
    }
}

// 简单的 alloc 实现，确保可运行
mbuf_t *mbuf_alloc(int payload_size) {
    int total_size = payload_size + MBUF_HEADROOM;
    mbuf_shared_info_t *sh = malloc(sizeof(*sh) + total_size);
    atomic_init(&sh->refcnt, 1);
    sh->head = sh->buffer;
    sh->end  = sh->buffer + total_size;

    mbuf_t *m = calloc(1, sizeof(*m));
    m->sh = sh;
    m->head = sh->head;
    m->data = sh->head + MBUF_HEADROOM;
    m->tail = sh->head + MBUF_HEADROOM;
    m->end  = sh->end;
    return m;
}

// COW 简化版
static void mbuf_ensure_writable(mbuf_t *m) {
    mbuf_shared_info_t *old = m->sh;
    if (atomic_load(&old->refcnt) == 1) return;
    int size = old->end - old->head;
    mbuf_shared_info_t *sh = malloc(sizeof(*sh) + size);
    atomic_init(&sh->refcnt, 1);
    sh->head = sh->buffer;
    sh->end  = sh->buffer + size;
    memcpy(sh->buffer, old->head, size);
    ptrdiff_t offset = m->data - old->head;
    ptrdiff_t len = m->tail - m->data;
    atomic_fetch_sub(&old->refcnt, 1);
    m->sh = sh;
    m->head = sh->head;
    m->data = sh->head + offset;
    m->tail = m->data + len;
    m->end  = sh->end;
}

void mbuf_append_large(mbuf_t *head, const void *buf, int len) {
    mbuf_t *curr = head;
    while (curr->next_frag) curr = curr->next_frag;
    const unsigned char *p = buf;
    int remain = len;
    while (remain > 0) {
        mbuf_ensure_writable(curr);
        int room = mbuf_tailroom(curr);
        if (room > 0) {
            int n = remain < room ? remain : room;
            memcpy(curr->tail, p, n);
            curr->tail += n;
            head->pkt_len += n;
            p += n;
            remain -= n;
        } else {
            mbuf_t *frag = mbuf_alloc(128);
            curr->next_frag = frag;
            curr = frag;
        }
    }
}
/* ==========================================
 * 补全：链式克隆 (工业级标准实现)
 * ========================================== */

// 内部辅助：克隆单个 mbuf 节点 (只负责节点本身，不处理链表关系)
static mbuf_t *__mbuf_clone_one(mbuf_t *src) {
    if (!src) return NULL;

    mbuf_t *dst = malloc(sizeof(*dst));
    if (!dst) return NULL;

    // 1. 复制元数据 (copy struct fields)
    *dst = *src;
    
    // 2. 清空链表指针 (必须切断，手动重连)
    dst->next = NULL; 
    dst->next_frag = NULL;

    // 3. 【关键】增加肉体引用计数
    // 无论是 Head 还是 Frag，只要它指向了 shared_info，就要 +1
    if (src->sh) {
        atomic_fetch_add(&src->sh->refcnt, 1);
    }

    return dst;
}

// 对外 API：克隆整个包 (包括所有分片)
mbuf_t *mbuf_clone(mbuf_t *head) {
    if (!head) return NULL;

    // 1. 克隆头节点
    mbuf_t *new_head = __mbuf_clone_one(head);
    if (!new_head) return NULL;

    // 2. 循环克隆后续分片
    mbuf_t *curr_src = head->next_frag;
    mbuf_t *curr_dst = new_head;

    while (curr_src) {
        // 克隆当前分片
        mbuf_t *new_frag = __mbuf_clone_one(curr_src);
        
        if (!new_frag) {
            // 错误处理：如果内存不足中间断了，必须把前面申请的全释放掉
            // 否则内存泄漏
            mbuf_free_chain(new_head);
            return NULL;
        }

        // 挂载到新链表上
        curr_dst->next_frag = new_frag;
        
        // 推进指针
        curr_dst = new_frag;
        curr_src = curr_src->next_frag;
    }

    return new_head;
}
/* ==========================================
 * 3. 核心 API 族 (New!)
 * ========================================== */

// ---------------------------------------------------------
// [API 1] skb_copy_bits: 跨分片数据拷贝
// 从 offset 开始，拷贝 len 字节到 to
// ---------------------------------------------------------
int mbuf_copy_bits(mbuf_t *m, int offset, void *to, int len) {
    int start = offset;
    int copy_len = len;
    unsigned char *dst = (unsigned char *)to;
    mbuf_t *curr = m;

    // 1. 寻找 offset 所在的起始分片
    while (curr && start >= mbuf_len(curr)) {
        start -= mbuf_len(curr);
        curr = curr->next_frag;
    }

    if (!curr) return -1; // Offset 越界

    // 2. 开始拷贝
    while (curr && copy_len > 0) {
        int avail = mbuf_len(curr) - start;
        if (avail > 0) {
            int to_copy = (copy_len < avail) ? copy_len : avail;
            memcpy(dst, curr->data + start, to_copy);
            
            dst += to_copy;
            copy_len -= to_copy;
        }
        start = 0; // 后续分片都从头开始拷
        curr = curr->next_frag;
    }

    if (copy_len > 0) return -1; // 数据不够长
    return 0;
}

// ---------------------------------------------------------
// [API 2] skb_header_pointer: 智能游标 (Zero-Copy 优先)
// ---------------------------------------------------------
void *mbuf_header_pointer(mbuf_t *m, int offset, int len, void *buffer) {
    int start = offset;
    mbuf_t *curr = m;

    // 1. 定位分片
    while (curr && start >= mbuf_len(curr)) {
        start -= mbuf_len(curr);
        curr = curr->next_frag;
    }
    if (!curr) return NULL;

    // 2. [Fast Path] 如果当前分片剩余数据足够，直接返回指针
    if (mbuf_len(curr) - start >= len) {
        return curr->data + start;
    }

    // 3. [Slow Path] 跨分片了，调用 copy_bits 拼凑到 buffer
    if (mbuf_copy_bits(m, offset, buffer, len) < 0) {
        return NULL;
    }
    return buffer;
}

// ---------------------------------------------------------
// [API 3] skb_pull: 头部剥离 (用于解析协议)
// ---------------------------------------------------------
void *mbuf_pull(mbuf_t *m, int len) {
    // 这里简化处理，通常 skb_pull 只允许 pull 线性区(frag[0])的数据
    // 如果要 pull 的数据跨分片了，内核会调用 __pskb_pull_tail 先把数据这一部分线性化
    
    if (len > mbuf_len(m)) {
        printf("[Pull Error] Cannot pull %d bytes (frag len %d)\n", len, mbuf_len(m));
        return NULL;
    }
    
    m->data += len;
    m->pkt_len -= len;
    return m->data;
}

// ---------------------------------------------------------
// [API 4] skb_trim: 尾部裁剪 (用于去 Padding)
// ---------------------------------------------------------
int mbuf_trim(mbuf_t *m, int new_len) {
    if (new_len >= m->pkt_len) return 0; // 没啥可剪的

    int current_len = 0;
    mbuf_t *curr = m;
    mbuf_t *prev = NULL;

    // 遍历链表，找到剪切点
    while (curr) {
        if (current_len + mbuf_len(curr) > new_len) {
            // 剪切点就在这个 frag 内部
            int keep = new_len - current_len;
            curr->tail = curr->data + keep; // 缩减 tail
            
            // 释放后续所有 frags
            mbuf_free_chain(curr->next_frag);
            curr->next_frag = NULL;
            
            m->pkt_len = new_len;
            return 0;
        }
        
        current_len += mbuf_len(curr);
        prev = curr;
        curr = curr->next_frag;
    }
    
    // 边界情况：可能正好剪到缝隙，代码一般走不到这
    return 0;
}

/* ==========================================
 * 4. 场景演示
 * ========================================== */

void dump_full(mbuf_t *m, const char *msg) {
    printf("\n--- %s (Total: %d) ---\n", msg, m->pkt_len);
    int idx = 0;
    mbuf_t *curr = m;
    while(curr) {
        printf("Frag %d: [", idx++);
        for (unsigned char *p = curr->data; p < curr->tail; p++) printf("%02X ", *p);
        printf("]\n");
        curr = curr->next_frag;
    }
}

int main() {
    // 1. 构造一个跨分片的包
    // Frag 0: 4 bytes (Headroom 128)
    mbuf_t *pkt = mbuf_alloc(4); 
    uint8_t part1[] = {0xAA, 0xBB, 0xCC, 0xDD};
    mbuf_append_large(pkt, part1, 4);

    // Frag 1: 4 bytes (自动分配)
    uint8_t part2[] = {0x11, 0x22, 0x33, 0x44};
    mbuf_append_large(pkt, part2, 4);

    dump_full(pkt, "Initial Packet");
    // Memory: [AA BB CC DD] -> [11 22 33 44]

    // -------------------------------------------------
    // 测试 1: skb_copy_bits (全量导出)
    // -------------------------------------------------
    uint8_t full_copy[8];
    mbuf_copy_bits(pkt, 0, full_copy, 8);
    printf("\n[Test CopyBits] %02X %02X ... %02X\n", full_copy[0], full_copy[1], full_copy[7]);

    // -------------------------------------------------
    // 测试 2: skb_pull (剥离头部 2 字节)
    // -------------------------------------------------
    mbuf_pull(pkt, 2); 
    dump_full(pkt, "After Pull(2)");
    // Memory: [CC DD] -> [11 22 33 44] (Total 6)

    // -------------------------------------------------
    // 测试 3: skb_header_pointer (跨分片读取)
    // -------------------------------------------------
    // 现在 pkt 开头是 CC DD。
    // 我们想读 offset=1 开始的 4 个字节 (即 DD 11 22 33)
    // 这跨越了 Frag0 (只剩 DD) 和 Frag1 (11 22 33 ...)
    uint8_t scratch[4];
    void *ptr = mbuf_header_pointer(pkt, 1, 4, scratch);
    
    printf("\n[Test SmartPtr] Reading 4 bytes at offset 1:\n");
    if (ptr == scratch) printf("-> Type: Slow Path (Copied)\n");
    else printf("-> Type: Fast Path (Direct)\n");
    
    uint8_t *p = (uint8_t *)ptr;
    printf("-> Data: %02X %02X %02X %02X (Expect: DD 11 22 33)\n", p[0], p[1], p[2], p[3]);

    // -------------------------------------------------
    // 测试 4: skb_trim (剪裁到总长 3)
    // -------------------------------------------------
    // 当前总长 6: [CC DD] -> [11 22 33 44]
    // 剪裁到 3:  应该剩下 [CC DD] -> [11]
    // Frag 1 后面的 22 33 44 会被丢弃，如果 Frag 1 变空或多余会被释放
    mbuf_trim(pkt, 3);
    dump_full(pkt, "After Trim(3)");

    mbuf_free_chain(pkt);
    return 0;
}