# LightWebServer 设计文档

## 1. 项目概述

### 项目定位

LightWebServer 是一个轻量级 HTTP Web 服务器，面向学习演示、小规模部署和嵌入式场景。不追求 Nginx 级别的性能，而是在代码可读性和工程完整性之间取平衡——核心模型用 Reactor + epoll 实现，代码量控制在 2000 行以内，每个模块职责单一、可直接阅读。

### 核心能力与目标

| 能力 | 说明 |
|------|------|
| 高并发 I/O | 基于 epoll（Linux）/ select（Windows）的事件驱动模型，单线程事件循环不阻塞 |
| HTTP/1.1 协议 | 支持 GET / POST / HEAD 方法，增量解析请求，正确处理 Keep-Alive |
| 静态资源服务 | 根据文件扩展名自动匹配 MIME 类型，直接读取磁盘文件返回 |
| API 路由 | 支持注册自定义路由回调，先匹配 API 再匹配静态文件 |
| 线程池并发 | 业务逻辑在线程池执行，不占用事件循环线程 |
| 连接超时 | 空闲连接自动超时关闭，防止资源泄漏 |
| 跨平台 | 同一套代码编译为 Linux ELF 和 Windows PE 二进制 |

### 适配平台

- **Linux**：x86_64，glibc 2.17+，epoll 可用
- **Windows**：7 及以上 64-bit，Winsock2 可用
- 跨平台通过编译期宏 `_WIN32` 切换，无运行时分支

### 技术栈总览

| 项 | 选型 |
|----|------|
| C++ 版本 | C++17（用到了 `std::optional` 语义、`std::string::erase`、结构化绑定思路等） |
| 网络模型 | 单 Reactor + 线程池 |
| I/O 多路复用 | Linux: epoll / Windows: select |
| HTTP 解析 | 手写增量状态机，无第三方库 |
| 线程池 | `std::thread` + `std::condition_variable`，无第三方库 |
| 定时器 | 双 `std::map`，懒删除 |
| 编译工具 | CMake 3.10+，支持 MinGW-w64 交叉编译 |
| 依赖库 | 零外部依赖，纯标准库 |

---

## 2. 整体系统架构

### 分层架构

```
┌────────────────────────────────────────────────────┐
│                   main.cpp                         │
│            参数解析 · 组件组装 · API注册             │
├────────────────────────────────────────────────────┤
│                   Server 层                        │
│          TCP监听 · 连接接纳 · 路由分发               │
├──────────────┬─────────────────────────────────────┤
│  HttpConnection 层                                 │
│     连接生命周期 · 读写调度 · 线程池分发              │
├──────────────┼──────────────┬──────────────────────┤
│  HTTP 协议层                │    路由/响应层          │
│  HttpRequestParser          │    RequestHandler      │
│  HttpResponse               │    (lambda 回调)       │
├──────────────┴──────────────┴──────────────────────┤
│                 事件驱动层                           │
│    EventLoop · Channel · Poller                     │
├────────────────────────────────────────────────────┤
│                 基础设施层                           │
│    ThreadPool · TimerManager · Logger · sockutil    │
└────────────────────────────────────────────────────┘
```

### 整体工作流程与数据流转

```
客户端请求
    │
    ▼
[epoll/select 就绪] ──→ EventLoop::loop()
    │
    ▼
Channel::handleEvent() ──→ 回调分发
    │
    ├─→ accept Channel ──→ Server::handleAccept()
    │                        │
    │                        ▼
    │                    创建 HttpConnection
    │                    注册 read Channel
    │                    添加 Timer
    │
    └─→ connection Channel ──→ HttpConnection::handleRead()
                                │
                                ▼
                           HttpRequestParser::parse()
                                │
                   ┌────────────┼────────────┐
                   ▼            ▼            ▼
               COMPLETE     INCOMPLETE    PARSE_ERROR
                   │            │            │
                   ▼            │            ▼
           pool_->submit()     │     400 Bad Request
           (线程池处理)         │     直接写回
                   │            │
                   ▼            │         等待更多数据
           processRequest()     │        （什么都不做）
                   │            │
                   ▼            │
           request_handler_()  │
           (API路由/静态文件)    │
                   │            │
                   ▼            │
           loop_->queueInLoop() │
           (投递回事件循环)      │
                   │            │
                   ▼            │
           HttpConnection       │
           ::handleWrite()      │
                   │            │
                   ▼            │
           sockutil::sendAll()  │
                   │            │
                   └────────────┘
                        │
                        ▼
                   客户端收到响应
```

### 核心架构模式

**单 Reactor + 线程池**：

- 一个 EventLoop 线程负责所有 I/O 事件（accept / read / write），不执行业务逻辑
- 线程池只做 HTTP 解析 + 路由匹配 + 响应构建，处理完后通过 `queueInLoop` 把响应投递回事件循环写回
- 这样保证了 I/O 线程永远不被业务逻辑阻塞

