# 技术设计文档 — 游戏启动器 + 实时聊天 桌面客户端

> 版本：v0.2（新增账号体系：自助注册 / SQLite 持久化 / 密码加盐哈希）
> 关联文档：[PRD.md](./PRD.md)、[AGENTS.md](../AGENTS.md)（环境/构建/运行/AI 准则）、[DEVLOG.md](./DEVLOG.md)
> 本文档为**实现唯一依据**：协议定义、目录结构、线程模型以本文为准。代码或文档任一变更，必须同步另一方。

---

## 1. 架构总览

系统由三个 CMake 目标组成，**客户端与 mock 服务端共用同一个协议库**：

```
┌────────────────────────┐         ┌────────────────────────┐
│      client (Qt GUI)   │         │   mock_server (CLI)    │
│  ┌──────────────────┐  │         │  ┌──────────────────┐  │
│  │   UI 层 (Widgets)│  │         │  │  ClientSession   │  │
│  ├──────────────────┤  │         │  ├──────────────────┤  │
│  │ 会话/模型层       │  │         │  │  业务分发 / 仓储  │  │
│  │ (Session/Model)  │  │         │  ├──────────────────┤  │
│  ├──────────────────┤  │         │  │  QTcpServer      │  │
│  │ 网络层           │  │         │  ├──────────────────┤  │
│  │ (NetWorker/线程) │  │         │  │  SQLite (mock.db)│  │
│  └──────────────────┘  │         │  └──────────────────┘  │
└───────────┬────────────┘         └─────────┼──────────────┘
            │                                │
            └─────────── TCP (二进制帧) ──────┘
                            ▲
                ┌───────────┴────────────┐
                │  protocol  (共享静态库) │
                │  帧编解码 + 消息类型     │
                └────────────────────────┘
```

**分层职责**

| 层 | 职责 | 不负责 |
|---|---|---|
| protocol | 帧格式、消息类型枚举、JSON 载荷编解码 | 业务逻辑、网络 IO |
| client 网络层 | 连接、心跳、重连、拆帧、收发 | UI 展示、业务状态 |
| client 模型层 | 登录态、好友/会话/消息数据、状态机 | 直接操作 Socket |
| client UI 层 | 渲染、用户交互 | 网络细节 |
| mock_server | 参考服务端：账号注册/登录校验（SQLite 持久化）、好友关系、消息路由与离线存储 | 鉴权加固、管理后台 |

---

## 2. 目录结构

```
final/
├─ CMakeLists.txt              # 顶层：项目定义 + 子目录
├─ AGENTS.md                   # 环境/构建/运行/AI 准则（agent 约定入口，必须在根目录）
├─ docs/
│  ├─ PRD.md                   # 产品需求文档
│  ├─ TECH_DESIGN.md           # 技术设计文档（本文件）
│  └─ DEVLOG.md                # 开发日志（每次开发会话追加一条）
├─ shared/                     # 共享协议库（客户端与服务端共用）
│  ├─ CMakeLists.txt           # 目标名: protocol
│  └─ protocol/
│     ├─ MessageType.h         # 消息类型枚举（唯一真相源）
│     ├─ Frame.h / Frame.cpp   # 帧编解码 + 增量解码器
│     └─ ProtocolVersion.h     # 协议版本号
├─ client/
│  ├─ CMakeLists.txt           # 目标名: client
│  ├─ src/
│  │  ├─ main.cpp
│  │  ├─ net/
│  │  │  ├─ NetWorker.h/.cpp       # 跑在网络线程：QTcpSocket + 心跳 + 重连
│  │  │  └─ NetController.h/.cpp   # 主线程侧门面，封装 invokeMethod 跨线程调用
│  │  ├─ core/
│  │  │  ├─ Session.h/.cpp         # 登录态、自身信息、连接状态机
│  │  │  ├─ FriendModel.h/.cpp     # QAbstractListModel：好友 + 在线状态
│  │  │  ├─ ConversationModel.h/.cpp # 会话列表 + 未读
│  │  │  └─ MessageModel.h/.cpp    # 单个会话的消息列表
│  │  ├─ launcher/
│  │  │  ├─ GameEntry.h            # 游戏条目结构（id/name/exePath/args/cover）
│  │  │  ├─ GameLibrary.h/.cpp     # 读写 games.json、导入/移除、路径校验
│  │  │  └─ GameProcessManager.h/.cpp # QProcess 拉起/跟踪/结束/退出清理
│  │  └─ ui/
│  │     ├─ Theme.h                    # 深色主题色板常量（QSS 占位符来源）
│  │     ├─ LoginDialog.h/.cpp/.ui     # 登录 / 注册（页签切换）
│  │     ├─ MainWindow.h/.cpp/.ui      # 左侧栏（好友/会话/游戏页）+ 右侧内容
│  │     ├─ FriendListWidget.h/.cpp
│  │     ├─ ChatPanel.h/.cpp
│  │     ├─ GameLauncherPanel.h/.cpp
│  │     └─ ImportGameDialog.h/.cpp    # 导入游戏：名称/参数/封面 + 校验提示
│  └─ resources/
│     ├─ resources.qrc         # 资源清单
│     ├─ style/app.qss         # 深色现代样式表（唯一皮肤来源）
│     └─ games.json            # 内置模板（账号首次登录时复制为 games-<userId>.json）
├─ server/
│  ├─ CMakeLists.txt           # 目标名: mock_server
│  ├─ data/                    # 运行期生成 mock.db（不入库）
│  └─ src/
│     ├─ main.cpp
│     ├─ MockServer.h/.cpp     # QTcpServer：接受连接、管理 session 列表
│     ├─ ClientSession.h/.cpp  # 单连接：拆帧、分发、回包
│     ├─ PasswordHash.h/.cpp   # 随机盐 + SHA-256 口令哈希与校验
│     └─ store/
│        ├─ Database.h/.cpp          # QSqlDatabase(SQLite)：连接、建表、迁移
│        ├─ UserRepository.h/.cpp    # 账号注册/查询/校验（用户名唯一）
│        ├─ FriendRepository.h/.cpp  # 好友关系（双向）+ 待处理请求
│        ├─ MessageRepository.h/.cpp # 消息落库 + 离线队列
│        └─ SessionRegistry.h/.cpp   # 在线用户 → ClientSession 映射（内存）
└─ .gitignore / build/         # build/ 为生成目录，不入库
```

