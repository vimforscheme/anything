#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>
#include <stdlib.h>

typedef struct
{
    // 基础配置域：冷数据独立对齐
    _Alignas(64) _Atomic(void *) *buf;
    uint32_t mask;

    // 生产者控制块（向 DPDK 靠拢：head/tail 同住一个 Cacheline）
    _Alignas(64) struct
    {
        _Atomic uint32_t head;
        _Atomic uint32_t tail;
    } prod;

    // 消费者控制块
    _Alignas(64) struct
    {
        _Atomic uint32_t head;
        _Atomic uint32_t tail;
    } cons;
} mpmc_ring_t;

// 工业级创建函数：前置卡死边界，保障物理对齐
mpmc_ring_t *mpmc_ring_create(uint32_t capacity)
{
    // 严格边界防御：
    // 1. 必须是 2 的幂 (capacity & (capacity - 1)) == 0
    // 2. 容量不得小于 8（确保 buf_size 至少为 64 字节，天然对齐 Cacheline，绝无 UB 风险）
    if (capacity < 8 || (capacity & (capacity - 1)) != 0)
    {
        return NULL;
    }

    // 分配结构体本身（强制 64 字节对齐）
    mpmc_ring_t *rb = aligned_alloc(64, sizeof(mpmc_ring_t));
    if (!rb)
        return NULL;
    memset(rb, 0, sizeof(mpmc_ring_t));
    rb->mask = capacity - 1;

    // 此时 buf_size 必然 >= 64 字节，且绝对是 64 的整数倍
    size_t buf_size = capacity * sizeof(_Atomic(void *));
    rb->buf = aligned_alloc(64, buf_size);
    if (!rb->buf)
    {
        free(rb);
        return NULL;
    }
    memset(rb->buf, 0, buf_size);
    // 初始化控制块与原子指针
    atomic_init(&rb->prod.head, 0);
    atomic_init(&rb->prod.tail, 0);
    atomic_init(&rb->cons.head, 0);
    atomic_init(&rb->cons.tail, 0);

    for (uint32_t i = 0; i < capacity; i++)
    {
        atomic_init(&rb->buf[i], NULL);
    }

    return rb;
}

// 对应的销毁函数
void mpmc_ring_destroy(mpmc_ring_t *rb)
{
    if (rb)
    {
        // aligned_alloc 分配的内存直接用标准 free 释放即可
        free(rb->buf);
        free(rb);
    }
}

// 入队函数（多生产者安全）
bool mpmc_ring_enqueue(mpmc_ring_t *rb, void *ptr)
{
    uint32_t head, next_head;
    uint32_t tail;

    // 依照 DPDK 规范将初始 load 放在循环外
    // 当 CAS 失败时，rb->prod.head 的最新值会自动写入 local 变量 head，无需在循环内重复 load
    head = atomic_load_explicit(&rb->prod.head, memory_order_relaxed);
    do
    {
        // 必须用 acquire 屏障，确保在读 buf 前，先看到消费侧 cons.tail 的最新释放
        tail = atomic_load_explicit(&rb->cons.tail, memory_order_acquire);

        // 精简后的无符号满判断：(head - tail) 真实反映了当前队列中的元素数量
        if ((head - tail) > rb->mask)
        {
            return false; // 队列已满
        }
        next_head = head + 1;

        // 遵循 Arm 官方为 DPDK 提交的 C11 优化：成功/失败全用 relaxed
        // 多个生产者之间仅单纯抢占槽位，不涉及任何数据同步，以此榨干弱内存序 CPU 的总线带宽
    } while (!atomic_compare_exchange_weak_explicit(
        &rb->prod.head, &head, next_head,
        memory_order_relaxed, memory_order_relaxed));

    // C11 标准原子写入，存在严格的地址依赖（取决于抢到的 head），绝无被编译器/CPU 向上重排的风险
    atomic_store_explicit(&rb->buf[head & rb->mask], ptr, memory_order_relaxed);

    // 严格串行化自旋：保证多线程并发写入时，tail 指令按严格递增的顺序隐退
    while (atomic_load_explicit(&rb->prod.tail, memory_order_relaxed) != head)
    {
#if defined(__x86_64__) || defined(_M_X64)
        __builtin_ia32_pause(); // x86 环境下防止流水线阻塞
#elif defined(__aarch64__)
        __asm__ __volatile__("yield" ::: "memory"); // ARM 环境下让出执行序
#endif
    }

    // 关键同步：用 release 确保上面 buf 写入的所有数据，对等待中的消费者产生 Happens-Before 关系
    atomic_store_explicit(&rb->prod.tail, next_head, memory_order_release);
    return true;
}

// 出队函数（多消费者安全）
bool mpmc_ring_dequeue(mpmc_ring_t *rb, void **ptr_out)
{
    uint32_t head, next_head;
    uint32_t tail;

    head = atomic_load_explicit(&rb->cons.head, memory_order_relaxed);
    do
    {
        // 必须用 acquire 屏障，确保看到生产侧已释放的最新 prod.tail 以及完整的 buf 写入数据
        tail = atomic_load_explicit(&rb->prod.tail, memory_order_acquire);

        if (head == tail)
        {
            return false; // 队列已空
        }
        next_head = head + 1;

    } while (!atomic_compare_exchange_weak_explicit(
        &rb->cons.head, &head, next_head,
        memory_order_relaxed, memory_order_relaxed));

    // 从缓冲区原子加载数据
    *ptr_out = atomic_load_explicit(&rb->buf[head & rb->mask], memory_order_relaxed);

    // 严格串行化自旋：保证消费尾指针的连续性
    while (atomic_load_explicit(&rb->cons.tail, memory_order_relaxed) != head)
    {
#if defined(__x86_64__) || defined(_M_X64)
        __builtin_ia32_pause();
#elif defined(__aarch64__)
        __asm__ __volatile__("yield" ::: "memory");
#endif
    }

    // 释放槽位：通过 release 通知等待中的生产者：该格子已彻底清空，可以安全复写
    atomic_store_explicit(&rb->cons.tail, next_head, memory_order_release);
    return true;
}