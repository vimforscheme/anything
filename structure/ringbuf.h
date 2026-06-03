#ifndef RINGBUF_H
#define RINGBUF_H

#include <stdint.h>
#include <stddef.h>
#include <assert.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* ─── 平台内存屏障宏 ─── */

#if defined(__x86_64__) || defined(__i386__)
#define RINGBUF_BARRIER() __asm__ __volatile__("" ::: "memory")
#elif defined(__aarch64__)
#define RINGBUF_BARRIER() __asm__ __volatile__("dmb ish" ::: "memory")
#elif defined(__arm__)
#define RINGBUF_BARRIER() __asm__ __volatile__("dmb" ::: "memory")
#elif defined(__mips__)
#define RINGBUF_BARRIER() __asm__ __volatile__("sync" ::: "memory")
#elif defined(__loongarch__)
#define RINGBUF_BARRIER() __asm__ __volatile__("dbar 0" ::: "memory")
#else
#include <stdatomic.h>
#define RINGBUF_BARRIER() atomic_thread_fence(memory_order_seq_cst)
#endif

    /* ─── 常量 ─── */

#define RINGBUF_CACHELINE 64

    /* ─── 数据结构 ─── */

#define RINGBUF_PAD0 (RINGBUF_CACHELINE - sizeof(void *) - sizeof(uint32_t))
#define RINGBUF_PAD1 (RINGBUF_CACHELINE - sizeof(uint32_t))
#define RINGBUF_PAD2 (RINGBUF_CACHELINE - sizeof(uint32_t))

    typedef struct
    {
        void **buf;
        uint32_t mask;
        char _pad0[RINGBUF_PAD0];

        uint32_t in;
        char _pad1[RINGBUF_PAD1];

        uint32_t out;
        char _pad2[RINGBUF_PAD2];
    } ringbuf_t;

    typedef char _ringbuf_layout_check[(sizeof(ringbuf_t) == RINGBUF_CACHELINE * 3) ? 1 : -1];

    /* ─── 初始化 ─── */

    static inline void ringbuf_init(ringbuf_t *rb, void **buf, uint32_t capacity)
    {
        assert(capacity > 0 && (capacity & (capacity - 1)) == 0);

        rb->buf = buf;
        rb->mask = capacity - 1;
        rb->in = 0;
        rb->out = 0;
    }

    /* ─── 容量/状态查询 ─── */

    static inline uint32_t ringbuf_capacity(const ringbuf_t *rb)
    {
        return rb->mask + 1;
    }

    static inline uint32_t ringbuf_count(const ringbuf_t *rb)
    {
        return rb->in - rb->out;
    }

    static inline uint32_t ringbuf_free_count(const ringbuf_t *rb)
    {
        return ringbuf_capacity(rb) - ringbuf_count(rb);
    }

    static inline int ringbuf_is_empty(const ringbuf_t *rb)
    {
        return rb->in == rb->out;
    }

    static inline int ringbuf_is_full(const ringbuf_t *rb)
    {
        return ringbuf_count(rb) >= ringbuf_capacity(rb);
    }

    /* ─── 核心操作 ─── */

    static inline int ringbuf_push(ringbuf_t *rb, void *ptr)
    {
        if (ringbuf_is_full(rb))
            return 0;

        rb->buf[rb->in & rb->mask] = ptr;
        RINGBUF_BARRIER();
        rb->in++;

        return 1;
    }

    static inline void *ringbuf_pop(ringbuf_t *rb)
    {
        if (ringbuf_is_empty(rb))
            return NULL;

        RINGBUF_BARRIER();
        void *ptr = rb->buf[rb->out & rb->mask];
        rb->out++;

        return ptr;
    }

    static inline void *ringbuf_peek(const ringbuf_t *rb)
    {
        if (ringbuf_is_empty(rb))
            return NULL;

        RINGBUF_BARRIER();
        void *ptr = rb->buf[rb->out & rb->mask];

        return ptr;
    }

#ifdef __cplusplus
}
#endif

#endif /* RINGBUF_H */