---

## 3. 传输层：帧格式

- 传输：**TCP**，端口默认 `9527`（可配置）。
- 字节序：**大端（网络字节序）**。
- 帧结构：长度前缀 + 类型 + 载荷。

```
 偏移  长度(字节)  字段      说明
 0     4           length    = 2(type) + payload 字节数（大端 uint32）
 4     2           type      消息类型，见 §4（大端 uint16）
 6     length-2    payload   UTF-8 编码的 JSON 对象（MVP 阶段）
```

**约束与健壮性**

| 项 | 规则 |
|---|---|
| 最小帧长 | 2（仅类型、空载荷 `{}`） |
| 最大帧长 | 1 MiB；超过直接断连（防止恶意/异常帧撑爆内存） |
| 半包/粘包 | 接收端必须使用**增量解码器**：把字节累积进缓冲区，循环尝试「够 4 字节 → 读长度 → 够 length 字节 → 取完整帧」 |
| 载荷解析失败 | 记录日志、丢弃该帧、回 `ErrorResp`（不崩溃、不断连） |

**参考编解码 API（`shared/protocol/Frame.h`）**

```cpp
// 组帧：type + JSON 对象 → 完整字节流
QByteArray encodeFrame(MsgType type, const QJsonObject& payload);

// 增量解码器：喂入任意长度字节，吐出完整帧
class FrameDecoder {
public:
    void feed(const QByteArray& bytes);
    // 返回 false 表示当前无完整帧（buffered() 为剩余未解析字节）
    bool next(MsgType& outType, QJsonObject& outPayload, QString* err);
    int buffered() const;
};
```

---

## 4. 消息类型与载荷定义

> 所有载荷均为 JSON 对象。`ts` 统一为 **Unix 毫秒（int64）**。`code == 0` 表示成功，非 0 为错误码。
> 命名段位：`0x01xx` 会话类、`0x02xx` 好友类、`0x03xx` 私聊类、`0x04xx` 启动器类（预留）。

### 4.1 枚举（`shared/protocol/MessageType.h`）

```cpp
enum class MsgType : quint16 {
    // 0x01xx 会话 / 连接
    LoginReq        = 0x0101,
    LoginResp       = 0x0102,
    LogoutReq       = 0x0103,
    LogoutResp      = 0x0104,
    HeartbeatReq    = 0x0105,
    HeartbeatResp   = 0x0106,
    ErrorResp       = 0x0107,
    RegisterReq     = 0x0108,
    RegisterResp    = 0x0109,

    // 0x02xx 好友
    FriendListReq   = 0x0201,
    FriendListResp  = 0x0202,
    FriendAddReq    = 0x0203,
    FriendAddResp   = 0x0204,
    FriendRequestPush = 0x0205,   // 服务端 → 客户端：有人加你
    FriendHandleReq = 0x0206,     // 客户端 → 服务端：接受/拒绝
    FriendHandleResp= 0x0207,
    FriendRemoveReq = 0x0208,
    FriendRemoveResp= 0x0209,
    PresencePush    = 0x020A,     // 服务端 → 客户端：好友上下线
    FriendRequestListReq  = 0x020B, // 客户端 → 服务端：主动拉取待处理请求
    FriendRequestListResp = 0x020C, // 服务端 → 客户端：待处理请求全量

    // 0x03xx 私聊
    ChatSendReq     = 0x0301,
    ChatSendResp    = 0x0302,
    ChatPush        = 0x0303,     // 服务端 → 客户端：收到新消息（含离线补推）
    ChatHistoryReq  = 0x0304,
    ChatHistoryResp = 0x0305,
    ChatReadReq     = 0x0306,
    ChatReadPush    = 0x0307,

    // 0x04xx 启动器（预留）
    GameListReq     = 0x0401,
    GameListResp    = 0x0402,
};
```

### 4.2 载荷字段表

**账号规则（服务端校验，违规返回明确错误码）**

