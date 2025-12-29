/* ============================================================================
 * aligned_def.h
 *
 * 目的:
 *   提供跨平台、跨编译器的“扩大对齐（Over-alignment）”宏定义。
 *
 * 适用场景:
 *   - cache line 对齐（避免 false sharing）
 *   - SIMD / DMA / 原子操作对齐要求
 *   - lock-free 数据结构
 *
 * 不适用场景:
 *   - 网络协议 / 磁盘格式（应使用 packed）
 *   - 节省内存为首要目标的结构
 *
 * 重要说明:
 *   - 对齐值必须是 2 的幂
 *   - 对齐的是“对象起始地址”，不是内部字段布局
 *   - 扩大对齐可能增加内存占用（空间换时间）
 *   - 实际布局最终由编译器 + ABI 决定
 * ========================================================================== */

#ifndef ALIGNED_DEF_H
#define ALIGNED_DEF_H

/* --------------------------------------------------------------------------
 * 编译器/标准能力探测
 * -------------------------------------------------------------------------- */



 /* *  C++11 或更高版本 
 * C++11 原生支持 alignas 关键字，无需头文件
 */
#if defined(__cplusplus) && (__cplusplus >= 201103L)
    #define ALIGNED_PRE(x)  alignas(x)
    #define ALIGNED_POST(x)

/* *  C11 标准 (C语言)
 */
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)

    #include <stdalign.h>
    #define ALIGNED_PRE(x)  alignas(x)
    #define ALIGNED_POST(x)

/*
 * MSVC:
 *   - 使用 __declspec(align(x))
 *   - 必须放在类型之前
 */
#elif defined(_MSC_VER)

    #define ALIGNED_PRE(x)  __declspec(align(x))
    #define ALIGNED_POST(x)

/*
 * GCC / Clang / LLVM:
 *   - 推荐使用 __attribute__((aligned))
 *   - 通常放在变量名或结构体定义结尾
 */
#elif defined(__GNUC__) || defined(__clang__)

    #define ALIGNED_PRE(x)
    #define ALIGNED_POST(x) __attribute__((aligned(x)))

/*
 * 兜底:
 *   - 编译器不支持自定义对齐
 *   - 允许编译通过，但不保证对齐效果
 */
#else

    #define ALIGNED_PRE(x)
    #define ALIGNED_POST(x)
    #pragma message("Warning: Custom alignment is not supported by this compiler.")

#endif

/* --------------------------------------------------------------------------
 * 使用示例 (仅示意):
 *
 *   ALIGNED_PRE(64)
 *   struct Foo {
 *       int a;
 *       int b;
 *   } ALIGNED_POST(64);
 *
 *   ALIGNED_PRE(32) int counter ALIGNED_POST(32);
 * -------------------------------------------------------------------------- */

#endif /* ALIGNED_DEF_H */
