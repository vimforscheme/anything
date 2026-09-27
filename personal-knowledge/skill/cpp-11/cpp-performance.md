# C++ 性能与并发规范

先遵守同目录 `SKILL.md`。编号规则以 `SKILL.md` 正文为准。

本文件只在任务说明或既有注释已经确认是热路径、报文路径或零拷贝路径时使用。普通业务、界面、启动和配置代码不使用本文件。不得把普通函数猜测成热路径（`MUST-010`）。

## 1. Hot path 规则

```text
PERF-001 已确认的 hot path 上，不得无依据使用会动态分配、引用计数或间接调用的操作。
         检查 allocation、reallocation、reference counting、type erasure、indirect call。
         容器类型本身不是禁止条件。

PERF-002 已确认的 packet fast path 上，不得 new/delete/malloc/free，除非已有性能验证并在修改说明里记录。

PERF-003 已确认的高频 packet path 上，不得使用 shared_ptr，除非修改说明里写了理由。

PERF-004 高频 lookup 不要默认使用 std::map。
         禁止机械替换成 unordered_map。替换前必须写出：
         lookup pattern、插入删除频率、遍历方式、key 分布、
         内存局部性、是否需要有序、benchmark 或工作负载依据。

PERF-005 已确认的高频 callback 不使用 std::function。
         改用模板参数或函数指针。
```

`vector` 在初始化阶段 `reserve`，快路径只按下标访问时，可以留在热路径上：

```cpp
std::vector<Packet> packets;
packets.reserve(1024);
// fast path:
packets[i]
```

有序遍历、`lower_bound`、范围查询或稳定迭代器仍可能适合 `std::map`。不要因为 `PERF-004` 把它换成 `unordered_map`。

高频回调的替代：

```cpp
template<typename Fn>
void on_packet(Fn&& fn, Packet* packet)
{
    std::forward<Fn>(fn)(packet);
}

typedef void (*PacketFn)(Packet* packet);
```

`Fn&&` 是转发引用。把回调交出去或立刻调用时用 `std::forward<Fn>`，不要 `std::move`（`MUST-015`）。

## 2. 内存分配

热路径上的缓冲区优先来自栈、对象池、arena、mempool 或 slab。初始化阶段可以分配，处理每个报文的路径上不再分配。

`std::string` 追加、`std::vector` 超过 `capacity`、`std::function` 的类型擦除、`std::shared_ptr` 的控制块，都会在看起来没有 `new` 的代码里分配。

## 3. 容器选择

必须结合工作负载选择，禁止机械替换。C++11 基线不使用 `std::flat_map`。

| 场景 | 推荐 |
| --- | --- |
| 高频 lookup | `unordered_map`、地址连续的有序数组、`array`、`vector`、radix tree、自定义哈希表 |
| 有序遍历、`lower_bound`、范围查询 | `std::map`、`std::set` |
| 小规模固定集合 | `std::array`、容量已预留的 `std::vector` |
| 高频插入删除 | 按工作负载在对象池、链表或预留容量的 `vector` 之间选择，并写出依据 |

## 4. 虚函数与间接调用

已确认的热路径上，虚函数和 `std::function` 都是间接调用。高频回调改用模板参数、函数指针或 CRTP。

## 5. 并发

### 5.1 线程

普通线程使用 `std::thread`、`std::mutex`、`std::lock_guard` / `std::unique_lock`、`std::condition_variable`、`std::atomic<T>`。

需要绑核、实时调度策略或自定义栈大小时使用 `pthread`，并在注释里写明需要哪一种能力。`std::thread` 在 C++11 里不能表达这些要求。

`std::thread` 对象在仍可 `join` 时析构会调用 `std::terminate`。每个线程对象都要有明确的 `join` 或 `detach`，并写明谁负责。

`std::thread` 按值保存参数。任务要修改调用方的对象时，传入 `std::ref` 或 `std::cref`（`MUST-016`）。

`std::async` 必须写启动策略。要在新线程上跑，使用 `std::launch::async`。不写策略时，任务可能拖到 `future::get()` 才执行，`future` 析构也可能一直等到任务结束。

### 5.2 原子内存序

正文是 `MUST-013`、`SHOULD-008`。C++ 文件使用 `<atomic>`。

| 场景 | 内存序 |
| --- | --- |
| 用原子发布或读取其他数据 | `release` / `acquire`；同一原子上的读改写用 `acq_rel` |
| 只有一个独立计数器 | `relaxed` |
| 说不清属于哪一种 | `seq_cst` |

独立计数器：

```cpp
counter.fetch_add(1, std::memory_order_relaxed);
```

若这个加法还表示“数据已经准备好，其他线程可以读”，它就不是独立计数器，使用 `release` / `acquire`。

同步关系可以用 `acquire` / `release` 说清时，仍写 `seq_cst` 要在修改说明里写理由。不得因此把已有正确同步改成 `relaxed`。不使用 `memory_order_consume`。

### 5.3 False sharing

正文是 `SHOULD-007`。

不同线程高频写入的计数器、锁、队列生产消费状态，评估是否落在同一缓存行。常见缓存行是 64 字节，目标平台不同则按该平台。

```cpp
struct alignas(64) Counter {
    std::atomic<uint64_t> value;
};
```

`alignas` 会改变对象大小、对齐和数组步长，可能改变 ABI。没有“多个线程写相邻字段”的证据时，不添加。

### 5.4 锁层级

多锁使用固定顺序，或使用会在析构时解锁的 `unique_lock`：

```cpp
std::unique_lock<std::mutex> lock_a(mutex_a, std::defer_lock);
std::unique_lock<std::mutex> lock_b(mutex_b, std::defer_lock);
std::lock(lock_a, lock_b);
```

`std::lock` 只负责按不会死锁的顺序锁上，不会在函数结束时解锁。不要对裸 `mutex` 调用 `std::lock` 后就结束作用域。

固定顺序用注释写明，例如 `global_lock`，然后 `connection_lock`，然后 `buffer_lock`。

### 5.5 阻塞

已确认的 IO 线程或零拷贝路径：

- 不等待可能被慢路径持有的锁。持有时间确定且很短的临界区可以保留。
- 不调用 `condition_variable.wait()`、`sleep()`，不执行慢速数据库操作。
- 零拷贝 fast path 上不 `malloc` / `free`。
- 处理单个报文时不写无界循环。线程本身的事件循环除外。

## 6. 缓存与数据布局

热数据放在一起，冷数据分开。先有访问模式证据，再改布局或添加 `alignas`。

## 7. 性能审查清单

未确认热路径时，清单全部写“跳过”。

```text
PERF-REVIEW-001 热路径上是否分配、重分配？
PERF-REVIEW-002 高频路径是否使用 shared_ptr？
PERF-REVIEW-003 高频回调是否使用 std::function？
PERF-REVIEW-004 std::map 的替换是否写了工作负载依据？
PERF-REVIEW-005 是否有证据表明 false sharing，且 alignas 没有误改 ABI？
PERF-REVIEW-006 内存序是否符合 MUST-013 的三类？
PERF-REVIEW-007 std::lock 的结果是否由 unique_lock 持有？
PERF-REVIEW-008 IO 线程是否在等待慢路径的锁？
PERF-REVIEW-009 零拷贝 fast path 是否 malloc/free？
PERF-REVIEW-010 热路径缓冲是否可以来自已有对象池、arena 或 mempool？
PERF-REVIEW-011 是否把普通代码误判成了热路径？
```