| 项 | 规则 |
|---|---|
| `username` | 3–20 字符，仅字母 / 数字 / 下划线；**全局唯一**（注册重复 → 非 0 错误码） |
| `password` | 6–32 字符 |
| `displayName` | 1–20 字符，可与 `username` 不同、允许重复 |

> 注册成功后服务端**不自动建立会话**：客户端切回登录页，随后发 `LoginReq` 完成登录（注册与登录职责分离）。

**会话类**

| 类型 | 方向 | 载荷字段 |
|---|---|---|
| LoginReq `0x0101` | C→S | `{ username:string, password:string, protocolVersion:int }` |
| LoginResp `0x0102` | S→C | `{ code:int, message:string, userId:int, username:string, displayName:string }` |
| LogoutReq `0x0103` | C→S | `{}` |
| LogoutResp `0x0104` | S→C | `{ code:int }` |
| HeartbeatReq `0x0105` | C→S | `{ ts:int64 }` |
| HeartbeatResp `0x0106` | S→C | `{ ts:int64 }` |
| ErrorResp `0x0107` | S→C | `{ code:int, message:string, refType:int }` |
| RegisterReq `0x0108` | C→S | `{ username:string, password:string, displayName:string }` |
| RegisterResp `0x0109` | S→C | `{ code:int, message:string, userId:int, username:string, displayName:string }` |

**好友类**

| 类型 | 方向 | 载荷字段 |
|---|---|---|
| FriendListReq `0x0201` | C→S | `{}` |
| FriendListResp `0x0202` | S→C | `{ friends: [ { userId:int, username:string, displayName:string, online:bool, signature:string } ] }` |
| FriendAddReq `0x0203` | C→S | `{ username:string, remark:string }` |
| FriendAddResp `0x0204` | S→C | `{ code:int, message:string }` |
| FriendRequestPush `0x0205` | S→C | `{ fromUserId:int, username:string, displayName:string, remark:string, ts:int64 }` |
| FriendHandleReq `0x0206` | C→S | `{ fromUserId:int, action:string }`（action ∈ `"accept"` / `"reject"`） |
| FriendHandleResp `0x0207` | S→C | `{ code:int, message:string, fromUserId:int }` |
| FriendRemoveReq `0x0208` | C→S | `{ userId:int }` |
| FriendRemoveResp `0x0209` | S→C | `{ code:int, userId:int }` |
| PresencePush `0x020A` | S→C | `{ userId:int, online:bool, ts:int64 }` |
| FriendRequestListReq `0x020B` | C→S | `{}` |
| FriendRequestListResp `0x020C` | S→C | `{ requests: [ { fromUserId:int, username:string, displayName:string, remark:string, ts:int64 } ] }`（**全量**，客户端整体覆盖） |

**私聊类**

| 类型 | 方向 | 载荷字段 |
|---|---|---|
| ChatSendReq `0x0301` | C→S | `{ toUserId:int, content:string, clientMsgId:string }` |
| ChatSendResp `0x0302` | S→C | `{ code:int, clientMsgId:string, serverMsgId:int64, ts:int64 }` |
| ChatPush `0x0303` | S→C | `{ serverMsgId:int64, fromUserId:int, toUserId:int, content:string, ts:int64, offline:bool }` |
| ChatHistoryReq `0x0304` | C→S | `{ peerUserId:int, beforeTs:int64, limit:int }` |
| ChatHistoryResp `0x0305` | S→C | `{ peerUserId:int, hasMore:bool, peerReadUpTo:int64, messages: [ { serverMsgId:int64, fromUserId:int, toUserId:int, content:string, ts:int64 } ] }`（`peerReadUpTo` = 对端已读到的最大 id，用于重建己方气泡的「已读」状态） |
| ChatReadReq `0x0306` | C→S | `{ peerUserId:int, upToServerMsgId:int64 }`（按单调递增的消息 id 标记已读，避免同毫秒歧义） |
| ChatReadPush `0x0307` | S→C | `{ peerUserId:int, upToServerMsgId:int64 }`（`peerUserId` 为**读消息的人**；发送方据此把 ≤ 该 id 的己方消息标为已读。对方离线则丢弃，不落库） |

**启动器类（预留）**

| 类型 | 方向 | 载荷字段 |
|---|---|---|
| GameListReq `0x0401` | C→S | `{}` |
| GameListResp `0x0402` | S→C | `{ games: [ { id:string, name:string, description:string, cover:string } ] }` |

> MVP 阶段启动器完全本地化（读 `games.json`），`0x04xx` 仅作为协议占位，服务端可返回空列表。

### 4.3 关键时序

**注册**
```
Client                          Server
  │── RegisterReq ──────────────▶│
  │                              │ 校验格式 → 查 username 唯一 → 生成盐 → 存哈希 → 分配 userId
  │◀── RegisterResp(code) ────────│  code==0 成功；重复/非法 → 非 0 + message
```

**登录 + 离线补推**
```
Client                          Server
  │── LoginReq ─────────────────▶│
  │                              │ 校验 → 注册在线 → 遍历好友广播 PresencePush(online)
  │◀── LoginResp ─────────────────│
  │── FriendListReq ────────────▶│
  │◀── FriendListResp ────────────│
  │                              │ 投递该用户离线队列（逐条 ChatPush, offline=true）
  │◀── ChatPush(offline) × N ─────│
  │◀── FriendRequestPush × N ─────│（若有未处理的好友请求）
  │                              │
  │── FriendRequestListReq ─────▶│（客户端主窗口就绪后主动拉取，作为推送的兜底）
  │◀── FriendRequestListResp ─────│（全量待处理请求，客户端覆盖本地列表）
```

