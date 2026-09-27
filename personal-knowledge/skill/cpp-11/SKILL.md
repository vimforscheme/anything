---
name: cpp-11
description: >
  C++11 工程代码的编写、审查与热路径约束。修改 .h/.hpp/.cpp/.cc/.cxx，
  审查或生成 C/C++，设计 API、所有权、生命周期、并发或内存序，
  以及判断 C++11/14/17 特性能否使用时启用。默认 -std=c++11。
  热路径性能规则只在任务已明确是报文路径或零拷贝路径时启用。
---

# C++ Coding Skill (C++11 基线，C++14/17 例外启用)

## 何时使用

任务涉及以下内容时启用本 Skill：

- 修改 `.h` / `.hpp` / `.cpp` / `.cc` / `.cxx`
- 审查、重构、生成 C/C++ 代码
- 设计 C++ API、类、资源管理、并发
- 判断 C++11 / C++14 / C++17 特性是否可用

按需打开同目录文件。编号规则的正文只在本文件：

| 文件 | 何时打开 |
| --- | --- |
| `cpp-style.md` | 编写新代码，需要命名、初始化、类、头文件写法 |
| `cpp-review.md` | 改完 C/C++ 后逐项自检 |
| `cpp-performance.md` | 任务说明或既有注释已确认是热路径、报文路径或零拷贝路径 |

普通业务、界面、启动和配置代码不打开 `cpp-performance.md`。不得把普通函数猜测成热路径。

## 总原则

1. 默认目标标准：C++11。
2. 代码必须能在 `-std=c++11` 下编译通过，除非文件顶部明确标记使用 C++14/17。
3. C++14/17 是例外启用，不是默认。
4. 规则优先级：`MUST > SHOULD > MAY > REVIEW`。
5. 违反 `MUST` 必须修改。违反 `SHOULD` 必须在修改说明里写明理由。使用 `MAY` 必须满足文末条件。`REVIEW` 是改完代码后的自检清单。
6. 不得猜测 ownership、lifetime、atomic memory order、ABI 边界、性能意图，或某段代码是不是热路径。

## C++ 标准边界

### 严格 C++11 基线

C++11 的语言和标准库都可以用。下面是优先使用的特性，不是许可名单。没出现在这里的 C++11 特性仍然可用，例如 `decltype`、尾置返回类型、委托构造、`std::array`、`std::tuple`、`std::tie`、`std::chrono`、`std::begin` / `std::end`。

优先使用：

- `auto`（局部变量；C++11 lambda 仅有一条 `return` 时可省略返回类型）
- 范围 for
- 统一初始化 `{}`（见 `cpp-style.md` 对 `initializer_list` 的限制）
- `nullptr`
- `enum class`
- `unique_ptr` / `shared_ptr` / `weak_ptr`
- `make_shared`
- Lambda
- `constexpr`（C++11：函数体只能有一条 `return`，不能写局部变量或循环）
- 移动语义。非模板的 `T&&` 是右值引用；模板参数 `T&&` 和 `auto&&` 是转发引用
- `override` / `final`
- `= default` / `= delete`
- `static_assert`
- `decltype`、尾置返回类型 `auto f() -> decltype(expr)`
- 变参模板
- `using` 类型别名
- `std::tie` / `std::get`（解包 `pair` 和 `tuple`；结构化绑定仍是 C++17）
- 线程 `<thread>` / `<mutex>` / `<condition_variable>` / `<future>`

原子操作：

- C++ 代码使用 `<atomic>`。
- C 代码使用 `<stdatomic.h>`。
- 禁止在 C++ 文件中混用 C11 原子接口与 C++ `<atomic>`。

### C++14 例外启用

使用前必须在文件顶部标记：

```cpp
// C++14 required: reason...
```

只允许以下特性：

- `std::make_unique`
- 泛型 Lambda（`auto` 参数）
- 普通函数的返回类型推导（短函数，返回类型显而易见）
- 二进制字面量 `0b...`
- 数字分隔符 `1'000'000`
- 放宽的 `constexpr`

### C++17 例外启用

使用前必须在文件顶部标记：

```cpp
// C++17 required: reason...
```

只允许以下特性：

- `std::optional`
- `std::string_view`
- `std::variant`
- 结构化绑定
- `if constexpr`
- 内联变量

未列入上面两个允许列表的 C++14/17 特性一律不写。包括 `decltype(auto)`、`std::exchange`、变量模板、折叠表达式、`if` 带初始化语句、`std::byte`、`std::filesystem`、并行算法、`std::any`、`std::flat_map`。

