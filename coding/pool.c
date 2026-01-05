#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>
#include <assert.h>
//gcc -O2 -DNDEBUG t6.c -o a.out
/* ==========================================
 * 配置与宏定义
 * ========================================== */
#define OBJECT_SIZE           2048
#define LOCAL_CACHE_CAPACITY  512
#define BATCH_SIZE            32
#define CACHE_LINE            64

//向上取整 让x成为a的倍数，a必须是2的幂
#define ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((a) - 1))
//是否是2的幂
#define IS_POWER_OF_2(x) ((x) != 0 && (((x) & ((x) - 1)) == 0))
//数组元素个数
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
//获取最大数值
#define DIV_ROUND_UP(n, d)  (((n) + (d) - 1) / (d))
//向下截取 x是a的倍数 a必须是2的幂
#define ALIGN_DOWN(x, a)  ((x) & ~((a) - 1))

#define offsetof(type, member) ((size_t) &((type *)0)->member)

#define container_of(ptr, type, member) ({          \
    const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
    (type *)( (char *)__mptr - offsetof(type,member) );})

/* ==========================================
 * 数据结构
 * ========================================== */

typedef struct free_node {
    struct free_node *next;
} free_node_t;

typedef struct {
    pthread_spinlock_t lock;
    free_node_t *free_head;

    pthread_mutex_t wait_lock;
    pthread_cond_t  wait_cond;
    int             waiters;

    void   *mmap_base;
    size_t  mmap_size;
} __attribute__((aligned(64))) global_pool_t;

typedef struct {
    void *objects[LOCAL_CACHE_CAPACITY];
    int count;
    global_pool_t *global;

    long long alloc_cnt;
    long long free_cnt;
    long long wait_cnt;
} thread_cache_t;

/* ==========================================
 * TLS / pthread key
 * ========================================== */

static __thread thread_cache_t *t_cache = NULL;
static pthread_key_t cleanup_key;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;

/* ==========================================
 * 线程清理
 * ========================================== */
static void thread_cleanup_handler(void *arg) {
    // 1. 强转 arg，这是正统做法
    thread_cache_t *tc = (thread_cache_t *)arg;

    // 防御性检查：如果 Pthread 传空指针进来（虽然不应该），或者 count 为 0
    if (!tc || tc->count == 0)
        goto out;

    global_pool_t *pool = tc->global; // 这里的 tc 替代了 t_cache

    // 2. 下面的逻辑全部操作 tc
    free_node_t *head = (free_node_t *)tc->objects[0];
    free_node_t *cur  = head;

    for (int i = 1; i < tc->count; i++) {
        cur->next = (free_node_t *)tc->objects[i];
        cur = cur->next;
    }
    cur->next = NULL;

    pthread_spin_lock(&pool->lock);
    bool need_signal = (pool->free_head == NULL);
    cur->next = pool->free_head;
    pool->free_head = head;
    pthread_spin_unlock(&pool->lock);

    if (need_signal) {
        pthread_mutex_lock(&pool->wait_lock);
        if (pool->waiters > 0)
            pthread_cond_broadcast(&pool->wait_cond);
        pthread_mutex_unlock(&pool->wait_lock);
    }

out:
    // 3. 释放内存
    if (tc) free(tc);
    
    // 4. 把全局 TLS 置空，防止悬垂指针（虽然线程马上要销毁了，但这是好习惯）
    t_cache = NULL; 
}

static void make_cleanup_key(void) {
    pthread_key_create(&cleanup_key, thread_cleanup_handler);
}

/* ==========================================
 * 核心逻辑：Flush / Refill
 * ========================================== */

static void cache_flush(void) {
    global_pool_t *pool = t_cache->global;

    int target = LOCAL_CACHE_CAPACITY / 2;
    int n = t_cache->count - target;
    if (n <= 0) return;

    int start = t_cache->count - n;

    free_node_t *head = (free_node_t *)t_cache->objects[start];
    free_node_t *cur  = head;

    for (int i = 1; i < n; i++) {
        cur->next = (free_node_t *)t_cache->objects[start + i];
        cur = cur->next;
    }

    /* FIX-5: 明确断尾 */
    cur->next = NULL;

    pthread_spin_lock(&pool->lock);
    bool need_signal = (pool->free_head == NULL);
    cur->next = pool->free_head;
    pool->free_head = head;
    pthread_spin_unlock(&pool->lock);

    t_cache->count = target;

    if (need_signal) {
        pthread_mutex_lock(&pool->wait_lock);
        if (pool->waiters > 0)
            pthread_cond_broadcast(&pool->wait_cond);
        pthread_mutex_unlock(&pool->wait_lock);
    }
}