**发送私聊**
```
Client                          Server
  │── ChatSendReq(clientMsgId) ─▶│
  │                              │ 落库 → 生成 serverMsgId
  │◀── ChatSendResp(已入队) ──────│
  │                              │ 收件人在线？ ── 是 ─▶ ChatPush(offline=false)
  │                              │             └ 否 ─▶ 入离线队列
```

**心跳与断线判定**
```
Client 每 5s 发 HeartbeatReq；连续 3 次（15s）未收到 HeartbeatResp → 判定断线 → 进入重连
Server 每 30s 未收到任何帧 → 主动断开该连接
```

**客户端自动重连（FR-1.3）**

- 登录成功后在 `Session` 内存保留本次凭据（**不落盘**）；登出、重登失败、进程退出即清空。
- 触发条件有两个：`disconnected`（已连上后掉线）、连接尝试失败（`errorOccurred` 且 socket 从未连上——
  后者 Qt **不会**再发 `disconnected`，必须单独补一次调度，否则重连链会断在半路）。
- 退避：1s → 2s → 4s → 8s → 16s → 30s，之后固定 30s；成功登录后计数归零。
- 重连成功后自动重发 `LoginReq`（复用内存凭据）；服务端按正常登录补推好友列表 / 离线消息，
  主窗口不销毁、`m_uiReady` 不复位，推送直接进入现有模型。
- 自动重登被拒（口令被改 / 账号被删）：清空凭据、退回登录页，原因经 `Session::lastError()` 显示在登录框。
- 用户主动登出：停止重连并清空凭据，因此不会再被自动拉回登录态。

---

## 5. 序列化策略

- **MVP**：帧头为二进制，**载荷用 JSON**（`QJsonDocument`，Qt 原生支持，零额外依赖，调试友好）。
- **抽象seam**：载荷的编解码被隔离在 `protocol` 内，外部只依赖 `QJsonObject`。未来若要换 protobuf / 手写二进制，只需替换该层实现，客户端与服务端业务代码不变。
- 为什么不直接手写二进制：本期无 `protoc` 依赖；手写序列化会显著放大出错面，且与学习目标（网络/并发/线程）重叠度低。载荷格式切换被显式预留。

> 若后续需要二进制载荷，在 `shared/protocol/` 增加 `PayloadCodec.h` 接口 + `JsonCodec` / `BinaryCodec` 两个实现，`FrameDecoder` 面向接口即可。

---

## 6. 客户端线程模型

**原则：网络阻塞绝不发生在 UI 线程；跨线程只通过信号槽/`invokeMethod` 通信，不共享可变状态。**

```
主线程 (UI)                        网络线程 (QThread)
┌───────────────────────────┐     ┌────────────────────────────┐
│ QApplication              │     │ NetWorker (QObject)        │
│ LoginDialog / MainWindow  │     │  ├ QTcpSocket              │
│ Session / *Model          │     │  ├ 接收缓冲 + FrameDecoder  │
│ NetController (门面)      │     │  ├ 心跳 QTimer              │
└────────┬──────────────────┘     │  └ 重连 QTimer(退避)         │
         │  QMetaObject::invokeMethod(QueuedConnection)          │
         └────────────────────────────────────────────────────▶ │
         ◀──────────────────────────────────────────────────── ┘
              signals: connected / disconnected / frameReceived / error
              (自动跨线程 QueuedConnection → 主线程槽)
```

- `NetWorker` 通过 `moveToThread(&m_netThread)` 移入网络线程；其 `QTimer`、`QTcpSocket` 必须在**网络线程内**创建（在响应 `QThread::started` 的槽里创建），否则定时器不触发。
- 出站：`NetController`（主线程门面）调用 `QMetaObject::invokeMethod(netWorker, "...", Qt::QueuedConnection, Q_ARG(...))`，参数类型需 `qRegisterMetaType`。
- 入站：`NetWorker::frameReceived(MsgType, QJsonObject)` 发到主线程，由 `Session` 分发给各 Model。
- 重连退避：1s → 2s → 4s → 8s → 上限 30s；登录成功后重置为 1s。
- 连接状态机：`Disconnected → Connecting → Connected → Reconnecting(→Connecting) → Disconnected`。

**为什么用独立网络线程而非主线程异步 Socket**：`QTcpSocket` 本身异步不阻塞主线程，但把网络层独立成线程更贴近「网络 / 并发」的学习目标，也便于后续加入编解码、压缩等 CPU 工作。若追求最小实现，可退化为主线程异步方案（架构不变，仅去掉 `NetWorker` 的 moveToThread）。

---

## 7. 客户端模块职责

