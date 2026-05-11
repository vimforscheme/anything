#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#define offsetof_custom(TYPE, MEMBER) ((size_t)&((TYPE *)0)->MEMBER)
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof_custom(type, member)))

typedef struct
{
    uint32_t node_id; // 可以放一些调试用的元数据，或者干脆为空
} rb_node_t;

typedef struct
{
    uint32_t timestamp;
    float huge_sensor_data[1024]; // 假设这是 4KB 的巨大数据，拷贝极慢！

    rb_node_t hook; // 预留的挂钩
} BigSensorFrame_t;

typedef struct
{
    rb_node_t **buffer;   // 存放 rb_node_t 的指针
    uint32_t size;        // 容量
    uint32_t write_idx;   // 写指针
    uint32_t read_idx;    // 读指针
    uint8_t write_mirror; // 写翻转位
    uint8_t read_mirror;  // 读翻转位
} IntrusiveRingBuffer_t;

// ==========================================
// 3. Ring Buffer 逻辑实现 (保留了读写翻转位)
// size必须是2的幂
// ==========================================

bool irb_init(IntrusiveRingBuffer_t *rb, uint32_t size)
{
    // 分配的是指针的数组 (每个元素占 4 或 8 字节)
    rb->buffer = (rb_node_t **)malloc(size * sizeof(rb_node_t *));
    if (!rb->buffer)
        return false;

    rb->size = size;
    rb->write_idx = 0;
    rb->read_idx = 0;
    rb->write_mirror = 0;
    rb->read_mirror = 0;
    return true;
}

static inline bool irb_is_empty(IntrusiveRingBuffer_t *rb)
{
    // 空：索引相同，翻转位也相同
    return (rb->write_idx == rb->read_idx) &&
           (rb->write_mirror == rb->read_mirror);
}

static inline bool irb_is_full(IntrusiveRingBuffer_t *rb)
{
    // 满：索引相同，但翻转位不同
    return (rb->write_idx == rb->read_idx) &&
           (rb->write_mirror != rb->read_mirror);
}

// 入队：只传递节点的指针，绝对零拷贝 (Zero-copy)
bool irb_push(IntrusiveRingBuffer_t *rb, rb_node_t *node)
{
    if (irb_is_full(rb))
        return false;

    rb->buffer[rb->write_idx] = node; // 只发生了 8 字节的指针赋值！
    rb->write_idx++;

    if (rb->write_idx == rb->size)
    {
        rb->write_idx = 0;
        rb->write_mirror ^= 1;
    }
    return true;
}

// 出队：弹出节点的指针
bool irb_pop(IntrusiveRingBuffer_t *rb, rb_node_t **node_out)
{
    if (irb_is_empty(rb))
        return false;

    *node_out = rb->buffer[rb->read_idx]; // 获取指针
    rb->read_idx++;

    if (rb->read_idx == rb->size)
    {
        rb->read_idx = 0;
        rb->read_mirror ^= 1;
    }
    return true;
}

// ==========================================
// 4. 测试用例：见证奇迹的时刻
// ==========================================
int main()
{
    IntrusiveRingBuffer_t tx_ring;
    irb_init(&tx_ring, 16);

    // 1. 生产者：准备数据 (通常在内存池里分配)
    BigSensorFrame_t frame1;
    frame1.timestamp = 1689000123;
    frame1.huge_sensor_data[0] = 3.14f;
    frame1.hook.node_id = 1;

    // 2. 生产者入队：我们只把帧内部的 hook 的地址传进去
    printf("[Producer] Pushing frame timestamp: %u\n", frame1.timestamp);
    irb_push(&tx_ring, &frame1.hook);

    // ----------------------------------------

    // 3. 消费者出队
    rb_node_t *out_node = NULL;
    if (irb_pop(&tx_ring, &out_node))
    {

        // 4. 重点来了！通过挂钩指针，利用 container_of 反推出真实的数据首地址
        BigSensorFrame_t *received_frame = container_of(out_node, BigSensorFrame_t, hook);

        printf("[Consumer] Popped hook ID: %u\n", out_node->node_id);
        printf("[Consumer] Recovered timestamp: %u\n", received_frame->timestamp);
        printf("[Consumer] Recovered sensor data: %f\n", received_frame->huge_sensor_data[0]);
    }

    return 0;
}