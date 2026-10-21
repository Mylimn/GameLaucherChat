# AGENTS.md — 项目环境、构建运行与 AI 协作准则

> 本文件面向**参与本项目的 AI 助手与人类开发者**，用于快速上手代码环境、复现构建、理解约定。
> 本文件**必须留在仓库根目录**（agent 约定入口，会被自动加载），不要移动到 `docs/`。
> 配套文档：[docs/PRD.md](./docs/PRD.md)（要做什么）、[docs/TECH_DESIGN.md](./docs/TECH_DESIGN.md)（怎么做）、[docs/DEVLOG.md](./docs/DEVLOG.md)（开发日志）。

---

## 1. 项目是什么

一个 **Windows 桌面客户端**（Qt 6 / C++20），提供两大能力：

1. **本地游戏启动器**：从本地 `games.json` 读取游戏列表，用独立进程拉起游戏并跟踪状态。
2. **账号与实时聊天**：账号自助注册/登录 + 私聊 + 好友列表（添加/处理请求、在线状态、离线消息、历史记录）。

外加一个 **轻量 mock 服务端**（C++ 控制台程序），与客户端**共用同一份协议定义**（`shared/protocol`），用于联调；账号/好友/消息用 **SQLite 落盘**，密码存**加盐哈希**。用户的真实服务端后续按同一协议实现即可无缝替换。

三大 CMake 目标：`protocol`（共享协议静态库）、`client`（GUI 客户端）、`mock_server`（mock 服务端）。

---

## 2. 环境与工具链（本机实测路径）

> 全部位于非默认路径，务必按此配置，不要假设 PATH 里有。

| 组件 | 版本 | 绝对路径 |
|---|---|---|
| Qt | **6.12.0**（msvc2022_64 套件） | `D:\true_etc\QT\6.12.0\msvc2022_64` |
| Qt Creator | 20.0.2 | `D:\true_etc\QT\Tools\QtCreator\bin\qtcreator.exe` |
| CMake | 4.5.0-rc1 | `D:\true_etc\CMAKE\bin\cmake.exe` |
| MSVC | Visual Studio 2022 | `E:\vs`（`E:\vs\VC\Auxiliary\Build\vcvars64.bat`） |
| Ninja | **未安装** | —— 请使用 VS 生成器（MSBuild），不要用 `-G Ninja` |

**Qt 模块可用性**

| 模块 | 状态 | 用途 |
|---|---|---|
| `Qt6::Core` / `Gui` / `Widgets` | 已装 | 客户端基础与 UI |
| `Qt6::Network` | 已装 | `QTcpSocket` / `QTcpServer`：本项目传输层 |
| `Qt6::Sql` | 已装（SQLite 驱动 `plugins/sqldrivers/qsqlite.dll` 在） | mock 服务端账号/好友/消息持久化（SQLite） |
| `Qt6::Test` | 已装 | 协议层单元测试 |
| **`Qt6::WebSockets`** | **未装** | 本项目**不使用**（走原生 TCP）；若确需，用 `D:\true_etc\QT\MaintenanceTool.exe` 安装 |

**关键约束**

- Qt 是 **MSVC 2022 64 位** 构建，因此**必须用 MSVC 编译**（MinGW 与 MSVC 构建的 Qt 库 ABI 不兼容）。
- 目标平台 `x64`；C++ 标准 **C++20**。
- 源文件含中文注释/文案，CMake 中需开启 MSVC 的 `/utf-8`。

---

## 3. 目录结构速览

```
final/
├─ CMakeLists.txt      # 顶层
├─ AGENTS.md           # 本文件（须在仓库根目录）
├─ docs/               # PRD.md / TECH_DESIGN.md / DEVLOG.md
├─ shared/protocol/    # 协议库：MessageType.h / Frame.h(.cpp) / ProtocolVersion.h
├─ client/src/         # net/ core/ launcher/ ui/（含 Theme.h）
├─ client/resources/   # style/app.qss（唯一皮肤来源）/ games.json（内置模板）
├─ server/src/         # MockServer / ClientSession / store（SQLite 持久化）
├─ server/data/        # 运行期生成的 SQLite 数据库 mock.db（不入库）
└─ build/              # 生成目录（不入库）
```