| 模块 | 关键类 | 职责 | 依赖 |
|---|---|---|---|
| 网络 | `NetWorker` | Socket 生命周期、心跳、重连、拆帧、组帧发送 | `protocol`、`Qt6::Network` |
| 网络门面 | `NetController` | 主线程安全的发送接口、信号转发 | `NetWorker` |
| 会话 | `Session` | 自身信息、连接状态机、断线退避重连与自动重登、消息分发中枢 | `NetController` |
| 会话列表 | `ConversationModel` | 有历史消息的会话、最后一条预览、未读数（本地状态，不跨设备同步） | — |
| 好友 | `FriendModel` | `QAbstractListModel`：好友数据 + 在线状态 | `Session` |
| 消息 | `MessageModel` | 单会话消息列表，发送状态（发送中/已送达/失败） | `Session` |
| 游戏库 | `GameLibrary` | 读写当前账号的 `games-<userId>.json`、导入/移除条目、路径校验 | `Qt6::Core` |
| 进程管理 | `GameProcessManager` | `QProcess` 启动/跟踪/结束/退出清理 | `GameLibrary`、`Qt6::Core` |
| 游戏页 | `GameLauncherPanel` / `ImportGameDialog` | 卡片列表、添加/启动/结束/移除 | `GameLibrary`、`GameProcessManager` |
| UI | `LoginDialog` / `MainWindow` / 各面板 | 渲染与交互 | Models |

**UI 布局（MainWindow）**

```
┌────────────┬──────────────────────────────────────────┐
│ 头像/昵称   │  会话面板（ChatPanel）                    │
│ 连接状态●   │   ┌ 消息列表（MessageModel）             │
│────────────│   ├ 未读/历史加载                        │
│ 会话        │   └ 输入框 + 发送                         │
│ 好友  ←Tab→ │                                          │
│ 游戏库      │  或 游戏面板（GameLauncherPanel）         │
│            │     卡片列表 + 添加/启动/结束/移除         │
│            │     + 运行状态与时长                      │
└────────────┴──────────────────────────────────────────┘
```

### 7.1 游戏启动器设计（M4）

**存储（唯一读写源，按账号隔离）**

- 路径：`QStandardPaths::AppDataLocation` + `/games-<userId>.json`
  （Windows 实际为 `%APPDATA%\GameLauncherChat\games-<userId>.json`；`userId` 由登录响应给出）
- 同一台机器上不同账号各有一份游戏库，互不影响；切换账号即切换文件
- 首次登录（该账号的文件不存在）时，从 qrc 中的 `:/games.json` 复制一份模板
- 读取失败（JSON 语法错误 / 根不是数组）：**不清空、不覆盖**，列表置空并在页面顶部提示原因，避免一次解析失败毁掉用户数据
- 写入：`QSaveFile` 原子替换，避免写一半崩溃导致文件损坏

**JSON 结构**

```json
[
  {
    "id": "6f1c2d9e-...",          // UUID，导入时生成
    "name": "示例游戏",              // 显示名，默认取 exe 文件名
    "exePath": "D:\\Games\\foo\\foo.exe",
    "args": ["-windowed"],          // 可为空数组
    "cover": "D:\\Games\\foo\\cover.png", // 可为空字符串
    "addedAt": 1761000000000        // 毫秒时间戳
  }
]
```

**`GameLibrary`**

- 构造时传入 `userId`，据此决定存储文件；同一进程内只服务当前登录账号
- `load()` / `save()` / `add(entry)` / `remove(id)` / `contains(exePath)`
- 去重键：`QFileInfo::canonicalFilePath()`（大小写与斜杠差异视为同一路径）
- 导入校验：文件存在 → 后缀 `.exe` → 非目录；失败返回可读原因
- 读取后不改写文件（只有 add/remove 才写盘）

**`GameProcessManager`**

- 每个游戏一个 `QProcess`，`setProgram(exePath)`、`setArguments(args)`、`setWorkingDirectory(exe 所在目录)`
  （工作目录必须设为 exe 目录，否则很多游戏找不到自己的资源）
- 状态机：`NotRunning → Starting → Running → Exited`
- 运行时长：单个 `QTimer`（1s）遍历运行中的进程刷新，不给每个游戏各开一个定时器
- 失败：`QProcess::errorOccurred` → 状态回 `NotRunning` 并回报原因（如 `FailedToStart`）
- 结束：`kill()`；退出原因与退出码记录到条目上，供 UI 展示（异常退出用 `kDanger` 提示）
- 退出清理：`MainWindow` 关闭 / 应用退出时调用 `killAll()`，只处理**自己拉起的**进程（FR-4.5）
- 全程不阻塞：不使用 `waitForStarted/waitForFinished`，避免卡 UI 线程

**导入流程（FR-4.8）**

```
[+ 添加游戏]
   → QFileDialog 选 *.exe
   → ImportGameDialog：名称（默认 exe 基名）/ 启动参数 / 封面（可选）
   → GameLibrary::add()
       ├─ 路径非法 / 不存在 → 对话框内红字提示，不关闭
       └─ 已在库中 → 提示「该程序已在游戏库中」
   → 保存成功 → 列表刷新并选中新条目
```

**移除（FR-4.9）**：仅从当前账号的 `games-<userId>.json` 删除记录；若该游戏正在运行，先提示「请先结束进程」。

**UI 约定**