## MUST：违反必须修改

```text
MUST-001 默认代码必须支持 C++11。
          使用 C++14/17 特性的文件必须在顶部注释：
          // C++14 required: reason...
          // C++17 required: reason...

MUST-002 空指针使用 nullptr。禁止 NULL，禁止用整数 0 表示空指针。

MUST-003 禁止 std::auto_ptr。

MUST-004 裸指针默认表示 non-owning。禁止隐式 owning raw pointer。
          底层 allocator / mempool / slab / arena / DPDK / VPP
          管理资源时，允许 raw pointer 表达 owning，但必须：
          1. 优先使用 RAII wrapper；或
          2. 在声明处注释写明谁分配、谁释放、调用哪个释放函数。
          using BufferOwner = Buffer* 这类别名只是名字，编译器不检查，
          不能单独作为所有权声明。
          不得猜测 raw pointer 的 ownership。
          禁止把 shared_ptr::get() 再包进 unique_ptr 或另一个 shared_ptr。

MUST-005 析构函数不得抛异常。
          用户声明的移动构造、移动赋值必须 noexcept。

MUST-006 跨 ABI 边界禁止 C++ exception 穿越。
          extern "C"、JNI、AIDL、shared library、plugin、IPC 边界
          必须 catch(...) 转错误码。成功路径必须有返回值。

MUST-007 禁止悬空 Lambda 捕获。
          重点检查 [this]、[&]、[&var]、异步 callback、thread、task、
          Qt queued connection。引用捕获不得活过被引用对象。

MUST-008 头文件必须 self-contained，能独立编译。
          用到的标准库设施必须在该头文件中直接 include，
          禁止依赖传递 include。

MUST-009 重写虚函数必须写 override。
          final 只在明确禁止进一步重写时使用。
          不得给虚函数默认加 final。

MUST-010 已确认的 hot path 中，禁止无性能验证的 new/delete/malloc/free。
          热路径以任务说明或代码中的既有注释为准。
          不得把普通业务、界面、启动或配置代码猜测成热路径。

MUST-011 修改范围最小化。
          1. 只修改完成当前任务所必需的代码。
          2. 不得无关重构。
          3. 不得为了符合规范批量修改已有代码。
          4. 不得无理由修改 public API。
          5. 不得无理由改变 ABI。
          6. 不得无理由改变线程模型。
          7. 不得无理由更换容器或内存管理模型。
          8. 不得顺手格式化整个文件。
          扩大修改范围时，必须在修改说明里写明原因。

MUST-012 不得为了消除编译错误而无依据使用：
          - const_cast
          - reinterpret_cast
          - C-style cast
          - ownership conversion
          - lifetime extension
          - thread synchronization removal
          先修复类型、生命周期或接口。

MUST-013 原子内存序先保证正确，再考虑性能。按下面三条选择：
          1. 用原子发布或读取其他数据：release / acquire。
             同一原子上的读改写用 acq_rel。
          2. 只有一个独立计数器，没有其他数据要同步：relaxed。
          3. 说不清属于哪一种：保持 seq_cst。
          禁止把第 1 类或第 3 类机械改成 relaxed。
          无法归入第 2 类时，不得使用 relaxed。
          不要使用 memory_order_consume。

MUST-014 新代码沿用当前文件和当前模块已有的命名、大括号、
          错误处理和资源管理方式。
          本技能的风格默认值只用于没有既有约定的新文件。
          不得把已有错误码接口改成异常。
          不得把已有异常接口改成错误码。
          任务本身就是统一错误处理时除外。
          不得按本技能批量重命名已有 API。

MUST-015 非模板的 T&& 是右值引用，只绑定右值。
          模板参数 T&& 和 auto&& 是转发引用，左值和右值都能绑定。
          把转发引用继续传下去时使用 std::forward<T>，不要使用 std::move。
          std::move 只把表达式转成右值引用，本身不搬移资源。
          被移动后的对象只可析构或重新赋值，不得再当有效值读取。

MUST-016 std::thread 和 std::bind 按值保存参数。
          要让任务看到原对象时，使用 std::ref 或 std::cref。
          std::async 必须写出 launch 策略。
          需要在新线程执行时使用 std::launch::async。
          不写策略时，不得假设任务已经在新线程上开始。
          持有该 future 时，注意其析构可能等到任务结束。
          shared_ptr 只保证控制块计数的并发增减是安全的，
          不保证被管理对象的并发读写是安全的。
```

