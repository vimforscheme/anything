#define _GNU_SOURCE
#include "ringbuf.h"
#include "objpool.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>

#define POOL_CAPACITY 1024
#define TOTAL_ITEMS   (5 * 1000 * 1000)

/* 跨平台 CPU relax 指令，减少 spin-wait 时的功耗和总线争用 */
#if defined(__x86_64__) || defined(__i386__)
#define CPU_RELAX() __asm__ __volatile__("pause" ::: "memory")
#elif defined(__aarch64__)
#define CPU_RELAX() __asm__ __volatile__("yield" ::: "memory")
#else
#define CPU_RELAX() /* noop */
#endif

typedef struct {
    uint64_t seq;
    uint64_t ts;
} packet_t;

typedef struct {
    ringbuf_t  rb;
    objpool_t  pool;
    atomic_int running;
} spsc_ctx_t;

static int pin_to_core(int core_id)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}

static inline uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static void *producer_thread(void *arg)
{
    spsc_ctx_t *ctx = (spsc_ctx_t *)arg;
    pin_to_core(0);

    uint64_t start = rdtsc();

    for (uint64_t i = 0; i < TOTAL_ITEMS; i++) {
        packet_t *pkt;

        /* 1. 从对象池 acquire */
        while ((pkt = (packet_t *)objpool_acquire(&ctx->pool)) == NULL)
            CPU_RELAX();

        /* 2. 填充数据 */
        pkt->seq = i;
        pkt->ts  = rdtsc();

        /* 3. push 到 ringbuf */
        while (ringbuf_push(&ctx->rb, pkt) == 0)
            CPU_RELAX();
    }

    uint64_t end   = rdtsc();
    uint64_t cycles = end - start;
    printf("[producer] %u items  |  %.0f cycles/item  |  %.1f ns/item @3GHz\n",
           TOTAL_ITEMS, (double)cycles / TOTAL_ITEMS,
           (double)cycles / TOTAL_ITEMS / 3.0);

    ctx->running = 0;
    return NULL;
}

static void *consumer_thread(void *arg)
{
    spsc_ctx_t *ctx = (spsc_ctx_t *)arg;
    pin_to_core(1);

    uint64_t received = 0;
    uint64_t expected = 0;
    uint64_t reorder  = 0;
    uint64_t start    = rdtsc();

    for (;;) {
        packet_t *pkt;

        /* 1. 从 ringbuf pop */
        while ((pkt = (packet_t *)ringbuf_pop(&ctx->rb)) == NULL) {
            if (!ctx->running && ringbuf_is_empty(&ctx->rb))
                goto done;
            CPU_RELAX();
        }

        /* 2. 数据校验 */
        if (pkt->seq != expected) {
            reorder++;
            expected = pkt->seq + 1;
        } else {
            expected++;
        }
        received++;

        /* 3. 归还对象 */
        while (objpool_release(&ctx->pool, pkt) == 0)
            CPU_RELAX();
    }

done:
    uint64_t end   = rdtsc();
    uint64_t cycles = end - start;
    printf("[consumer] %lu items  |  %.0f cycles/item  |  %.1f ns/item @3GHz\n",
           received, (double)cycles / received,
           (double)cycles / received / 3.0);
    if (reorder)
        printf("[consumer] WARNING: %lu reorder events\n", reorder);
    else
        printf("[consumer] sequence: all in order ✓\n");

    if (received != TOTAL_ITEMS)
        printf("[consumer] WARNING: expected %u, got %lu\n", TOTAL_ITEMS, received);

    return NULL;
}

int main(void)
{
    void     *slots[POOL_CAPACITY];
    packet_t  slab[POOL_CAPACITY];
    void     *rb_buf[POOL_CAPACITY];

    spsc_ctx_t ctx;
    ctx.running = 1;

    ringbuf_init(&ctx.rb, rb_buf, POOL_CAPACITY);
    objpool_init(&ctx.pool, slots, slab, POOL_CAPACITY, sizeof(packet_t));

    printf("=== SPSC Zero-Copy Multi-Threaded Benchmark ===\n");
    printf("pool: %d objects  |  total: %u items  |  obj size: %zu bytes\n",
           POOL_CAPACITY, TOTAL_ITEMS, sizeof(packet_t));
    printf("producer → core 0  |  consumer → core 1\n\n");

    uint64_t t0 = rdtsc();

    pthread_t prod, cons;
    pthread_create(&cons, NULL, consumer_thread, &ctx);
    pthread_create(&prod, NULL, producer_thread, &ctx);

    pthread_join(prod, NULL);
    pthread_join(cons, NULL);

    uint64_t t1 = rdtsc();
    double total_ms = (double)(t1 - t0) / 3000000.0;
    double throughput = TOTAL_ITEMS / total_ms * 1000.0;

    uint32_t avail = objpool_available(&ctx.pool);
    printf("\nwall clock: %.1f ms  |  throughput: %.0f items/s\n",
           total_ms, throughput);
    printf("pool integrity: %u/%u %s\n",
           avail, POOL_CAPACITY,
           avail == POOL_CAPACITY ? "PASSED ✓" : "FAILED ✗");

    return avail == POOL_CAPACITY ? 0 : 1;
}
