# C++ 风格与语言特性规范

先遵守同目录 `SKILL.md`。编号规则以 `SKILL.md` 正文为准，本文件不重复那些段落。

本文件的命名、大括号和错误处理是**没有既有约定的新文件**的默认值。当前文件或当前模块已有风格时，遵守 `MUST-014`，沿用已有风格。

## 1. 命名约定

没有既有约定时：

| 元素 | 命名风格 | 示例 |
| --- | --- | --- |
| 类/结构体/枚举类型/联合体 | 大驼峰 | `UrlTable` |
| 函数（全局/成员） | `snake_case` | `parse_url()` |
| 全局变量/局部变量/函数参数 | `snake_case` | `max_count` |
| 成员变量 | `snake_case_`（后缀下划线） | `count_` |
| 常量（`const`/`constexpr`） | `k` + 大驼峰 | `kMaxSize` |
| `enum class` 枚举值 | `k` + 大驼峰 | `kRed` |
| 宏 | 全大写 + 下划线 | `MAX_BUFFER_SIZE` |
| 命名空间 | 大驼峰 | `namespace UrlUtils` |

规则：

- 避免用 `typedef` / `#define` 给基本类型起别名。
- 优先使用 `<cstdint>` 中的 `int8_t`、`uint32_t` 等。
- 已有文件使用驼峰函数名、小写命名空间或其他枚举风格时，新代码跟该文件，不套用上表。

## 2. 代码风格

- 文件后缀：头文件 `.h`，实现文件 `.cpp`。已有目录使用 `.hpp` / `.cc` 时跟该目录。
- 头文件保护：优先 `#pragma once`。模块已统一使用 include guard 时跟模块。
- 缩进：4 空格，不使用 Tab。
- 大括号：函数的 `{` 独占一行。控制语句的 `{` 与语句同一行。构造函数的初始化列表换行后，`{` 仍独占一行。
- 行宽：不超过 100 字符。

```cpp
void parse_url()
{
    if (ready) {
        read();
    }
}

explicit Buffer(std::size_t size)
    : data_(size)
{
}
```

## 3. 初始化

用 `{}` 防止窄化转换：

```cpp
int x{0};
class Foo { int count_{0}; };
```

类型有 `initializer_list` 构造函数时，先看清重载。`std::vector<int> v{10}` 是一个值为 10 的元素，不是长度 10。本意是元素个数或长度时用圆括号：

```cpp
std::vector<int> values(10);
std::vector<int> values{1, 2, 3};
```

非静态成员变量使用默认成员初始化。

## 4. `auto`

- 用于局部变量，尤其迭代器和长类型名。
- 不用于函数参数。C++14 泛型 Lambda 除外，且须遵守 `MAY-002`。
- 普通函数不省略返回类型。C++11 需要由表达式决定返回类型时，写尾置返回类型 `auto f() -> decltype(expr)`。C++14 短函数且类型显而易见时才省略，且须遵守 `MAY-003`。`decltype(auto)` 不是 C++11。
- C++11 lambda 只有一条 `return` 时可以省略返回类型。
- `auto values = {1, 2, 3}` 的类型是 `std::initializer_list<int>`，不是 `vector`。需要 `vector` 时写明类型。

## 5. 范围 for

- 只读遍历使用 `const auto&`。
- 需要修改使用 `auto&`。
- 需要拷贝使用 `auto`。元素拷贝便宜时，按值遍历即可。

```cpp
for (const auto& item : container) {
    process(item);
}
```

## 6. `nullptr`

空指针遵守 `MUST-002`：使用 `nullptr`。

```cpp
if (ptr != nullptr) {
    use(ptr);
}
```

## 7. 强类型枚举

没有既有约定时，优先 `enum class`，并显式指定底层类型。枚举值使用 `k` + 大驼峰，不使用宏的全大写。

```cpp
enum class Color : std::uint8_t {
    kRed,
    kGreen,
    kBlue
};
```

## 8. 智能指针与所有权

选择准则：

| 所有权模型 | 指针类型 |
| --- | --- |
| 独占、线性所有权 | `std::unique_ptr<T>` |
| 共享、引用计数 | `std::shared_ptr<T>` |
| 观察 `shared_ptr` 管理的对象 | `std::weak_ptr<T>` |
| 非拥有观察 | 裸指针 `T*` 或 `T&` |

所有权规则的正文是 `MUST-003`、`MUST-004`、`SHOULD-004`。执行时遵守：

- 裸指针默认表示 non-owning。
- 优先 RAII wrapper。暂时不能包装时，在声明旁写明释放函数。
- `using BufferOwner = Buffer*` 不能代替这条注释。
- 能用 `make_shared` 时优先 `make_shared`。自定义删除器，或大对象同时存在 `weak_ptr` 时，按 `SHOULD-004` 的例外处理。
- C++11 不使用 `make_unique`。
- 禁止把 `shared_ptr::get()` 再包进 `unique_ptr` 或另一个 `shared_ptr`。
- `shared_ptr` 只让控制块计数可以并发增减。多个线程改它管理的对象时，仍要另加同步（`MUST-016`）。

非 RAII 资源的注释写法：

```cpp
// Owns buffer. Release with buffer_pool_free(). Do not delete.
Buffer* buffer = nullptr;
```

## 9. Lambda

正文是 `MUST-007`。Lambda 用于局部回调和算法谓词。引用捕获不得活过被引用对象。

```cpp
std::sort(v.begin(), v.end(), [](int a, int b) {
    return a > b;
});
```