### 模块依赖关系

```
main.cpp
  ├── Server
  │     ├── EventLoop
  │     │     ├── Poller
  │     │     └── Channel
  │     ├── ThreadPool
  │     ├── HttpConnection ──→ HttpRequestParser, HttpResponse
  │     ├── TimerManager
  │     └── sockutil
  └── Logger
```

依赖方向：上层依赖下层，不存在循环依赖。EventLoop 不依赖 HttpConnection，HttpConnection 通过回调与 Server 解耦。

---

## 3. 概要设计

### 3.1 模块划分

| 模块 | 头文件 | 源文件 | 职责 |
|------|--------|--------|------|
| 网络模块 | `socket_utils.h` | `socket_utils.cpp` | 跨平台 socket 生命周期管理：创建监听、非阻塞设置、读写、关闭、唤醒管道 |
| 事件分发模块 | `poller.h` `channel.h` `event_loop.h` | `poller.cpp` `channel.cpp` `event_loop.cpp` | I/O 多路复用 + 事件循环 + 事件通道抽象 + 跨线程任务队列 |
| 线程池模块 | `thread_pool.h` | `thread_pool.cpp` | 固定线程数工作线程池，条件变量调度任务队列 |
| HTTP 请求解析模块 | `http_request.h` | `http_request.cpp` | 增量状态机解析 HTTP/1.1 请求行、头部、body |
| 路由与响应模块 | `http_response.h` `server.h` | `http_response.cpp` `server.cpp` | 响应构建序列化、API 路由注册与分发、静态文件读取 |
| 连接管理模块 | `http_connection.h` | `http_connection.cpp` | 单个连接的读写回调、生命周期、线程池分发、Keep-Alive |
| 定时器模块 | `timer.h` | `timer.cpp` | 连接超时管理，双 map 快速查找 + 懒删除过期扫描 |
| 日志模块 | `logger.h` | `logger.cpp` | 多级别格式化日志，支持文件输出 |
| 工具公共模块 | `mime_types.h` | — | MIME 扩展名→Content-Type 映射，header-only |

### 3.2 模块交互关系

#### 启动流程

```
main()
  ├── sockutil::init()              // Windows: WSAStartup
  ├── Logger::setLevel()            // 设置日志级别
  ├── EventLoop loop                // 创建事件循环，创建唤醒管道
  ├── ThreadPool pool(4)            // 创建线程池
  ├── pool.start()                  // 启动 4 个工作线程
  ├── Server server(&loop, &pool)   // 创建服务器
  ├── server.registerApi(...)       // 注册 API 路由
  ├── server.start()                // 创建监听 socket → 注册 accept Channel
  └── loop.loop()                   // 进入事件循环（阻塞）
```

#### 连接建立流程

```
epoll 返回 listen_fd 可读
  → Server::handleAccept()
    → accept() 获取 conn_fd
    → setNonBlocking(conn_fd)
    → setNoDelay(conn_fd)
    → onNewConnection(fd)
      → 构造 RequestHandler lambda（捕获路由表 + 根目录）
      → 创建 HttpConnection(loop, pool, fd, handler)
      → conn.init()：设置 read/write/error/close 回调，enableReading
      → timer_mgr_->addTimer(fd, conn, 5000ms)
```

#### 请求处理全流程

```
1. epoll 返回 conn_fd 可读
2. Channel::handleEvent() → read 回调
3. HttpConnection::handleRead()
4. sockutil::recvAll() → 数据追加到 input_buf_
5. HttpRequestParser::parse() → COMPLETE
6. pool_->submit(processRequest)  ← 交给线程池
7. [工作线程] processRequest()
   → 重新解析请求
   → 调用 request_handler_(req, &resp)
     → 先查 API 路由（map 查找）
     → 未命中则查静态文件（stat + fread）
   → resp.serialize() 生成响应文本
8. loop_->queueInLoop(写回任务)  ← 投递回事件循环线程
9. [事件循环线程] output_buf_ += data; enableWriting()
10. epoll 返回 conn_fd 可写
11. HttpConnection::handleWrite()
12. sockutil::sendAll() → 发送响应
13. 发送完毕 → disableWriting()
14. 如果非 Keep-Alive → handleClose()
```

---

## 4. 关键设计决策 & 选择原因

### 4.1 为什么选用 epoll，不选用 select/poll

| 方案 | 连接数 | 时间复杂度 | 触发方式 | 跨平台 |
|------|--------|-----------|---------|--------|
| select | FD_SETSIZE 限制（通常 1024） | O(n) 每次全量扫描 | 水平触发 | ✅ |
| poll | 无硬限制 | O(n) 每次全量扫描 | 水平触发 | ✅ |
| **epoll** | **无硬限制** | **O(1) 只返回就绪 fd** | **支持边缘触发** | ❌ Linux only |

