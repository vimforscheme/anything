#ifndef OBJPOOL_H
#define OBJPOOL_H

#include <stdint.h>
#include <stddef.h>
#include <assert.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* ─── 数据结构 ─── */

    typedef struct
    {
        void **slots;
        void *slab;
        uint32_t esize;
        uint32_t mask;
        uint32_t in;
        uint32_t out;
    } objpool_t;

    /* ─── 初始化 ─── */

    static inline void objpool_init(objpool_t *pool, void **slots, void *slab,
                                    uint32_t capacity, uint32_t esize)
    {
        assert(capacity > 0 && (capacity & (capacity - 1)) == 0);

        pool->slots = slots;
        pool->slab = slab;
        pool->esize = esize;
        pool->mask = capacity - 1;
        pool->in = capacity;
        pool->out = 0;

        uint8_t *base = (uint8_t *)slab;
        uint32_t i;
        for (i = 0; i < capacity; i++)
            slots[i] = base + (size_t)i * esize;
    }

    /* ─── 容量/状态查询 ─── */

    static inline uint32_t objpool_capacity(const objpool_t *pool)
    {
        return pool->mask + 1;
    }

    static inline uint32_t objpool_available(const objpool_t *pool)
    {
        return pool->in - pool->out;
    }

    static inline int objpool_is_empty(const objpool_t *pool)
    {
        return pool->in == pool->out;
    }

    static inline int objpool_is_full(const objpool_t *pool)
    {
        return objpool_available(pool) >= objpool_capacity(pool);
    }

    /* ─── 核心操作 ─── */

    static inline void *objpool_acquire(objpool_t *pool)
    {
        if (objpool_is_empty(pool))
            return NULL;

        RINGBUF_BARRIER();
        void *ptr = pool->slots[pool->out & pool->mask];
        pool->out++;

        return ptr;
    }

    static inline int objpool_release(objpool_t *pool, void *obj)
    {
        if (objpool_is_full(pool))
        {
            assert(0);
            return 0;
        }

        pool->slots[pool->in & pool->mask] = obj;
        RINGBUF_BARRIER();
        pool->in++;

        return 1;
    }

#ifdef __cplusplus
}
#endif

#endif /* OBJPOOL_H */