static void cache_refill(void) {
    global_pool_t *pool = t_cache->global;

    pthread_spin_lock(&pool->lock);

    while (pool->free_head == NULL) {
        pthread_spin_unlock(&pool->lock);

        t_cache->wait_cnt++;

        pthread_mutex_lock(&pool->wait_lock);
        pool->waiters++;
        pthread_cond_wait(&pool->wait_cond, &pool->wait_lock);
        pool->waiters--;
        pthread_mutex_unlock(&pool->wait_lock);

        pthread_spin_lock(&pool->lock);
    }

    int fetched = 0;
    while (fetched < BATCH_SIZE && pool->free_head) {
        free_node_t *n = pool->free_head;
        pool->free_head = n->next;
        t_cache->objects[t_cache->count++] = n;
        fetched++;
    }

    pthread_spin_unlock(&pool->lock);
}



void mp_thread_init(global_pool_t *global) {
    pthread_once(&key_once, make_cleanup_key);

    if (t_cache) return;

    t_cache = calloc(1, sizeof(thread_cache_t));
    t_cache->global = global;

    int ret = pthread_setspecific(cleanup_key, t_cache);
    if (ret != 0) {
        fprintf(stderr, "pthread_setspecific failed: %d\n", ret);
        abort();
    }
}

/* ==========================================
 * Pool API
 * ========================================== */

global_pool_t *mp_pool_create(int count) {
    pthread_once(&key_once, make_cleanup_key);

    size_t sz = ALIGN_UP(sizeof(global_pool_t), CACHE_LINE); /* FIX-6 */
    global_pool_t *pool = aligned_alloc(CACHE_LINE, sz);

    pthread_spin_init(&pool->lock, PTHREAD_PROCESS_PRIVATE);
    pthread_mutex_init(&pool->wait_lock, NULL);
    pthread_cond_init(&pool->wait_cond, NULL);
    pool->waiters = 0;

    size_t obj_sz = ALIGN_UP(OBJECT_SIZE, CACHE_LINE);
    size_t total  = obj_sz * count;

    void *base = mmap(NULL, total, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    pool->mmap_base = base;
    pool->mmap_size = total;
    pool->free_head = NULL;

    uint8_t *p = base;
    for (int i = 0; i < count; i++) {
        free_node_t *n = (free_node_t *)p;
        n->next = pool->free_head;
        pool->free_head = n;
        p += obj_sz;
    }

    return pool;
}

void mp_pool_destroy(global_pool_t *pool) {
    if (!pool) return;

    munmap(pool->mmap_base, pool->mmap_size);
    pthread_spin_destroy(&pool->lock);
    pthread_mutex_destroy(&pool->wait_lock);
    pthread_cond_destroy(&pool->wait_cond);
    free(pool);
}

void *mp_alloc(void) {

    assert(t_cache != NULL && "Did you forget mp_thread_init?");

    if (t_cache->count > 0) {
        t_cache->alloc_cnt++;
        return t_cache->objects[--t_cache->count];
    }

    cache_refill();
    t_cache->alloc_cnt++;
    return (t_cache->count > 0) ? t_cache->objects[--t_cache->count] : NULL;
}

void mp_free(void *ptr) {
    if (!ptr) return;

    assert(t_cache != NULL && "Did you forget mp_thread_init?");

    t_cache->free_cnt++;

    if (t_cache->count < LOCAL_CACHE_CAPACITY) {
        t_cache->objects[t_cache->count++] = ptr;
        return;
    }

    cache_flush();
    t_cache->objects[t_cache->count++] = ptr;
}

/* ==========================================
 * Benchmark
 * ========================================== */

#define TEST_THREADS 4
#define TOTAL_OPS    5000000
#define POOL_SIZE    100000

void *worker(void *arg) {
    mp_thread_init(arg);

    for (int i = 0; i < TOTAL_OPS; i++) {
        void *p = mp_alloc();
        *(int *)p = i;
        mp_free(p);
    }
    return NULL;
}

int main(void) {
    global_pool_t *pool = mp_pool_create(POOL_SIZE);

    pthread_t th[TEST_THREADS];
    struct timespec s, e;
    clock_gettime(CLOCK_MONOTONIC, &s);

    for (int i = 0; i < TEST_THREADS; i++)
        pthread_create(&th[i], NULL, worker, pool);
    for (int i = 0; i < TEST_THREADS; i++)
        pthread_join(th[i], NULL);

    clock_gettime(CLOCK_MONOTONIC, &e);
    double sec = (e.tv_sec - s.tv_sec) +
                 (e.tv_nsec - s.tv_nsec) / 1e9;

    printf("Time: %.3f s\n", sec);
    printf("Rate: %.2f Mops/s\n",
           (TEST_THREADS * TOTAL_OPS * 2.0) / 1e6 / sec);

    mp_pool_destroy(pool);
    return 0;
}