**选择**：Linux 用 epoll，Windows 降级为 select。原因：
- epoll 是 Linux 高并发网络的标准方案，O(1) 就绪通知，轻松支撑万级连接
- select 在 Windows 上是唯一原生选项（IOCP 过于复杂，不符合轻量定位）
- 通过 Poller 抽象层隔离差异，上层代码无感知

### 4.2 为什么用单 Reactor 模型，不用多 Reactor

| 模型 | 复杂度 | 适用场景 |
|------|--------|---------|
| 单 Reactor | 低 | 连接数 < 万级，业务处理快 |
| 单 Reactor + 线程池 | **中** | **连接数 < 万级，业务处理可耗时** |
| 多 Reactor（主从） | 高 | 连接数万级以上 |

**选择**：单 Reactor + 线程池。原因：
- I/O 线程只做 accept / read / write，不执行业务逻辑，单线程足够
- 业务逻辑（解析、路由、文件读取）交给线程池，不阻塞事件循环
- 代码量比多 Reactor 少一半，调试简单，适合轻量级定位
- 实测 4 线程即可满足小规模部署需求

### 4.3 为什么用线程池、大小设计依据

- **为什么用**：HTTP 请求的解析和静态文件读取可能耗时（磁盘 I/O、大 body），如果直接在事件循环线程处理，会阻塞所有连接
- **大小依据**：默认 4 线程，上限 16。轻量服务器并发量有限，4 线程足以覆盖 CPU 核心数；16 线程为极端场景预留。不做动态扩缩，保持实现简单

### 4.4 非阻塞 I/O vs 阻塞 I/O

**选择**：全部 socket 设为非阻塞。原因：
- 事件循环是单线程，任何阻塞都会卡死所有连接
- 非阻塞 read/write 配合 epoll，只有真正可读/可写时才调用，避免空转
- `recvAll` / `sendAll` 内部正确处理 EAGAIN / EWOULDBLOCK，保证不丢失数据

### 4.5 为什么只实现 HTTP/1.1，不做 HTTP/2 和 HTTPS

| 特性 | 实现成本 | 收益（轻量场景） |
|------|---------|----------------|
| HTTP/1.1 | 低（手写状态机 ~150 行） | 核心需求 |
| HTTP/2 | 极高（帧协议、HPACK、流控） | 微，小规模无需多路复用 |
| HTTPS | 高（需 TLS 库引入） | 中，但增加依赖和复杂度 |

**选择**：只做 HTTP/1.1。原因：
- 轻量定位不允许引入 TLS 库（即使 OpenSSL 也太重）
- HTTP/2 的帧协议复杂度远超项目规模，投入产出不成比例
- 如需 HTTPS，建议前端放 Nginx 做反向代理

### 4.6 静态资源处理方案选择

| 方案 | 优点 | 缺点 |
|------|------|------|
| mmap 映射 | 零拷贝，大文件快 | 跨平台兼容差，代码复杂 |
| read 系统调用 | 标准 POSIX | 和 fopen 本质相同 |
| **fread 标准库** | **跨平台、简单可靠** | **多一次用户态拷贝** |

**选择**：`fread`。原因：
- 跨平台零适配，Windows/macOS/Linux 统一行为
- 轻量服务器处理的文件通常不大（KB~MB 级），多一次拷贝可忽略
- 代码简洁，3 行读完整个文件

### 4.7 跨平台兼容设计决策

| 差异点 | Linux | Windows | 处理方式 |
|--------|-------|---------|---------|
| socket 类型 | `int` | `SOCKET` (unsigned) | `sock_fd_t` 类型别名 |
| 无效 socket | `-1` | `INVALID_SOCKET` | `SOCK_INVALID` 常量 |
| 关闭 socket | `close()` | `closesocket()` | `sockutil::closeSocket()` |
| 非阻塞 | `fcntl(O_NONBLOCK)` | `ioctlsocket(FIONBIO)` | `sockutil::setNonBlocking()` |
| I/O 多路复用 | `epoll` | `select` | `Poller` 抽象层，编译期 `#ifdef` |
| 唤醒管道 | `pipe()` | 回环 TCP socket 对 | `sockutil::createWakeupPipe()` |
| 初始化/清理 | 无需 | `WSAStartup/WSACleanup` | `sockutil::init/cleanup()` |
| 错误码 | `errno` + EAGAIN | `WSAGetLastError()` + WSAEWOULDBLOCK | `recvAll/sendAll` 内部分支 |
| 文件状态 | `stat()` | `_stat()` | `STAT_FUNC` 宏 |
| 命名冲突 | 无 | `ERROR`/`DELETE` 等宏 | `LERROR`/`PARSE_ERROR` 避让 |

**原则**：所有平台差异封装在 `socket_utils` 和 `poller` 两个模块中，上层模块不出现 `#ifdef _WIN32`。

