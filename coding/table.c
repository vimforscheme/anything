#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ==========================================
 * 1. 基础定义
 * ========================================== */

// 模拟的消息类型 (0 ~ 255)
typedef enum {
    MSG_PING      = 0,
    MSG_LOGIN     = 1,
    MSG_HEARTBEAT = 2,
    MSG_DATA      = 3,
    MSG_ADMIN_CMD = 4,
    // ... 假设中间有很多空洞 ...
    MSG_LOGOUT    = 10
} msg_type_t;

// 模拟的数据包结构
typedef struct {
    uint8_t type;
    uint8_t is_admin; // 发送者是否是管理员
    uint16_t len;
    char payload[1024];
} packet_t;

// 定义函数指针类型：处理函数的统一接口
typedef int (*handler_func)(packet_t *pkt);

/* ==========================================
 * 2. 核心结构体 (这才是表驱动的灵魂)
 * ========================================== */

// 标志位：用位掩码控制行为，比写一堆 bool 变量更紧凑
#define FLG_ADMIN_ONLY  (1 << 0)  // 需要管理员权限
#define FLG_LOG_STATS   (1 << 1)  // 需要记录统计日志

typedef struct {
    const char *name;       // [元数据] 消息名称，用于打印日志
    handler_func handler;   // [动作]   回调函数
    uint16_t min_len;       // [规则]   最小负载长度 (自动校验用)
    uint8_t flags;          // [策略]   权限位、日志开关等
} msg_handler_t;

/* ==========================================
 * 3. 具体的业务回调函数
 * ========================================== */

int handle_ping(packet_t *pkt) {
    printf(">> PONG!\n");
    return 0;
}

int handle_login(packet_t *pkt) {
    printf(">> User login processing: %s\n", pkt->payload);
    return 0;
}

int handle_admin(packet_t *pkt) {
    printf(">> !!! ADMIN COMMAND EXECUTED !!!\n");
    return 0;
}

/* ==========================================
 * 4. 定义分发表 (The Dispatch Table)
 * ========================================== */

// 技巧：使用 C99 的 [INDEX] = { ... } 指定初始化
// 这样即使枚举顺序变了，或者中间有空洞，表也不会错位！
static const msg_handler_t dispatch_table[256] = {
    [MSG_PING] = {
        .name = "PING",
        .handler = handle_ping,
        .min_len = 0,
        .flags = 0  // 谁都可以 ping，不记录日志
    },
    [MSG_LOGIN] = {
        .name = "LOGIN",
        .handler = handle_login,
        .min_len = 4, // 用户名至少4字节
        .flags = FLG_LOG_STATS
    },
    [MSG_ADMIN_CMD] = {
        .name = "ADMIN",
        .handler = handle_admin,
        .min_len = 0,
        .flags = FLG_ADMIN_ONLY | FLG_LOG_STATS // 既要权限，又要日志
    },
    // 没有定义的项，默认为 NULL/0 (C语言特性)
};

/* ==========================================
 * 5. 驱动引擎 (Driver Engine)
 * ========================================== */

void process_packet(packet_t *pkt) {
    // A. 边界检查 (极其重要！防止数组越界攻击)
    // 因为 pkt->type 是 uint8_t，天然 < 256，但如果是 int 必须检查
    uint8_t type = pkt->type;
    
    // B. 获取表项 (O(1) 瞬间定位)
    // 这里的指针指向常量区，速度极快
    const msg_handler_t *entry = &dispatch_table[type];

    // C. 统一的前置检查 (Middleware Logic)
    
    // 1. 检查是否存在处理函数 (处理空洞)
    if (entry->handler == NULL) {
        printf("[Error] Unknown Msg Type: %d\n", type);
        return;
    }

    // 2. 长度自动校验 (数据驱动逻辑)
    if (pkt->len < entry->min_len) {
        printf("[Drop] %s packet too short. Need %d, got %d\n", 
               entry->name, entry->min_len, pkt->len);
        return;
    }

    // 3. 权限自动校验 (消除了函数内部的重复 if 判断)
    if ((entry->flags & FLG_ADMIN_ONLY) && !pkt->is_admin) {
        printf("[Auth] Permission Denied for %s\n", entry->name);
        return;
    }

    // D. 执行业务逻辑
    if (entry->flags & FLG_LOG_STATS) {
        printf("[Log] Processing %s...\n", entry->name);
    }
    
    int ret = entry->handler(pkt);

    // E. 统一错误处理
    if (ret != 0) {
        printf("[Fail] Handler returned error: %d\n", ret);
    }
}

/* ==========================================
 * 6. 测试
 * ========================================== */

int main() {
    // 场景 1: 普通用户发 Ping
    packet_t p1 = { .type = MSG_PING, .is_admin = 0, .len = 0 };
    process_packet(&p1);

    // 场景 2: 普通用户想发 Admin 命令 (应该被拒)
    packet_t p2 = { .type = MSG_ADMIN_CMD, .is_admin = 0, .len = 10 };
    process_packet(&p2);

    // 场景 3: 管理员发 Admin 命令 (应该成功 + 有日志)
    packet_t p3 = { .type = MSG_ADMIN_CMD, .is_admin = 1, .len = 10 };
    process_packet(&p3);

    // 场景 4: 未知的消息类型 (应该报错)
    packet_t p4 = { .type = 99, .is_admin = 0 };
    process_packet(&p4);
    
    return 0;
}