- 右侧 `QTabWidget`：「会话」「游戏」
- 游戏卡片（列表项）：名称、exe 路径（次文本、省略号）、状态点 + 文案、运行时长；右侧按钮随状态切换「启动 / 结束」
- 右上角「+ 添加游戏」（次按钮样式）；用户导入项提供「移除」
- 空态：`还没有游戏，点右上角添加`
- exe 不存在（外部被删）：卡片标记「路径失效」，启动按钮禁用（FR-4.6）

**视觉规范（深色现代风格）**

**基调**：深色低饱和底 + 单一主色强调；卡片化分区、圆角、充足留白；去掉原生控件的默认边框与灰色底。

**色板（Dark）**

| 用途 | 常量（`client/src/ui/Theme.h`） | 值 |
|---|---|---|
| 主色 / 强调 | `kAccent` | `#5B8DEF` |
| 主色-悬停 / 按压 | `kAccentHover` / `kAccentPressed` | `#74A0F5` / `#4A79D6` |
| 窗口底色 | `kBgBase` | `#1B1D21` |
| 侧栏 / 面板底 | `kBgPanel` | `#22252A` |
| 卡片 / 输入框底 | `kBgCard` | `#2A2E35` |
| 列表项 hover / 选中 | `kBgHover` / `kBgSelected` | `#313640` / `#374050` |
| 分隔线 | `kBorder` | `#353A42` |
| 主文本 / 次文本 | `kTextPrimary` / `kTextSecondary` | `#E6E8EB` / `#9AA1AC` |
| 在线 / 连接中 / 断线 | `kOnline` / `kWarning` / `kOffline` | `#3FB950` / `#D29922` / `#6E7681` |
| 失败 / 危险 | `kDanger` | `#E5534B` |

**形状 / 字体 / 间距**

| 项 | 规范 |
|---|---|
| 圆角 | 卡片 10px；按钮 / 输入框 8px；头像与状态点用圆形 |
| 字体 | `Segoe UI`, `Microsoft YaHei UI`；正文 13px；面板标题 15px semibold；时间戳 11px 次文本色 |
| 间距刻度 | 只用 **4 / 8 / 12 / 16 / 24**，避免随手数值 |
| 控件高度 | 按钮 / 输入框 32px；好友与游戏列表项 44px |

**关键组件状态**

| 组件 | 规范 |
|---|---|
| 主按钮 | 实心主色 + 白字；hover 提亮、pressed 压暗、disabled 透明度 40% |
| 次按钮 | 透明底 + 1px `kBorder` 描边 + 主文本；hover 底色 `kBgCard`、描边变主色 |
| 输入框 | 底 `kBgCard` + 1px `kBorder`；focus 时描边主色；圆角 8px |
| 好友 / 游戏列表项 | hover `kBgHover`；选中：底色 `kBgSelected` + 左侧 3px 主色竖条；未读：主色圆点 + 文本加粗 |
| 消息气泡 | 自己：靠右、主色底、深色文字；对方：靠左、`kBgCard` 底、主文本；圆角 10px；最大宽度约 70% |
| 连接状态 | 小圆点 + 文案：已连接 `kOnline` / 连接中 `kWarning` / 断线 `kOffline` |
| 滚动条 | 宽 8px、圆角、滑块 `#3A4048`、hover 提亮 |
| 空态 | 居中图标 + 一句次文本提示（如「还没有好友，点右上角添加」） |

**实现约定**

- **唯一皮肤来源**：`client/resources/style/app.qss`，`main.cpp` 启动时加载。
- **颜色只在 `client/src/ui/Theme.h` 定义**；QSS 内用 `@kAccent` 形式占位符，加载时由 `Theme` 批量替换 → 一套 QSS 即可支持以后换肤（§11 扩展点 8）。**禁止在 UI 代码里散落硬编码颜色。**
- **窗口不透明度同样集中在 `Theme.h`**（`kWindowOpacity`，当前 0.94）：登录框与主窗口都调 `setWindowOpacity`，改一处即全局生效；不用 `WA_TranslucentBackground`，避免原生边框阴影与重绘异常。
- 图标优先内嵌 SVG（`Qt6::Svg` 已装）或 `QStyle` 标准图标；**不使用 QML**。
- 窗口默认用系统标题栏；若要做无边框自绘标题栏，另开任务（拖拽与缩放实现成本较高）。

---

## 8. mock 服务端设计

- **单线程事件循环**：`QTcpServer` + 每连接一个 `ClientSession`（持有 `QTcpSocket`）。业务在同一线程内串行处理 → 无并发竞争，**不需要锁**。
- **存储：SQLite（`Qt6::Sql`）**，数据库文件 `server/data/mock.db`（可用启动参数覆盖路径）。
  - `Database`：管理 `QSqlDatabase`（驱动 `QSQLITE`），首次运行自动建表，用 `PRAGMA user_version` 记录 schema 版本。
  - 仓储层：`UserRepository` / `FriendRepository` / `MessageRepository`。
  - `SessionRegistry` 仍为**内存**（在线 socket 无法持久化）。
- **口令安全**：`PasswordHash` — 每个账号一个 16 字节随机盐（`QRandomGenerator::system()`），
  存 `password_salt` + `password_hash`，其中 `password_hash = SHA-256(salt || password)`（`QCryptographicHash`）；
  **不存明文**。校验时按同一方式重算并比较（强度不足见 §12）。