### 4.8 不引入重型第三方库的原因

- 项目定位轻量，引入 libevent / Boost.Asio / OpenSSL 会膨胀代码和二进制体积
- 零依赖编译：clone 后只需 CMake + 编译器，无需联网下载
- 手写 HTTP 解析器虽不如成熟库健壮，但 2000 行总量可完全掌控

### 4.9 内存管理与资源释放设计决策

| 资源 | 管理方式 | 释放时机 |
|------|---------|---------|
| socket fd | `HttpConnection` 析构时 `closeSocket()` | 连接关闭 → `shared_ptr` 引用归零 → 析构 |
| Channel 对象 | `HttpConnection` 持有 `unique_ptr<Channel>` | 连接关闭时随 HttpConnection 释放 |
| HttpConnection | `shared_ptr` 引用计数 | Channel 回调通过 `shared_from_this()` 延长生命周期 |
| 定时器节点 | `weak_ptr<HttpConnection>` | 连接已关闭时 `lock()` 返回空，跳过关闭 |
| 线程池任务 | `std::function` + `std::queue` | 任务执行完自动销毁 |
| 日志文件 | `FILE*` 静态变量 | 进程退出时由 OS 回收 |

**核心策略**：`HttpConnection` 继承 `enable_shared_from_this`，Channel 回调捕获 `shared_ptr`，保证回调执行时对象一定存活。连接关闭时 `disableAll + removeChannel`，Channel 不再触发回调，`shared_ptr` 引用归零后自动析构关闭 fd。

---

## 5. 详细设计

### 5.1 网络模块（socket_utils）

**核心功能**：屏蔽 Linux / Windows 的 socket API 差异。

**核心函数**：

| 函数 | 功能 |
|------|------|
| `init()` / `cleanup()` | Windows WSA 初始化/清理，Linux 空操作 |
| `createListener(port)` | 创建 TCP socket → SO_REUSEADDR → bind → listen，返回 fd |
| `setNonBlocking(fd)` | Linux: `fcntl(O_NONBLOCK)`，Windows: `ioctlsocket(FIONBIO)` |
| `setNoDelay(fd)` | 禁用 Nagle 算法，降低小包延迟 |
| `recvAll(fd, buf, zero)` | 非阻塞循环 recv，数据追加到 buf，处理 EINTR/EAGAIN，zero 标记对端关闭 |
| `sendAll(fd, buf)` | 非阻塞循环 send，已发送部分从 buf 头部移除，处理 EINTR/EAGAIN |
| `createWakeupPipe(read_fd, write_fd)` | Linux: `pipe()`，Windows: 回环 TCP socket 对 |
| `wakeup(write_fd)` | 向写端发送 1 字节，唤醒事件循环 |
| `ignoreSigpipe()` | Linux: `signal(SIGPIPE, SIG_IGN)`，Windows: 空操作 |

**关键数据结构**：`sock_fd_t` 类型别名（Linux: `int`，Windows: `SOCKET`），`SOCK_INVALID` 常量。

**唤醒管道设计**：Linux 用 `pipe()` 创建，Windows 因无 `pipe()` 用回环 TCP 连接模拟——server socket bind 到 loopback → client connect → accept 得到 read_fd，client_fd 即 write_fd。两端都可设为非阻塞并加入 epoll/select 监听。

### 5.2 事件分发模块（Poller + Channel + EventLoop）

#### Poller

**核心功能**：I/O 多路复用统一接口。

**Linux 实现**（epoll）：

```cpp
// 构造: epoll_create1(EPOLL_CLOEXEC)
// 添加: epoll_ctl(EPOLL_CTL_ADD, fd, &ev)   ev.data.ptr = ch
// 修改: epoll_ctl(EPOLL_CTL_MOD, fd, &ev)
// 删除: epoll_ctl(EPOLL_CTL_DEL, fd, &ev)
// 等待: epoll_wait() → 遍历就绪事件 → 从 data.ptr 取回 Channel*
```

epoll_event.data.ptr 存放 Channel 指针，避免 fd → Channel 的全局映射表。epoll 事件翻译回 Channel 内部事件常量（kRead/kWrite/kError/kHangup）。

**Windows 实现**（select）：

```cpp
// 维护 unordered_map<int, Channel*> channels_
// 每次poll: 遍历 channels_ 构建三个 fd_set
// select 返回后: 遍历 channels_ 检查 FD_ISSET 设置 revents
```

#### Channel

**核心功能**：封装一个 fd + 关注事件 + 就绪回调。

**关键数据**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `fd_` | `int` | 文件描述符 |
| `events_` | `int` | 关注的事件（kRead=1, kWrite=2） |
| `revents_` | `int` | Poller 返回的就绪事件 |
| `read_cb_` 等 | `std::function<void()>` | 事件回调 |

