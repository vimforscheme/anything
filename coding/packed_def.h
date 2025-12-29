/* ============================================================================
 * packed_def.h
 *
 * 目的:
 *   提供跨平台、跨编译器的“结构体压缩（Packed Layout）”宏定义。
 *
 * 适用场景:
 *   - 网络协议头
 *   - 磁盘 / 文件 / 线缆格式
 *   - 明确定义的二进制数据布局
 *
 * 不适用场景:
 *   - 高频访问的内部数据结构
 *   - 需要原子访问 / SIMD / cache 友好的结构
 *
 * 重要风险:
 *   - packed 结构体可能产生未对齐访问
 *   - 在部分架构（ARM/MIPS 等）上可能:
 *       * 性能显著下降
 *       * 触发 SIGBUS
 *   - 推荐做法:
 *       packed 用于“表示”，自然对齐结构用于“计算”
 * ========================================================================== */

#ifndef PACKED_DEF_H
#define PACKED_DEF_H

/* --------------------------------------------------------------------------
 * Microsoft Visual C++ (MSVC)
 *
 * 说明:
 *   - MSVC 不支持 __attribute__((packed))
 *   - 只能通过 #pragma pack 控制结构体整体布局
 *   - 使用 push/pop 防止污染外部结构体
 * -------------------------------------------------------------------------- */
#if defined(_MSC_VER)

    #define PACKED_STRUCT_BEGIN __pragma(pack(push, 1))
    #define PACKED_STRUCT_END   __pragma(pack(pop))

    /* MSVC 无法对单个字段使用 packed attribute */
    #define PACKED_FIELD(x) x

/* --------------------------------------------------------------------------
 * GCC / Clang / LLVM / ARMCC
 *
 * 说明:
 *   - 使用 __attribute__((packed))
 *   - packed 放在结构体结尾分号之前
 * -------------------------------------------------------------------------- */
#elif defined(__GNUC__) || defined(__clang__)

    #define PACKED_STRUCT_BEGIN
    #define PACKED_STRUCT_END   __attribute__((packed))

    /* 支持字段级 packed（一般不推荐） */
    #define PACKED_FIELD(x) x __attribute__((packed))

/* --------------------------------------------------------------------------
 * 兜底编译器
 *
 * 说明:
 *   - 不保证结构体真实压缩
 *   - 编译可通过，但协议解析可能不正确
 * -------------------------------------------------------------------------- */
#else

    #define PACKED_STRUCT_BEGIN
    #define PACKED_STRUCT_END
    #define PACKED_FIELD(x) x
    #pragma message("Warning: Structure packing is not supported by this compiler.")

#endif

/* --------------------------------------------------------------------------
 * 使用示例:
 *
 *   PACKED_STRUCT_BEGIN
 *   struct ProtoHeader {
 *       uint8_t  version;
 *       uint16_t length;
 *       uint32_t id;
 *   } PACKED_STRUCT_END;
 *
 *   // 推荐:
 *   //   1. memcpy 到自然对齐结构
 *   //   2. 再进行逻辑计算
 * -------------------------------------------------------------------------- */

#endif /* PACKED_DEF_H */