## SHOULD：默认遵守，例外需说明

```text
SHOULD-001 优先 Rule of Zero。
           只有资源管理类，且默认特殊成员函数不能满足不变量时，
           才手写 Rule of Five。

SHOULD-002 优先 RAII 表达资源生命周期。

SHOULD-003 拷贝便宜的参数按值传递：整数、指针、枚举、很小的平凡结构体。
           拷贝昂贵的只读参数用 const T&。
           不修改对象的成员函数标记 const。

SHOULD-004 能用 make_shared 时优先 make_shared。
           需要自定义删除器时不用 make_shared。
           对象很大且存在 weak_ptr 时，先评估控制块与对象共分配
           导致的延迟释放，再决定是否使用 make_shared。
           make_unique 是 C++14，C++11 基线中不可用。

SHOULD-005 能前置声明就不要 include。前置声明的类型不得按值使用。

SHOULD-006 多锁必须固定锁顺序，或用 defer_lock 的 unique_lock 调用 std::lock。
           std::lock 不会在析构时解锁，锁必须由 unique_lock 持有。

SHOULD-007 高频写入的跨线程共享变量必须评估 false sharing。
           alignas 会改变对象大小、对齐和数组步长，可能改变 ABI。
           没有跨线程写同一缓存行的证据时，不添加 alignas。

SHOULD-008 内存序遵守 MUST-013。
           同步关系可以用 acquire/release 说清时，仍写 seq_cst 要在修改说明里写理由。
           不得把本条理解成默认改成 relaxed。

SHOULD-009 已确认的 hot path 中谨慎使用 std::shared_ptr、std::function、
           std::map、会重新分配的 std::string。
           重点检查 allocation、reallocation、reference counting、
           type erasure、indirect call。
           容器类型本身不是禁止条件。

SHOULD-010 C++11 API 优先 pointer + size 或 iterator pair。
           string_view 属于 C++17 允许列表，启用前遵守 MUST-001。
```

## MAY：C++14/17 例外启用

```text
MAY-001 C++14 make_unique。
MAY-002 C++14 泛型 Lambda，仅当明显减少重复且不把 Lambda 类型泄漏到 API。
MAY-003 C++14 返回类型推导，仅限短函数且返回类型显而易见。
MAY-004 C++14 二进制字面量、数字分隔符。
MAY-005 C++17 optional、string_view、variant、结构化绑定、if constexpr、内联变量。
```

使用 `MAY` 必须同时满足：

- 团队编译器已全面支持该标准
- 收益显著
- 文件顶部标记标准
- 修改说明里写明理由

允许列表之外的更高标准特性不是 MAY，直接不写。

## REVIEW：AI 改完代码后逐项检查

检查步骤见 `cpp-review.md`。条目如下：

```text
REVIEW-001 ownership：谁拥有？谁释放？是否隐式 owning raw pointer？
REVIEW-002 lifetime：Lambda、callback、async、this、引用捕获是否悬空？
REVIEW-003 concurrency：锁是否由 unique_lock 持有？锁顺序、内存序、false sharing？
REVIEW-004 performance：仅热路径。是否分配、引用计数、虚函数、间接调用？
REVIEW-005 ABI：异常是否穿越 C API / shared library / plugin 边界？
REVIEW-006 C++11 兼容：是否使用了允许列表之外的 C++14/17 特性？
REVIEW-007 Rule of Zero：是否手写了本可默认生成的特殊成员函数？
REVIEW-008 header：是否 self-contained？是否依赖传递 include？
REVIEW-009 修改范围：是否只改了必需代码？
REVIEW-010 cast 与编译规避：是否无依据使用危险转换或删除同步？
REVIEW-011 原子正确性：是否符合 MUST-013 的三类选择？
REVIEW-012 ownership 猜测：是否在未显式声明时假设 raw pointer 的 ownership？
REVIEW-013 既有风格：是否沿用当前文件的命名和错误处理？是否把普通代码误判成热路径？
REVIEW-014 转发与移后状态：模板 T&& 是否误用 std::move？移后对象是否仍被读取？
REVIEW-015 线程参数：引用是否用 std::ref？std::async 是否写了 launch 策略？
```

## 编译器标志建议

```bash
# 最低要求：C++11
-std=c++11 -Wall -Wextra -Wpedantic -Werror
```

这是新构建配置的建议。不得因此修改业务工程里已经存在的编译选项，除非任务就是改构建配置。

CI 至少保留一个 `-std=c++11` 构建，确保核心代码不意外依赖 C++14/17。