**事件分发逻辑**（handleEvent）：
1. 挂起且不可读 → close 回调
2. 错误 → error 回调
3. 可读 → read 回调
4. 可写 → write 回调

#### EventLoop

**核心功能**：Reactor 核心，驱动事件循环 + 跨线程任务队列。

**主循环**：

```cpp
while (!quit_) {
    active = poller_->poll(100ms);  // 最多等 100ms
    for (ch : active) ch->handleEvent();
    doPendingTasks();               // 执行跨线程投递的任务
}
```

**跨线程投递**：
- `queueInLoop(task)`：加锁入队，若不在循环线程则 `wakeup()` 写管道
- `runInLoop(task)`：若已在循环线程直接执行，否则 `queueInLoop`
- `doPendingTasks()`：swap 出任务队列后逐一执行，缩小锁粒度

**唤醒机制**：EventLoop 构造时创建唤醒管道，read_fd 注册为 Channel 监听可读。工作线程写 1 字节到 write_fd，事件循环被唤醒后读空管道，然后执行 pending tasks。

### 5.3 线程池模块（ThreadPool）

**核心功能**：固定线程数任务队列。

**核心逻辑**：

```cpp
// 工作线程主循环
while (true) {
    unique_lock lock(mutex);
    cond_.wait(lock, []{ return !running_ || !tasks_.empty(); });
    if (!running_ && tasks_.empty()) return;  // 优雅退出
    task = move(tasks_.front());
    tasks_.pop();
    lock.unlock();
    task();  // 执行任务
}
```

**线程数**：构造时指定，4~16。`start()` 创建线程，`stop()` 设 `running_=false` 并 `notify_all`，等待所有线程 `join`。

### 5.4 HTTP 请求解析模块（HttpRequestParser）

**核心功能**：增量解析 HTTP/1.1 请求，支持多次 `parse()` 调用拼合数据。

**状态机**：

```
LINE ──→ HEADERS ──→ BODY ──→ DONE
  │          │         │
  │  解析失败 │  解析失败│ 数据不足
  ▼          ▼         ▼
PARSE_ERROR  返回      返回
             INCOMPLETE INCOMPLETE
```

**解析步骤**：

1. **LINE**：找 `\r\n`，切出请求行，解析 Method / URI / Version
2. **HEADERS**：找 `\r\n\r\n`，逐行按 `:` 分割 key/value
3. **BODY**：POST 方法根据 Content-Length 读取指定长度；GET/HEAD 跳过此步
4. **DONE**：返回 COMPLETE

**URI 解析**：`/path?query` 分离为 path 和 query。`/` 默认映射为 `/index.html`。

**Keep-Alive 判断**：查 `Connection` 头，值为 `keep-alive` 或 `Keep-Alive` 则为长连接；无此头时 HTTP/1.1 默认 Keep-Alive。

### 5.5 路由与响应模块

#### Server

**核心功能**：TCP 监听 + 连接接纳 + 路由分发。

**路由注册**：`registerApi(path, handler)` 将路径映射到 `std::function<void(const HttpRequest&, HttpResponse*)>` 回调。

**路由回调构造**：每次新建连接时，将 `api_handlers_`（map 副本）和 `root_dir_` 捕获进 lambda，作为 `HttpConnection::RequestHandler` 注入。这避免了 HttpConnection 直接依赖 Server。

**路由匹配顺序**：
1. 精确匹配 API 路由（`std::map::find`）
2. 未命中则尝试静态文件：`root_dir_ + path` → `stat()` 检查 → `fread()` 读取

#### HttpResponse

**核心功能**：构建并序列化 HTTP 响应。

**序列化格式**：

```
HTTP/1.1 200 OK\r\n
Connection: keep-alive\r\n      (或 close)
Keep-Alive: timeout=300\r\n     (Keep-Alive 时)
Content-Length: 1234\r\n
Server: LightWebServer\r\n
Content-Type: text/html\r\n
\r\n
<body>
```

**错误响应**：`makeError(code, msg)` 生成 HTML 错误页面，Connection: close。

### 5.6 连接管理模块（HttpConnection）

**核心功能**：单个客户端连接的完整生命周期。

**关键流程**：

| 回调 | 行为 |
|------|------|
| `handleRead()` | recvAll → parse → COMPLETE 则 submit 到线程池，PARSE_ERROR 则回 400 |
| `handleWrite()` | sendAll → 写完则 disableWriting，非 Keep-Alive 则 handleClose |
| `handleError()` | 回 500 |
| `handleClose()` | disableAll + removeChannel，标记 disconnected |

**线程安全**：
- `handleRead/Write/Close` 在事件循环线程执行，无竞争
- `processRequest` 在工作线程执行，完成后通过 `queueInLoop` 投递回事件循环
- `queueInLoop` 内部加锁，保证任务队列线程安全

