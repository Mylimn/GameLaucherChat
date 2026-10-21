# GameLauncherChat

Windows 桌面客户端（Qt 6 / C++20）：**本地游戏启动器** + **账号自助注册/登录 + 好友 + 私聊**。
附带一个可替换的参考服务端（`mock_server`），与客户端共用同一份协议定义。

- 需求：[docs/PRD.md](docs/PRD.md)
- 设计：[docs/TECH_DESIGN.md](docs/TECH_DESIGN.md)
- 开发日志：[docs/DEVLOG.md](docs/DEVLOG.md)
- 开发者环境与构建：[AGENTS.md](AGENTS.md)

---

## 1. 直接用（推荐）

发布物分两份，**不要一起发给最终用户**：

| 包 | 给谁 | 内容 |
|---|---|---|
| `dist/GameLauncherChat-client-0.1.0-win64.zip` | 最终用户 | 客户端 + Qt 依赖 + VC 运行库，解压即用 |
| `dist/GameLauncherChat-server-0.1.0-win64.zip` | 你自己（常驻服务端的那台机器） | 参考服务端 + Qt 依赖 + `sqldrivers` |

**服务端（一台机器长期运行）**

1. 解压到任意目录，双击 `start_server.bat`（控制台保持运行，默认端口 9527）
2. 首次启动会在 exe 同级自动创建 `data\mock.db`
3. 局域网内其他机器要连接，需要在防火墙放行 TCP 9527 入站

**客户端（每台用户机器）**

1. 解压后双击 `start_client.bat`，可多开：两个窗口分别注册/登录不同账号
2. 登录页底部的「服务器」填服务端地址（默认 `127.0.0.1:9527`，局域网填如 `192.168.1.10:9527`），
   点登录/注册时生效并记住，下次自动带出
3. 登录页切到「注册」自助创建账号 → 互加好友 → 私聊 / 导入游戏

> 目标机器无需安装 Qt 或 Visual Studio。两个 bat 都会先把工作目录切到自己所在目录。

## 2. 数据位置

| 内容 | 路径 | 重置方法 |
|---|---|---|
| 账号 / 好友 / 消息（发布包） | 服务端 exe 同级 `data\mock.db` | 停掉服务端后删除该文件 |
| 账号 / 好友 / 消息（源码开发） | 仓库 `server/data/mock.db` | 同上 |
| 上次填写的服务器地址 | `%APPDATA%\GameLauncherChat\client.ini` | 改登录页「服务器」即可覆盖 |
| 游戏库（按账号隔离） | `%APPDATA%\GameLauncherChat\games-<userId>.json` | 删除对应文件 |

首次登录新账号时，会从内置模板复制一份示例游戏库；内置模板为仓库内 `client/resources/games.json`。

## 3. 从源码构建

见 [AGENTS.md §4](AGENTS.md#4-怎么构建)。要点：

```powershell
& "D:\true_etc\CMAKE\bin\cmake.exe" -S . -B build `
    -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_PREFIX_PATH=D:/true_etc/QT/6.12.0/msvc2022_64
& "D:\true_etc\CMAKE\bin\cmake.exe" --build build --config Debug
```

- 产物：`build\Debug\client.exe`、`build\Debug\mock_server.exe`
- **构建前先关闭正在运行的 `client.exe` / `mock_server.exe`**，否则链接会失败或留下损坏产物
  （Debug 已强制关闭增量链接，见 `CMakeLists.txt`）
- 直接用 Debug 产物运行需要把 Qt 的 `bin` 加入 `PATH`（见 AGENTS.md §5）

## 4. 功能范围

已实现：

- 账号：自助注册、登录、登出、同账号重复登录拒绝（保留已登录方）
- 好友：按用户名搜索添加、请求列表、同意/拒绝、删除好友、在线状态
- 私聊：实时收发、离线消息补推、历史分页、发送状态（发送中/已送达/对方已读）
- 会话列表：最近会话 + 未读角标（左栏分两段）
- 网络：心跳保活、断线自动退避重连、断线期间消息排队重发
- 启动器：游戏库（按账号隔离）、手动导入 exe、启动/结束进程、运行时长、退出清理

不包含（后续）：

- 服务端下发游戏列表（FR-4.7，P2）
- 待发消息队列仅存内存，进程退出即丢
- 传输为明文 TCP；密码为加盐 SHA-256。**公网部署前必须补 TLS 与慢哈希（如 Argon2/scrypt）**
- 无自动化测试，验收以手工清单为准（见 [docs/TECH_DESIGN.md §13](docs/TECH_DESIGN.md)）

## 5. 已知限制

- 客户端崩溃/断网后，旧会话最长要 30s 空闲超时被服务端清理，该账号才能重新登录
- 非原生文件选择框（深色适配）不提供系统「快速访问/最近位置」
