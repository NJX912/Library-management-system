# 图书管理系统（Library Management System）

一个用现代 C++（C++20）实现的控制台图书管理系统，覆盖图书增删改查、借书、还书、图书状态标记与文本文件持久化的完整闭环。

工程按生产级分层组织：领域模型与界面解耦、构建走 CMake、带独立单元测试、内存所有权显式可验证。

---

## 目录

- [功能特性](#功能特性)
- [环境要求](#环境要求)
- [构建与运行](#构建与运行)
- [使用说明](#使用说明)
- [项目结构](#项目结构)
- [架构设计](#架构设计)
- [关键设计决策](#关键设计决策)
- [数据文件格式](#数据文件格式)
- [测试](#测试)
- [常见问题](#常见问题)

---

## 功能特性

| 功能 | 说明 |
| --- | --- |
| 图书录入 | 按 ISBN 唯一标识入库，字段在构造时校验 |
| 图书修改 | 书名 / 作者 / 出版年份可改，ISBN 不可变 |
| 图书删除 | 借出中的图书禁止删除，必须先归还 |
| 借书 | 记录读者与应还日期，默认可配置借期（默认 30 天） |
| 还书 | 关闭对应借阅流水并回写归还日期 |
| 图书状态标记 | 在架 / 已借出 / 遗失 / 下架，状态迁移受规则约束 |
| 搜索 | 书名、作者、ISBN 关键字匹配，大小写不敏感 |
| 借阅流水 | 完整的借出—归还台账 |
| 保存到 txt | 退出或手动保存，写入采用原子替换 |
| 启动加载 | 程序启动自动读取数据文件，文件不存在则按空库处理 |

---

## 环境要求

- **编译器**：支持 C++20 的 GCC ≥ 11、Clang ≥ 14 或 MSVC ≥ 19.29
- **构建工具**：CMake ≥ 3.20
- 无第三方依赖，标准库自带测试框架

本文档中的命令在 MinGW-w64 GCC 15.2 + CMake 4.2 下实测通过。

---

## 构建与运行

### MinGW / MSYS2（Windows）

```bash
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/bin/libms_app.exe
```

### Linux / macOS

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/bin/libms_app
```

### MSVC（Visual Studio）

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
build\bin\Release\libms_app.exe
```

产物位置：

- `build/bin/libms_app` — 主程序
- `build/bin/libms_tests` — 单元测试
- `build/lib/liblibms.a` — 核心静态库

### 指定数据文件路径

默认读写 `data/books.txt`，可通过命令行参数覆盖：

```bash
./build/bin/libms_app.exe D:/somewhere/my_library.txt
```

---

## 使用说明

启动后进入菜单循环：

```
================ 图书管理系统 ================
   1) 列出全部图书          2) 添加图书
   3) 修改图书信息          4) 删除图书
   5) 借书                  6) 还书
   7) 标记图书状态          8) 搜索图书
   9) 查看借阅流水         10) 保存到文件
   0) 保存并退出
==============================================
```

几个约定：

- 菜单 `0` 会在退出前自动保存；直接关闭窗口（或 Ctrl+Z 结束输入流）也会先保存。
- 修改图书时直接回车即保留原值。
- 借期留空则使用默认 30 天。
- 操作失败会打印 `[!]` 开头的具体原因，例如：

  ```
  [!] 操作失败：图书《深入理解计算机系统》当前状态为「下架」，不可借出
  [!] 操作失败：ISBN 978-7-121-34150-5 已存在
  ```

- Windows 下程序会主动把控制台切到 UTF-8 代码页，中文不会乱码。

---

## 项目结构

```
.
├── CMakeLists.txt              # 构建定义
├── README.md
├── LICENSE
├── .gitignore
├── include/library/            # 对外头文件（按库名建子目录，避免污染 include 根）
│   ├── Book.hpp                # 图书实体 + 状态枚举 + 借阅流水结构
│   ├── Library.hpp             # 图书馆聚合根，持有内存与全部业务规则
│   ├── TextStorage.hpp         # 文本持久化
│   └── ConsoleUI.hpp           # 命令行界面
├── src/
│   ├── Book.cpp
│   ├── Library.cpp
│   ├── TextStorage.cpp
│   ├── ConsoleUI.cpp
│   └── main.cpp                # 组装依赖、加载数据、进入主循环
├── tests/
│   └── test_library.cpp        # 单元测试（无第三方依赖）
└── data/
    └── README.md               # 运行时数据目录，books.txt 被 .gitignore 排除
```

---

## 架构设计

分三层，依赖单向向下，界面层不碰文件、持久化层不碰业务规则：

```
        main.cpp（组装）
             │
     ┌───────┴────────┐
     ▼                ▼
 ConsoleUI  ────▶  Library ◀──── TextStorage
 （输入/展示）    （业务规则）      （读写 txt）
                     │
                     ▼
                   Book
                （实体/状态）
```

| 类型 | 职责 | 关键点 |
| --- | --- | --- |
| `Book` | 图书实体，维护自身状态与当前借阅信息 | 值语义，构造时即校验字段 |
| `Library` | 图书目录 + 借阅流水，全部业务规则的唯一入口 | **独占持有堆上的 `Book`，析构统一释放** |
| `TextStorage` | 把 `Library` 的状态序列化成文本、从文本还原 | 不依赖 `Library`，只认 `Book` 与流水 |
| `ConsoleUI` | 解析输入、渲染表格、把异常翻译成人话 | 不含业务判断 |

`Library` 与 `TextStorage` 互不知晓对方，由 `main` 负责编排加载：

```cpp
LibrarySnapshot snapshot = storage.load();
for (auto& book : snapshot.books) {
    library.adopt(std::move(book));   // 所有权交给 Library
}
library.restoreLedger(std::move(snapshot.ledger));
```

### 容器选型

- `std::vector<Book*>` 存目录：需要按下标随机访问、需要连续遍历打印。
- `std::list<BorrowRecord>` 存流水：只在尾部追加、按时间顺序整体遍历，正是链表的主场。

---

## 关键设计决策

### 1. 内存管理：裸指针 + 析构释放（Rule of 5）

`Library` 是堆上 `Book` 的唯一所有者：

```cpp
Library::~Library() {
    clear();          // 逐个 delete
}
```

因为持有所有权，拷贝语义必须被切断，否则会二次释放：

```cpp
Library(const Library&)            = delete;
Library& operator=(const Library&) = delete;
Library(Library&& other) noexcept;            // 移动 = 完整转移所有权
Library& operator=(Library&& other) noexcept;
```

移动构造里有一个容易踩的坑：标准只保证被移动的 `vector` 处于「有效但未指定」状态，**不能假设它已经空了**，因此显式 `clear()` 源对象，才能真正保证析构时不会重复 `delete`。

**释放在哪里可以验证？** `Library::liveBookObjects()` 是一个自检探针，返回值等于「已 new 但尚未 delete 的 Book 数」。正常情况下它恒等于 `bookCount()`，且所有 `Library` 实例析构后回落到 0。单元测试直接断言这一点：

```cpp
void testDestructorReleasesHeapMemory() {
    const std::int64_t baseline = Library::liveBookObjects();
    {
        Library library;
        library.addBook("ISBN-1", "A", "B", 2000);
        library.addBook("ISBN-2", "C", "D", 2001);
        CHECK_EQ(Library::liveBookObjects(), baseline + 2);
    }                                        // <- Library 析构
    CHECK_EQ(Library::liveBookObjects(), baseline);   // 堆上的 Book 已被 delete
}
```

`Book` 自身则遵守 **Rule of 0**：只持有 `std::string` 与平凡成员，拷贝/移动全交给编译器生成，因此不需要、也不应该手写析构函数。

### 2. 异常契约

两类错误分开表达，调用方可以精确区分：

| 异常类型 | 含义 | 例子 |
| --- | --- | --- |
| `std::invalid_argument` | **参数本身**不合法 | 书名为空、年份越界、字段含 `\|`、读者姓名为空 |
| `libms::LibraryError` | **领域规则**不允许 | ISBN 重复、书不存在、已借出不能再借、借出中不能删除 |

界面层统一捕获并打印，程序不会因为一次误操作而中断。

### 3. 状态迁移受规则约束

不是任意状态都能互相切换，`Library::markStatus()` 只接受合法迁移：

- 不能直接标记成「已借出」——必须走 `borrow()`；
- 「已借出」必须先还书，才能改成在架 / 遗失 / 下架；
- 不允许原地重复标记同一状态。

### 4. 持久化：原子替换，永不写坏数据

`save()` 先写同目录的 `books.txt.tmp`，成功后再 `std::filesystem::rename` 覆盖正式文件。中途失败（磁盘满、进程被杀）只会留下临时文件，原数据保持完整。

加载侧同样保守：格式损坏时**报错退出并且不保存**，绝不会用空库覆盖掉用户数据。

### 5. 编译期约束

`CMakeLists.txt` 里为所有 target 挂上统一的高告警等级：

```
-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast
```

当前代码在这些开关下**零告警**通过。

---

## 数据文件格式

`|` 分隔的纯文本，分两个节。`#` 开头为注释。

```
# Library Management System —— 数据文件，请勿手工改动分隔符
VERSION|1

[BOOKS]
# isbn|title|author|publishYear|status|borrower|dueDate
978-7-121-34150-5|深入理解计算机系统|Randal Bryant|2016|OFF_SHELF||
978-0-13-235088-4|Clean Code|Robert Martin|2008|BORROWED|张三|2026-10-23

[LEDGER]
# isbn|borrower|borrowDate|dueDate|returnDate|returned
978-0-13-235088-4|张三|2026-09-23|2026-10-23||0
```

- `status` 取值：`AVAILABLE` / `BORROWED` / `LOST` / `OFF_SHELF`（与语言无关，枚举数值变也不影响旧文件）。
- 日期一律 `YYYY-MM-DD`，未归还时 `returnDate` 为空。
- 字段中出现 `|` 或换行会被拒绝，从源头保证格式可解析。

---

## 测试

```bash
ctest --test-dir build --output-on-failure
# 或者直接跑
./build/bin/libms_tests
```

当前 **16 个用例 / 79 项断言全部通过**，覆盖：

- 图书增删改查与 ISBN 唯一性
- 借书、还书、重复借出、无效借期
- 状态迁移的合法与非法路径
- 搜索的大小写不敏感
- 保存 → 加载往返一致（含中文与状态）
- 文件不存在按空库处理、损坏文件被拒绝
- **析构释放堆内存、`clear()` 释放、移动语义不重复释放、删除单本只释放该书**

---

## 常见问题

### MinGW 下程序在 `libstdc++-6.dll` 里随机崩溃

**现象**：Debug 构建正常，Release 构建一跑就段错误，调用栈全在 `libstdc++-6.dll` 内、没有符号。

**原因**：MinGW 默认**动态**链接 `libstdc++-6.dll`，运行时按 `PATH` 顺序解析。如果 `PATH` 上存在另一个 MinGW 发行版（最典型的是 **Git for Windows 自带的旧版 libstdc++**），程序会加载到 ABI 不兼容的那个运行库，进而在 C++ 标准库内部崩溃。

**本项目已经处理**：`CMakeLists.txt` 对 MinGW 加了 `-static-libgcc -static-libstdc++`，产物自包含，不再受 `PATH` 影响：

```cmake
add_library(libms_runtime INTERFACE)
if(MINGW)
    target_link_options(libms_runtime INTERFACE -static-libgcc -static-libstdc++)
endif()
```

**临时规避**（不加该选项时）：把编译器自带的 bin 目录提到 `PATH` 最前，例如

```bash
PATH="/d/code/mingw64/bin:$PATH" ./build/bin/libms_app.exe
```

### 中文显示成乱码

Windows 下程序启动即调用 `SetConsoleOutputCP(CP_UTF8)`。若在旧版终端里仍异常，请在 Windows Terminal 中运行，或先执行 `chcp 65001`。

---

## License

[MIT](LICENSE)