**数据流**：`input_buf_` 只在事件循环线程读写，`processRequest` 拷贝数据到工作线程处理，`output_buf_` 只在事件循环线程写入（通过 `queueInLoop` 保证）。

### 5.7 定时器模块（TimerManager）

**核心功能**：管理连接超时，自动关闭空闲连接。

**数据结构**：

```
fd_to_expire_:  map<int, uint64_t>        // fd → 过期时间，O(log n) 查找/删除
expire_queue_:  multimap<uint64_t, Node>  // 过期时间 → 节点，O(log n) 插入，O(1) 取最早
```

**添加/更新**：先 `removeTimer(fd)` 删除旧节点，再计算 `expire = nowMs() + timeout`，同时插入两个 map。

**过期扫描**：从 `expire_queue_` 头部开始，过期时间 ≤ 当前时间则取出、删除、关闭连接；遇到未过期的停止。

**懒删除**：连接正常关闭时不会显式调用 `removeTimer`，定时器到期时 `weak_ptr::lock()` 检查连接是否已关闭，已关闭则跳过。这避免了在连接关闭热路径上操作定时器。

### 5.8 日志模块（Logger）

**核心功能**：格式化日志输出。

**格式**：`[时间] [级别] 文件:行号 - 内容`

**级别**：LDEBUG < LINFO < LWARN < LERROR，低于设定级别不输出。

**输出**：默认 stdout，可通过 `setOutputFile(path)` 切换到文件。每条日志后立即 flush。

**宏**：`LOG_INFO(...)` / `LOG_ERROR(...)` 等，自动填入 `__FILE__` 和 `__LINE__`。

### 5.9 工具公共模块（mime_types）

**核心功能**：根据文件扩展名返回 Content-Type。

**实现**：header-only，内联函数 + `static const` 本地 map，覆盖 22 种常见扩展名。未命中返回 `application/octet-stream`。

---

## 6. 核心工作流程

### 6.1 服务启动初始化流程

```
1. main() 解析命令行参数 (-p/-t/-r/-l)
2. 参数校验（端口范围 1-65535，线程数 1-64）
3. sockutil::init() — Windows 初始化 Winsock
4. Logger 设置级别和输出目标
5. 创建 EventLoop — 构造 Poller，创建唤醒管道，注册 wakeup Channel
6. 创建 ThreadPool — 指定线程数，调用 start() 启动工作线程
7. 创建 Server — 传入 loop/pool/port/root_dir
8. 注册 API 路由 — registerApi("/api/hello", ...) / registerApi("/api/echo", ...)
9. Server::start() — 创建监听 socket，设置非阻塞，注册 accept Channel
10. loop.loop() — 进入主循环，阻塞运行
```

### 6.2 客户端连接接入流程

```
1. epoll/select 报告 listen_fd 可读
2. Channel::handleEvent() → read 回调 → Server::handleAccept()
3. 循环调用 accept() 直到返回 SOCK_INVALID（非阻塞，ET 模式需循环取完）
4. 对每个新 conn_fd：
   a. setNonBlocking + setNoDelay
   b. 构造 RequestHandler lambda（捕获路由表 + 根目录）
   c. new HttpConnection(loop, pool, fd, handler)
   d. conn.init() — 设置 4 个回调，enableReading
   e. timer_mgr_->addTimer(fd, conn, 5000ms)
```

### 6.3 HTTP 请求解析流程

```
1. epoll/select 报告 conn_fd 可读
2. HttpConnection::handleRead()
3. sockutil::recvAll(fd, input_buf_, zero) — 非阻塞循环读取
4. 如果 zero=true（对端关闭）→ handleClose()，结束
5. HttpRequestParser::parse(input_buf_)
6. 状态机逐步解析：
   a. LINE → 找 \r\n → 解析 Method + Path + Version
   b. HEADERS → 找 \r\n\r\n → 逐行按 : 分割
   c. BODY → POST 按 Content-Length 读取
7. 返回 COMPLETE / INCOMPLETE / PARSE_ERROR
```

### 6.4 路由匹配 & 业务处理流程

```
[工作线程] processRequest(input_data)
1. 重新用独立 parser 解析 input_data
2. 调用 request_handler_(req, &resp)
3. lambda 内部：
   a. map.find(req.path) 查 API 路由 → 命中则执行回调，返回
   b. 未命中则拼 root_dir + path → stat() 检查文件
   c. 文件不存在 → 404
   d. 不是普通文件 → 404
   e. fopen → fread → 读取文件内容
   f. MIME 类型查找（扩展名 → Content-Type）
   g. 设置 200 + body
4. resp.serialize() 生成完整 HTTP 响应文本
5. loop_->queueInLoop() 投递回事件循环
```

### 6.5 响应返回 & 连接释放流程