详见 [docs/TECH_DESIGN.md §2](./docs/TECH_DESIGN.md)。

---

## 4. 怎么构建

### 方式 A：Qt Creator（最省事）

1. 打开 `D:\true_etc\QT\Tools\QtCreator\bin\qtcreator.exe`
2. `File → Open File or Project` → 选择项目根目录的 `CMakeLists.txt`
3. Kit 选择 **Desktop Qt 6.12.0 MSVC2022 64bit**
4. 点击构建（Ctrl+B）/ 运行（Ctrl+R）

### 方式 B：命令行（PowerShell，推荐用于 AI 自动验证）

```powershell
# 1) 配置（首次执行；VS 生成器，x64）
& "D:\true_etc\CMAKE\bin\cmake.exe" -S . -B build `
    -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_PREFIX_PATH=D:/true_etc/QT/6.12.0/msvc2022_64

# 2) 编译
& "D:\true_etc\CMAKE\bin\cmake.exe" --build build --config Debug
```

- 产物：`build\Debug\client.exe`、`build\Debug\mock_server.exe`（`qt_standard_project_setup` 会把所有可执行文件归并到顶层 `build/Debug`；协议库为 `build\shared\Debug\protocol.lib`）
- 若 CMake 找不到 Visual Studio：先执行
  `cmd /c "E:\vs\VC\Auxiliary\Build\vcvars64.bat && set"` 导入环境，或显式加
  `-DCMAKE_GENERATOR_INSTANCE=E:/vs`。
- 重新配置（改了 `CMakeLists.txt` 后）：重复第 1 步即可。

---

## 5. 怎么运行

Qt 的 DLL 不在系统 PATH 中，运行前需指定：

```powershell
$env:PATH = "D:\true_etc\QT\6.12.0\msvc2022_64\bin;$env:PATH"

# 先起 mock 服务端（控制台会打印监听端口与日志）
.\build\Debug\mock_server.exe

# 再起客户端（可多开：分别用 alice / bob 登录）
.\build\Debug\client.exe
```

- **账号需自助注册**：在登录页切到「注册」创建（服务端不再预置固定账号）
- 数据文件：`server/data/mock.db`（SQLite）；**要重置数据，删掉该文件即可**
- 默认端口：`9527`
- 若不想每次改 PATH，可用 Qt 自带的部署工具把依赖拷到产物目录：
  `D:\true_etc\QT\6.12.0\msvc2022_64\bin\windeployqt.exe build\Debug\client.exe`

**联调验证步骤**见 [docs/TECH_DESIGN.md §13](./docs/TECH_DESIGN.md)。

---

## 6. 编码约定

| 项 | 约定 |
|---|---|
| 语言标准 | C++20；MSVC 编译，`/utf-8` |
| UI 框架 | Qt Widgets（不使用 QML） |
| 命名 | 类 `PascalCase`；成员变量 `m_xxx`；函数 `camelCase`；常量 `k` 前缀或全大写 |
| 头文件 | `#pragma once`；`Q_OBJECT` 用于需要信号槽的类 |
| 注释 | **中文**；解释「为什么」而非「做什么」 |
| 错误处理 | 不抛异常穿透线程边界；网络错误通过返回值 + 信号上报；UI 不崩溃 |
| 线程 | 跨线程只用信号槽 / `QMetaObject::invokeMethod`；**不共享可变状态**；QTimer/Socket 必须在所属线程创建 |
| 资源 | 资源文件统一 UTF-8 |
| 依赖 | **仅允许 Qt**；新增任何第三方库前必须先讨论并更新本文档 |
| 界面风格 | **深色现代**：深色底 + 卡片化 + 主色强调 + 圆角。颜色只在 `client/src/ui/Theme.h` 定义，样式集中在 `client/resources/style/app.qss`；**禁止在 UI 代码里硬编码颜色**（规范见 [docs/TECH_DESIGN.md §7](./docs/TECH_DESIGN.md)） |
| 版本控制 | `build/` 不入库；提交信息用祈使句、说明「为什么」 |

