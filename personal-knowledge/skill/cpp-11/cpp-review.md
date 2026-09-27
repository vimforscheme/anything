# C++ Code Review Checklist

改完 C/C++ 后按本清单检查。规则正文在同目录 `SKILL.md`。发现问题必须修，或在输出里写明保留原因。

热路径相关各项：任务说明或既有注释没有确认热路径、报文路径、零拷贝路径时，直接标成跳过，不要为了清单去改普通代码。

## 1. Ownership

规则：`MUST-003`、`MUST-004`、`REVIEW-001`、`REVIEW-012`。

- 裸指针是否被当成拥有者？默认按 non-owning。
- owning 是否用了 RAII，或在声明处写明分配者和释放函数？
- 是否只写了 `using Owner = T*` 就当作已声明所有权？
- 是否把 `shared_ptr::get()` 又包进 `unique_ptr` 或另一个 `shared_ptr`？
- 是否使用 `std::auto_ptr`？
- 是否在没有声明的情况下猜测了 ownership？

## 2. Lifetime

规则：`MUST-007`、`REVIEW-002`。

- `[this]` 是否可能比对象活得更久？
- `[&]`、`[&variable]` 是否引用局部变量或临时对象，并在对象结束后仍被调用？
- 异步 callback、thread、task、Qt queued connection 是否持有引用？
- 值捕获离开定义作用域后是否仍然安全？
- 模板参数 `T&&` 或 `auto&&` 继续传递时，是否误用了 `std::move`（`MUST-015`）？
- 被移动后的对象是否又被当成有效值读取？

## 3. Concurrency

规则：`MUST-013`、`SHOULD-006`、`SHOULD-007`、`SHOULD-008`、`REVIEW-003`、`REVIEW-011`。

- 多个锁是否有固定顺序？
- 使用 `std::lock` 时，参数是否是 `defer_lock` 的 `unique_lock`？离开作用域时谁解锁？
- 内存序属于 `MUST-013` 的哪一类？独立计数器用 `relaxed`，发布或读取其他数据用 `release` / `acquire`，说不清则保持 `seq_cst`。
- 是否把第 1 类或第 3 类改成了 `relaxed`？
- 是否使用 `memory_order_consume`？
- 持锁期间是否调用了未知代码？
- 条件变量是否用谓词检查，而不是被唤醒一次就当作条件成立？
- 高频跨线程写同一缓存行时，是否评估了 false sharing？没有证据时不要加 `alignas`。
- 数据竞争只在本任务改到的共享状态上检查。
- `std::thread` / `std::bind` 是否按值保存了本该引用的参数？需要原对象时是否用了 `std::ref` / `std::cref`？
- `std::async` 是否写了 `std::launch::async` 或 `std::launch::deferred`？是否假设不写策略就会开新线程？
- 多个线程是否在没有额外同步时同时改同一份 `shared_ptr` 管理的对象？计数本身的并发增减不是这个问题。

热路径才继续看：IO 线程是否等待慢路径持有的锁，是否在零拷贝路径上 `malloc` / `free`。细节在 `cpp-performance.md`。

## 4. Performance

规则：`MUST-010`、`SHOULD-009`、`REVIEW-004`。未确认热路径则跳过。

- 是否在热路径上无验证地 `new` / `delete` / `malloc` / `free`？
- `shared_ptr`、`std::function`、会重新分配的 `string` 是否出现在已确认的高频路径？
- `std::map` 是否被机械换成 `unordered_map`？
- 预先 `reserve` 之后只按下标访问的 `vector`，不要当成热路径分配。

## 5. ABI

规则：`MUST-006`、`REVIEW-005`。

- `extern "C"`、JNI、AIDL、shared library、plugin、IPC 是否可能把 C++ 异常传出边界？
- `catch (...)` 之后是否返回了错误码？成功路径是否有返回值？
- C API 头文件是否暴露了 STL 类型？

## 6. C++11 兼容

规则：`MUST-001`、`REVIEW-006`。

- 是否使用了 `SKILL.md` 允许列表之外的 C++14/17 特性？
- `make_unique`、泛型 Lambda、普通函数返回类型推导、`string_view`、`optional`、`variant`、结构化绑定、`if constexpr` 是否出现？出现时文件顶部有没有标准标记和理由？
- C++ 文件是否包含了 `<stdatomic.h>`？
- 解包 `pair` / `tuple` 时是否用了结构化绑定，却没有 C++17 标记？C++11 应使用 `std::tie` 或 `std::get`。
- `constexpr` 函数是否在 C++11 下写了局部变量或循环，却没有 C++14 标记？

## 7. Rule of Zero / Five

规则：`MUST-005`、`SHOULD-001`、`REVIEW-007`。

- 成员已经管理资源时，是否仍手写了可默认生成的特殊成员函数？
- 手写移动是否只为了默认移动满足不了的不变量，并且 `noexcept`？
- 析构函数是否可能抛异常？
- 构造函数失败时，已经取得的资源是否由成员析构释放？

不要求本次任务把未修改的类升级成强异常保证。

## 8. Header

规则：`MUST-008`、`SHOULD-005`、`REVIEW-008`。

- 头文件能否在只包含它自己时编译？
- `std::move`、智能指针、容器是否在本文件直接 include？
- 只持有指针或引用时，是否可以改为前置声明？
- 头文件是否有 `using namespace`？

## 9. 修改范围

规则：`MUST-011`、`REVIEW-009`。

- 是否做了与任务无关的重构、批量重命名或全文件格式化？
- 是否无理由修改了 public API、ABI、线程模型、容器或内存管理方式？
- 扩大修改范围时，修改说明里有没有原因？

## 10. Cast 与编译规避

规则：`MUST-012`、`REVIEW-010`。

- 是否无依据使用 `const_cast`、`reinterpret_cast` 或 C 风格转换？
- 是否无依据转换所有权、延长生命周期，或删除同步？
- 能否改为修正类型、生命周期或接口？

## 11. 既有风格

规则：`MUST-014`、`REVIEW-013`。

- 新代码的命名、大括号、错误处理是否和当前文件一致？
- 是否把本模块的错误码改成了异常，或把异常改成了错误码？
- 是否把界面、启动、配置或普通业务代码当成热路径？

## 12. 最终检查清单

```text
[ ] REVIEW-001 ownership
[ ] REVIEW-002 lifetime
[ ] REVIEW-003 concurrency
[ ] REVIEW-004 performance（非热路径写“跳过”）
[ ] REVIEW-005 ABI
[ ] REVIEW-006 C++11 兼容
[ ] REVIEW-007 Rule of Zero
[ ] REVIEW-008 header
[ ] REVIEW-009 修改范围
[ ] REVIEW-010 cast 与编译规避
[ ] REVIEW-011 原子正确性（MUST-013 三类）
[ ] REVIEW-012 ownership 猜测
[ ] REVIEW-013 既有风格
[ ] REVIEW-014 转发与移后状态
[ ] REVIEW-015 线程参数与 std::async
[ ] 是否违反 MUST-001 ~ MUST-016
[ ] 是否违反 SHOULD-001 ~ SHOULD-010 且未写理由
[ ] 是否使用 MAY，且已标记标准并写明理由
```