```
1. [事件循环线程] 执行 queueInLoop 投递的任务
2. output_buf_ += response_data
3. enableWriting() — 注册可写事件
4. epoll/select 报告 conn_fd 可写
5. HttpConnection::handleWrite()
6. sockutil::sendAll(fd, output_buf_) — 非阻塞循环写入
7. 写完 → disableWriting()
8. 判断 Keep-Alive：
   a. keep_alive_=true → 保持连接，等待下一个请求
   b. keep_alive_=false → handleClose()
9. handleClose()：
   a. connected_ = false
   b. channel_->disableAll() — 取消所有事件
   c. loop_->removeChannel(ch) — 从 epoll/select 移除
   d. shared_ptr 引用归零 → ~HttpConnection() → closeSocket(fd)
```

### 6.6 优雅退出流程

```
1. 收到退出信号（当前为 Ctrl+C，未注册信号处理）
2. EventLoop::quit_ = true
3. loop() 退出 while 循环
4. main() 调用 pool.stop() — 设置 running_=false，notify_all，join 所有工作线程
5. sockutil::cleanup() — Windows WSACleanup
6. 局部变量析构：Server → EventLoop → 关闭唤醒管道
```

> **当前限制**：未注册 SIGINT/SIGTERM 信号处理，Ctrl+C 会直接终止进程。优雅退出需要后续补充。

---

## 7. 功能特性清单

### 已实现特性

- [x] 基于 epoll（Linux）/ select（Windows）的 Reactor 高并发模型
- [x] 非阻塞 I/O，事件驱动
- [x] HTTP/1.1 GET / POST / HEAD 请求解析
- [x] 增量解析器，支持 TCP 分片到达
- [x] 静态资源服务（22 种 MIME 类型自动识别）
- [x] API 路由注册与分发
- [x] Keep-Alive 长连接（HTTP/1.1 默认开启）
- [x] 线程池并发处理（可配置 1~16 线程）
- [x] 连接超时管理（默认 5 秒，Keep-Alive 5 分钟）
- [x] 跨线程任务投递（pipe/eventfd 唤醒机制）
- [x] 400 / 404 / 500 / 200 标准状态码
- [x] 命令行参数配置（端口/线程/根目录/日志文件）
- [x] 多级别日志（DEBUG/INFO/WARN/ERROR）
- [x] CMake 跨平台构建（Linux ELF + Windows PE）
- [x] 零外部依赖

### 未实现 & 刻意不实现特性

| 特性 | 原因 |
|------|------|
| HTTPS / TLS | 引入 OpenSSL 等重型依赖，违背轻量定位；生产环境建议 Nginx 反代 |
| HTTP/2 | 帧协议+HPACK 压缩实现复杂度极高，轻量场景无收益 |
| 分块传输编码 (chunked) | 增加解析器复杂度，小文件场景直接 Content-Length 更简单 |
| 多 Reactor / 多进程 | 连接规模未到需要多 Reactor 的程度；单 Reactor + 线程池已够用 |
| sendfile 零拷贝 | 不跨平台（Windows 无等效系统调用），fread 在小文件场景差距可忽略 |
| 信号处理（SIGINT 优雅退出） | 保持 main() 简洁，后续可加 |
| 配置文件 | 命令行参数已满足轻量需求；配置文件增加解析代码和文件格式依赖 |
| 访问日志 | 当前只有运行日志；访问日志可后续扩展 |
| 范围请求 (Range) | 小文件场景不需要断点续传 |
| CGI / FastCGI | 过重，API 路由已覆盖动态内容需求 |
| WebSocket | 协议完全不同，不属于 HTTP 服务器范畴 |

---

## 8. 项目限制与约束

### 并发连接数上限

| 平台 | 限制 | 原因 |
|------|------|------|
| Linux | 受系统 `ulimit -n` 限制，默认 1024 | 单进程 fd 上限 |
| Windows | select 的 `FD_SETSIZE` 默认 64（代码未重定义） | Windows select 机制限制 |

可通过 `ulimit -n 65535`（Linux）或重定义 `FD_SETSIZE`（Windows）提升。

### 单请求报文大小限制

- 无硬限制。`input_buf_` 是 `std::string`，理论上受内存约束
- `recvAll` 每次读 4096 字节，大请求会多次读取
- 未做请求体大小校验，超大 POST body 可能耗尽内存

### 不支持的 HTTP 特性

- 分块传输编码 (Transfer-Encoding: chunked)
- 范围请求 (Range / Accept-Range)
- 100 Continue
- HTTP 管道化 (pipelining)
- 内容编码 (Content-Encoding: gzip/deflate)
- 请求体多部分上传 (multipart/form-data)

### 平台差异限制

| 差异 | 说明 |
|------|------|
| Windows select 性能 | 连接数超过 64 时性能急剧下降，不适合高并发 |
| Windows 唤醒管道 | 用回环 TCP 模拟，比 Linux pipe 多一次 TCP 握手 |
| Timer 精度 | 使用 `std::chrono::steady_clock`，精度取决于系统（通常 1ms） |
| SIGPIPE | Linux 需显式忽略，Windows 无此信号 |

