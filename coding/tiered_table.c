#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <time.h>

/* ==========================================
 * 1. 公共数据结构 (完全一致)
 * ========================================== */
typedef struct {
    uint32_t limit;  // 阈值 (Upper Bound)
    int      id;     // 属性 ID
    const char *desc;
} step_rule_t;

/* * 规则表：必须有序 (从小到大)
 * 最后一个 UINT32_MAX 是兜底，防止越界
 */
static const step_rule_t rules[] = {
    { 100,   1, "Low"      }, // [0, 100)
    { 500,   2, "Medium"   }, // [100, 500)
    { 1000,  3, "High"     }, // [500, 1000)
    { 5000,  4, "Critical" }, // [1000, 5000)
    { UINT32_MAX, 5, "Explosion"} // [5000, MAX]
};

#define RULES_COUNT (sizeof(rules) / sizeof(rules[0]))

/* ==========================================
 * 2. 方式 A：普通阶梯表 (线性扫描)
 * ========================================== */
// 特点：简单、直观、适合小表 (CPU 预取友好)
const step_rule_t* get_rule_linear(uint32_t val) {
    // 直接从头遍历到尾
    for (int i = 0; i < RULES_COUNT; i++) {
        // 只要遇到第一个“界限比我大”的，就是它了
        if (val < rules[i].limit) {
            return &rules[i];
        }
    }
    // 理论上不会走到这里，因为有 UINT32_MAX 兜底
    return &rules[RULES_COUNT - 1];
}

/* ==========================================
 * 3. 方式 B：二分阶梯表 (二分查找)
 * ========================================== */
// 特点：逻辑稍杂、适合大表 (跳跃式查找)
const step_rule_t* get_rule_bsearch(uint32_t val) {
    int left = 0;
    int right = RULES_COUNT - 1;
    int ans = RULES_COUNT - 1; // 默认命中兜底

    while (left <= right) {
        int mid = left + (right - left) / 2;

        if (val < rules[mid].limit) {
            // 这是一个潜在的解，但不一定是“第一个”
            // 先记下来，然后往左边尝试找更小的 limit
            ans = mid;
            right = mid - 1;
        } else {
            // val 比当前 limit 还大，说明肯定在右边
            left = mid + 1;
        }
    }
    return &rules[ans];
}

/* ==========================================
 * 4. 测试与验证
 * ========================================== */
int main() {
    uint32_t test_vals[] = {50, 499, 6000};
    
    printf("--- Linear Search (普通) ---\n");
    for (int i = 0; i < 3; i++) {
        const step_rule_t *r = get_rule_linear(test_vals[i]);
        printf("Val: %-4u -> %s\n", test_vals[i], r->desc);
    }

    printf("\n--- Binary Search (二分) ---\n");
    for (int i = 0; i < 3; i++) {
        const step_rule_t *r = get_rule_bsearch(test_vals[i]);
        printf("Val: %-4u -> %s\n", test_vals[i], r->desc);
    }

    return 0;
}