- **注册**：校验格式 → 查 `username` 唯一 → 生成盐与哈希 → 插入 `users` → 返回 `userId`。
- **消息路由**：`ChatSendReq` → 插入 `messages` → 回 `ChatSendResp` → 收件人在线则 `ChatPush(offline=false)`；否则该行保持 `delivered=0`，留待补推。
- **离线补推**：登录成功后查询 `to_user = 自己 AND delivered = 0` 的行，按 `id` 升序逐条 `ChatPush(offline=true)` 并置 `delivered=1`；同时推送其未处理的好友请求。
- **历史分页**：`ChatHistoryReq` 用 `(from_user,to_user)` 双向条件 + `id < beforeId` 倒序取 `limit` 条。
- **在线状态**：登录/登出/断连时向该用户的所有好友广播 `PresencePush`。
- **同账号重复登录**：登录成功前查 `SessionRegistry`；该 `userId` 已有其他在线连接时**保留原连接、拒绝新登录**
  （`LoginResp.code = 8`，message「该账号已在其他设备登录，请先退出后重试」）。单线程事件循环下检查与注册之间不会被插入，
  不需要额外锁；旧连接自然断线后（最长一个空闲超时 30s）该账号才可再次登录。
- **断线**：客户端 Socket 断开 → 从 `SessionRegistry` 移除 → 广播离线。
- **可扩展性预留**：`ClientSession` 只负责拆帧与转发到分发函数；替换为真实服务端时业务层可平移到线程池模型。

**SQLite 表结构（首次运行自动创建）**

```sql
CREATE TABLE users (
  id            INTEGER PRIMARY KEY AUTOINCREMENT,
  username      TEXT    NOT NULL UNIQUE,   -- 登录名，全局唯一
  display_name  TEXT    NOT NULL,
  password_salt TEXT    NOT NULL,          -- 随机盐（hex）
  password_hash TEXT    NOT NULL,          -- SHA-256(salt || password)（hex）
  signature     TEXT    NOT NULL DEFAULT '',
  created_at    INTEGER NOT NULL
);

CREATE TABLE friends (
  user_id    INTEGER NOT NULL,
  friend_id  INTEGER NOT NULL,
  created_at INTEGER NOT NULL,
  PRIMARY KEY (user_id, friend_id)          -- 双向各存一行
);

CREATE TABLE friend_requests (
  id         INTEGER PRIMARY KEY AUTOINCREMENT,
  from_user  INTEGER NOT NULL,
  to_user    INTEGER NOT NULL,
  remark     TEXT    NOT NULL DEFAULT '',
  state      INTEGER NOT NULL DEFAULT 0,    -- 0 待处理 / 1 已接受 / 2 已拒绝
  created_at INTEGER NOT NULL
);

CREATE TABLE messages (
  id        INTEGER PRIMARY KEY AUTOINCREMENT,  -- serverMsgId，单调递增
  from_user INTEGER NOT NULL,
  to_user   INTEGER NOT NULL,
  content   TEXT    NOT NULL,
  ts        INTEGER NOT NULL,
  delivered INTEGER NOT NULL DEFAULT 0          -- 0 = 待投递（离线队列），1 = 已投递
);
CREATE INDEX idx_msg_pair        ON messages(from_user, to_user, id);
CREATE INDEX idx_msg_undelivered ON messages(to_user, delivered);
```

> 修复原有「§8 用 `ts` 排序离线队列」的隐患：`ts` 同毫秒会乱序，改为按自增 `id` 排序。

**日志**：服务端在控制台打印每条收发的消息类型、用户、结果，便于联调。

---

## 9. 构建系统（CMake）

- 顶层 `CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.24)
project(gamelauncher_chat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network Sql Test)
qt_standard_project_setup(REQUIRES 6.5)

add_subdirectory(shared)   # protocol
add_subdirectory(client)   # client
add_subdirectory(server)   # mock_server
```

| 目标 | 类型 | 链接 | 说明 |
|---|---|---|---|
| `protocol` | STATIC 库 | `Qt6::Core` | 帧编解码 + 消息类型；客户端与服务端共用 |
| `client` | 可执行（`WIN32_EXECUTABLE ON`） | `Qt6::Widgets` `Qt6::Network` `protocol` | 桌面客户端 |
| `mock_server` | 可执行（控制台） | `Qt6::Network` `Qt6::Sql` `protocol` | 参考服务端（账号/好友/消息 SQLite 持久化） |

- 生成器：**Visual Studio 17 2022（x64）**（本机无 ninja，用 VS 自带的 MSBuild）。
- Qt 定位：`-DCMAKE_PREFIX_PATH=D:/true_etc/QT/6.12.0/msvc2022_64`。
- 编译命令见 [docs 上级的 AGENTS.md](../AGENTS.md)。

---

## 10. 错误处理与边界