### 部署运行环境约束

- 需要文件系统访问权限（读取静态文件）
- 需要网络监听权限（绑定端口，1024 以下需 root/管理员）
- 线程池任务在子线程执行，不支持任务取消
- 当前未实现热加载/配置重载，修改 API 路由需重启

---

## 9. 编译 & 运行说明

### 编译步骤

**一键构建**：

```bash
./build.sh            # 构建 lin64 + win64
./build.sh lin64      # 仅 Linux
./build.sh win64      # 仅 Windows
```

**手动编译（Linux）**：

```bash
mkdir build/lin64 && cd build/lin64
cmake ../.. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel
```

**手动编译（Windows 交叉）**：

```bash
mkdir build/win64 && cd build/win64
cmake ../.. -DCMAKE_TOOLCHAIN_FILE=../toolchain-mingw64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel
```

### 启动参数

```bash
./webserver -p 8080 -t 4 -r ./www -l server.log
```

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `-p` | 监听端口 | 8080 |
| `-t` | 线程池大小 | 4 |
| `-r` | 静态文件根目录 | `./www` |
| `-l` | 日志文件路径 | stdout |
| `-h` | 显示帮助 | — |

### 目录结构

```
LightWebServer/
├── CMakeLists.txt               # CMake 构建配置
├── toolchain-mingw64.cmake      # MinGW-w64 交叉编译工具链
├── build.sh                     # 一键构建脚本
├── main.cpp                     # 程序入口
├── src/
│   ├── socket_utils.h/cpp       # 跨平台 socket 工具
│   ├── logger.h/cpp             # 日志
│   ├── mime_types.h             # MIME 类型 (header-only)
│   ├── channel.h/cpp            # I/O 事件通道
│   ├── poller.h/cpp             # I/O 多路复用 (epoll/select)
│   ├── event_loop.h/cpp         # 事件循环 (Reactor)
│   ├── thread_pool.h/cpp        # 线程池
│   ├── timer.h/cpp              # 连接超时
│   ├── http_request.h/cpp       # HTTP 请求解析
│   ├── http_response.h/cpp      # HTTP 响应构建
│   ├── http_connection.h/cpp    # HTTP 连接处理
│   └── server.h/cpp             # 服务器核心
├── www/
│   └── index.html               # 默认首页
└── README.md
```

---

## 10. 扩展与优化方向

### 可后续新增的功能

| 功能 | 难度 | 扩展点 |
|------|------|--------|
| SIGINT/SIGTERM 优雅退出 | 低 | main.cpp 注册信号 → `loop.quit()` |
| 访问日志 | 低 | Server::onNewConnection 和 handleClose 中记录请求路径、状态码 |
| 请求体大小限制 | 低 | HttpRequestParser 在 BODY 状态检查 body_needed_ 上限 |
| 正则路由匹配 | 中 | 替换 `map<string, handler>` 为路由树，支持 `/api/:id` 模式 |
| 配置文件 | 中 | 读取 JSON/YAML 配置替代命令行参数 |
| HTTPS 支持 | 高 | 引入 OpenSSL / mbedTLS，在 socket_utils 层加 TLS 包装 |

### 性能优化点

| 优化点 | 当前 | 优化方向 |
|--------|------|---------|
| 静态文件读取 | fread 每次读全文件 | 小文件缓存到内存（LRU），避免重复磁盘 I/O |
| sendfile 零拷贝 | 未使用 | Linux 下对大文件用 `sendfile()` 直接内核态传输 |
| epoll 触发模式 | LT（水平触发） | 改为 ET（边缘触发）+ 循环读写，减少系统调用次数 |
| 响应序列化 | 每次构建完整 string | 预分配 buffer，用 `writev` 分散写（header + body） |
| Timer 扫描 | 每次 poll 后全量扫描 | 改为只在最近定时器到期时才扫描，减少无效遍历 |
| 多 Reactor | 单 Reactor | 主 Reactor 只做 accept，子 Reactor 按线程分管连接 |
| HTTP 解析 | 字符串 substr 频繁拷贝 | 改为零拷贝解析，用指针/索引标记边界 |

### 可扩展模块预留设计

- **RequestHandler 回调机制**：Server 通过 lambda 注入路由逻辑到 HttpConnection，新增路由只需调用 `registerApi`，无需修改连接处理代码
- **Poller 抽象层**：已有 epoll/select 两种实现，可扩展 IOCP（Windows）或 kqueue（macOS）
- **Logger 扩展点**：当前是同步写文件，可扩展为异步日志（后台线程写磁盘）
- **ThreadPool 扩展点**：可增加任务优先级、任务取消、线程数动态调整