---

## 7. 协议变更流程（强制）

协议是客户端与服务端之间的**契约**，改它必须三处同步：

1. 修改 `shared/protocol/MessageType.h`（枚举）与 `Frame.*`（如有格式变化）；
2. 更新 [docs/TECH_DESIGN.md §3/§4](./docs/TECH_DESIGN.md) 的帧格式与载荷字段表；
3. 同步修改**客户端**（`client/src/net`、`core`）与**服务端**（`server/src`）两侧的处理分支。

> 只改一侧 = 破坏契约。任何 AI 或人工改动都必须遵守此流程，否则视为未完成。

---

## 8. AI 行为准则

**开工前**

- 先读 `PRD.md`、`TECH_DESIGN.md`、本文件；确认当前任务对应哪条需求（FR-x.x）。
- 不确定的需求**先提问**，不要臆测；与文档冲突时以文档为准并提出修订。

**动手时**

- 严格限定在需求范围内，不做未被要求的功能、重构、优化或「顺手」改动。
- 不引入 Qt 以外的新依赖；必须引入时先说明理由并更新本文档。
- 不新增文档/README，除非被明确要求。
- 保持与既有代码风格一致；修改协议必须走 §7 流程。

**收尾时**

- 每次代码改动后**必须编译通过**（用 §4 方式 B 的命令验证），并说明对应 FR 编号。
- 报告「做了什么 / 改到哪 / 如何验证」，不夸大、不隐瞒未完成项。
- **每次开发会话结束时，在 [docs/DEVLOG.md](./docs/DEVLOG.md) 顶部追加一条记录**（日期 / 目标 / 改动 / 决策与坑 / 下一步）。
- 不主动 `git commit` / `push`，除非用户明确要求。
- 删除代码需有依据（确认确实无引用），不做「兼容性兼容层」式冗余保留。

---

## 9. 常见问题

| 现象 | 原因 / 处理 |
|---|---|
| 运行时报「找不到 Qt6Core.dll」 | 未把 `D:\true_etc\QT\6.12.0\msvc2022_64\bin` 加入 PATH，或未执行 `windeployqt` |
| CMake 报找不到 Qt6 | 缺少 `-DCMAKE_PREFIX_PATH=D:/true_etc/QT/6.12.0/msvc2022_64` |
| CMake 报找不到 Visual Studio | 先导入 `vcvars64.bat`，或加 `-DCMAKE_GENERATOR_INSTANCE=E:/vs` |
| 中文注释/文案乱码 | 未开启 `/utf-8`；确认 CMake 中的编译选项与文件编码为 UTF-8 |
| 使用 `-G Ninja` 失败 | 本机未装 ninja；改用 VS 生成器 |
| 想用 WebSocket | 本机 Qt 未装 `QtWebSockets`，需先用 MaintenanceTool 安装（见 §2） |
| 服务端报 `QSQLITE driver not loaded` | 缺 SQLite 驱动插件；确认 PATH 含 `D:\true_etc\QT\6.12.0\msvc2022_64\bin`（插件目录由 Qt 安装位置推导），或对 `mock_server.exe` 执行 `windeployqt` |
| 数据库打不开 / `mock.db` 被锁 | 文件被占用或目录不可写；确认 `server/data/` 可写，或删掉 `mock.db` 让其重建 |
| 链接错误 LNK2038 / 运行崩溃 | 可能混用了 x86/x64 或不匹配的 Qt 套件；确认统一 x64 + msvc2022_64 |