| 场景 | 处理 |
|---|---|
| 服务端未启动 / 连接被拒 | 状态置为「重连中」，按退避重试；UI 提示，不弹崩溃 |
| 登录失败（账号/密码错） | 回 `LoginResp.code != 0`，UI 展示错误文案 |
| 注册：用户名重复 | 回 `RegisterResp.code != 0`（唯一性冲突），UI 提示「用户名已被占用」 |
| 注册：格式非法（长度 / 字符集 / 密码过短） | 服务端校验后回非 0 错误码 + 具体原因，UI 原样展示 |
| SQLite 打不开 / `QSQLITE` 驱动缺失 | 服务端**启动即失败**并打印明确错误，不静默降级为内存存储 |
| 收到未知 `type` | 记录日志并回 `ErrorResp`；忽略该帧 |
| 载荷 JSON 解析失败 | 丢弃该帧 + `ErrorResp`；不断连 |
| 帧长超过 1 MiB | 直接断连，防止内存膨胀 |
| 心跳超时 | 判定断线 → 重连 |
| 游戏 exe 不存在 | UI 提示「文件不存在」，不启动，不崩溃 |
| 游戏进程异常退出 | `QProcess::finished` 更新状态为「已退出」，显示退出码 |
| 客户端退出 | 主动登出 + 结束自己拉起的子进程 |
| 重复消息 | 客户端按 `serverMsgId` / `clientMsgId` 去重 |

---

## 11. 扩展点（明确预留，不在本期实现）

1. **替换 mock → 用户手写服务端**：只需按 §3、§4 实现协议；客户端零改动。
2. **载荷格式切换**（JSON → 二进制/protobuf）：实现 §5 的 `PayloadCodec`。
3. **服务端下发游戏列表**：启用 `0x04xx` 消息类型。
4. **群聊**：新增 `0x05xx` 段位 + `ConferenceModel`。
5. **加密**：在帧与载荷之间插入对称加密层（帧头增加加密标志位）。
6. **WebSocket 传输**：本机 Qt 未安装 `QtWebSockets` 模块（如需，用 `D:\true_etc\QT\MaintenanceTool.exe` 安装）；届时抽出 `ITransport` 接口，`TcpTransport` / `WsTransport` 两实现。
7. **账号扩展**：找回密码 / 邮箱验证 / 第三方登录；口令哈希升级为 PBKDF2 等慢哈希。
8. **换肤 / 浅色主题**：`Theme` 色板 + `app.qss` 占位符替换机制已就绪，新增主题只需再加一份色板。

---

## 12. 待办 / 已知风险

- **传输安全（公网使用前必须做）**：注册/登录仍为明文传输 → 需 TLS；无鉴权限流、无登录失败锁定。
- **口令哈希强度**：MVP 为「随机盐 + SHA-256」，抗 GPU 暴力破解弱；公网前应换 PBKDF2 / scrypt / bcrypt（Qt 提供 `QMessageAuthenticationCode`，可自行实现 PBKDF2）。
- 账号数据：已落盘 SQLite，但**无备份与迁移工具**，schema 变更靠 `PRAGMA user_version` 手工迁移。
- 时间同步：以服务端 `ts` 为准，客户端不信任本地时钟。
- 消息顺序：MVP 假设单连接内顺序投递；跨连接需 `serverMsgId` 全局单调（SQLite 自增 `id` 已满足）。
- 资源上限：`messages` 表无上限会持续增长；需加定期清理或归档（本期未做）。
- 本机 Qt 为 **6.12.0 RC 期版本**，若遇 Qt 侧异常，先确认是否为 Qt 自身问题。

---

## 13. 验证方法（联调）

1. **构建**：按 AGENTS.md 的命令行流程产出 `client.exe` 与 `mock_server.exe`。
2. **启动服务端**：运行 `mock_server.exe`，确认控制台输出监听端口。
3. **双开客户端**：直接启动两个 `client.exe` 实例。
4. **功能验证**（对应 PRD §6）：
   - 两个实例各自**注册**新账号（如 `alice` / `bob`）并登录成功 → 好友列表拉取；
   - 重复注册同一 `username` → 收到「用户名已被占用」错误；
   - 重启 `mock_server` → 用同一账号**再次登录仍成功**（验证 SQLite 落盘）；
   - `alice` 加 `bob` → `bob` 收到请求并接受 → 双方在线状态互见；
   - 互发私聊 → 实时上屏 + 送达状态；
   - 关闭 bob 客户端 → alice 发消息 → bob 重登 → 收到离线消息；
   - 客户端启动一个本地游戏 exe → 状态「运行中」→ 结束进程 → 状态更新；
   - 点「添加游戏」导入 `notepad.exe` → 列表出现该条目 → 启动 → 关闭记事本 → 状态「已退出」；
   - 导入一个不存在的路径 / 重复导入同一 exe → 分别提示「路径失效」「已在库中」，不崩溃；
   - 重启客户端 → 导入的游戏仍在（验证 `%APPDATA%\GameLauncherChat\games-<userId>.json`）；
   - 用另一个账号登录 → 游戏库独立（只有模板示例，看不到另一账号导入的条目）；
   - 游戏运行中直接关闭客户端 → 任务管理器中该子进程已被清理；
   - kill 掉 `mock_server` → 客户端显示「重连中」→ 重启服务端 → 自动重连。
5. **协议层自测**（可选）：`protocol` 目标可加 `Qt6::Test` 单测，覆盖组帧/拆帧/半包/粘包/超长帧。