离开作用域后仍要调用的回调，捕获值，不捕获引用：

```cpp
auto callback = [value]() {
    printf("%d\n", value);
};
```

重点检查 `[this]`、`[&]`、`[&variable]`、异步 callback、thread、task、Qt queued connection。

## 10. `const` / `constexpr`

参数传递遵守 `SHOULD-003`：

- 整数、指针、枚举、很小的平凡结构体：按值传递。
- 拷贝昂贵的只读参数：`const T&`。
- 不修改对象的成员函数：`T::foo() const`。

指针语义写清楚：

```cpp
const T*        // pointer to const
T* const        // const pointer
const T* const  // const pointer to const
```

`constexpr` 用于编译期常量。C++11 的 `constexpr` 函数体只能有一条 `return`，不能写局部变量或循环。需要局部变量或循环时，按 `MAY` 启用 C++14 放宽的 `constexpr`，并在文件顶部标记。

不为了 const 正确性把代码写成多层转发。

## 11. 类设计

优先 Rule of Zero（`SHOULD-001`）。成员本身会管理资源时，不手写拷贝、移动和析构：

```cpp
class Buffer {
public:
    explicit Buffer(std::size_t size)
        : data_(size)
    {
    }

private:
    std::vector<std::uint8_t> data_;
};
```

不要为了展示移动语义手写 `Buffer(Buffer&&)`。

只有默认移动不能满足不变量时才手写，并在函数上写明不变量。下面的 `size_` 必须和空指针一致，`unique_ptr` 的默认移动不会把 `size_` 清零：

```cpp
Buffer(Buffer&& other) noexcept
    : data_(std::move(other.data_))
    , size_(other.size_)
{
    other.size_ = 0;
}
```

这段所在的头文件必须直接 `#include <utility>`（`MUST-008`）。

移动和转发遵守 `MUST-015`：

- 非模板的 `T&&` 是右值引用。模板参数 `T&&`、`auto&&` 是转发引用。
- 转发引用继续传递时用 `std::forward<T>`，不用 `std::move`。
- `std::move` 只做类型转换。对象被移动之后，只析构或重新赋值，不再读取它的值。

```cpp
template <typename T>
void store(T&& value)
{
    slot_ = std::forward<T>(value);
}
```

C++11 解包 `std::pair` / `std::tuple` 用 `std::tie` 或 `std::get`。结构化绑定是 C++17，未标记不得使用。

```cpp
std::string name;
std::tie(std::ignore, name) = std::make_pair(1, std::string("box"));
```

`override` 遵守 `MUST-009`。`= default` / `= delete` 用于需要明确特殊成员函数的场合。析构函数不得抛异常。用户声明的移动构造和移动赋值必须 `noexcept`（`MUST-005`）。

## 12. 异常与错误处理

当前模块已经使用错误码或已经使用异常时，遵守 `MUST-014`，不要改换风格。

没有既有约定的新模块：

- 异常用于资源获取失败和违反前置条件。
- 不用异常做正常控制流。
- 错误码用于已确认的热路径，以及 `MUST-006` 的 ABI 边界。
- `assert` 用于调试期检查。`static_assert` 用于编译期检查。

跨 C API、JNI、AIDL、shared library、plugin、IPC 边界时，遵守 `MUST-006`。捕获后把异常转成错误码，成功路径要返回成功码：

```cpp
extern "C" int foo(int input)
{
    try {
        return do_foo(input);
    } catch (...) {
        return kErrFailed;
    }
}
```

## 13. 模板与泛型

- 用 `using` 定义类型别名，不用 `typedef`。
- C++20 概念不可用。约束用 `static_assert` 和 `type_traits`。
- 变参模板用于转发时，控制实例化深度。
- 用 `if constexpr` 替代标签分发属于 `MAY-005`，必须先标记 C++17。

## 14. 头文件依赖

正文是 `MUST-008` 和 `SHOULD-005`。

```text
头文件必须能单独编译。
用到的声明必须在本文件直接 include。
只保存指针或引用、且不需要完整类型时，前置声明。
头文件中不写 using namespace。
```

只出现 `Bar*` 时：

```cpp
class Bar;
```

按值包含 `Bar`、继承 `Bar`、或调用 `Bar` 的成员时，include `bar.h`。

## 15. 原子操作

正文是 `MUST-013` 和 `SHOULD-008`。C++ 文件使用 `<atomic>`，不使用 `<stdatomic.h>`。

独立计数器才使用 `relaxed`：

```cpp
counter.fetch_add(1, std::memory_order_relaxed);
```

这个计数器如果还负责“发布”另一份数据，就不是独立计数器，改用 `release` / `acquire`。说不清时保持 `seq_cst`。

## 16. 修改范围与危险转换

修改范围遵守 `MUST-011`。消除编译错误时遵守 `MUST-012`。

## 17. 示例：没有既有风格时的 C++11 类

`vector` 管理缓冲区，特殊成员函数全部默认生成。头文件直接包含它用到的标准库头。

```cpp
// buffer.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace MyProject {

class Buffer {
public:
    Buffer() = default;

    explicit Buffer(std::size_t size)
        : data_(size)
    {
    }

    std::size_t size() const noexcept
    {
        return data_.size();
    }

    std::uint8_t* data() noexcept
    {
        return data_.data();
    }

    const std::uint8_t* data() const noexcept
    {
        return data_.data();
    }

private:
    std::vector<std::uint8_t> data_;
};

}  // namespace MyProject
```
