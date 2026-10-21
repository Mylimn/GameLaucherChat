# 开发日志 DEVLOG

> **记录规则**：每次开发会话结束时**追加一条**，最新条目放在**最上面**（倒序）。
> **单条内容**：日期 / 目标 / 改动 / 遇到的坑与结论 / 下一步。
> **写给谁看**：未来的自己，以及接手本项目的 AI 助手。写「为什么」和踩过的坑，不写流水账。

---

## 2026-10-22 — 修复：地址/端口填错时卡死在「连接中」

**现象**：登录页把服务器端口改错（或服务端没启动），点登录后界面一直停在「正在登录… / ● 连接中」，
没有任何错误提示，只能关窗口。

**根因**：`Session::onNetError` 里「连接尝试失败就调度重连」的分支条件是
`!m_socketConnected && !m_savedPassword.isEmpty()`。手动登录时 `login()` 刚把凭据存进
`m_savedPassword`，于是这条分支把错误**吞掉**（`return` 在 `emit errorOccurred` 之前），
`LoginDialog` 收不到任何消息，状态也就没人复位。这条分支本来是给「已登录会话断线自动重连」用的。

**修复**（`Session::onNetError`）
- 用 `m_userId != 0` 区分「曾经登录成功的会话」与「手动登录/注册」：只有前者才静默退避重连。
- 手动登录/注册连接失败时：清掉未成功使用过的凭据、停掉重连、把状态复位为 `Disconnected`，
  并上报 `无法连接服务器：<原因>（请确认服务端已启动、地址与端口正确）`。
- 顺带修正启动即失败（`main.cpp` 用上次保存地址连接）时状态同样停在「连接中」的问题。

**验证**：UIA 驱动客户端填 `127.0.0.1:9999` 登录 → 界面显示错误文案且状态为「● 未连接」；
改回 `127.0.0.1:9527` 用新注册账号登录 → 标题变为「游戏启动器 + 聊天」，连接建立。

**顺带发现**：`%APPDATA%\GameLauncherChat\client.ini` 会记住上次地址，填错后下次启动仍会连错地址
（错误提示现在能看见，不再静默卡住）；排查时如遇「明明服务端在跑却连不上」，先看登录页的服务器一栏。

---

## 2026-10-22 — 交付前冗余清理

**目标**：关掉进程后全项目扫一遍，删掉废弃/冗余内容，保证仓库与发布物一致。

**核查方式**：按符号全局检索引用次数（不含注释）、比对 `CMakeLists.txt` 源文件清单、
核对文档中提到的文件是否真实存在、检查 build/dist 目录残留。

**结论与改动**
- **源码无孤儿文件**：`client/src`、`server/src`、`shared/protocol` 下的每个 `.cpp/.h`
  都能在对应 `CMakeLists.txt` 中找到，没有被遗忘的模块；服务端仓储层全部方法均有调用方。
- 删除唯一死接口：`GameLibrary::userId()`（构造函数已持有 userId，对外从未使用）。
- 文档纠偏：`AGENTS.md` §3 与 `TECH_DESIGN.md` §2 的目录树里写了并不存在的 `client/resources/icons/`，
  已删除该行（图标实际全部由 `app.qss` + 文字/几何绘制）。
- 清理临时产物：`build/soak.log`（挂机脚本输出）。
- 发布打包物（`dist/`，约 114 MB：两个 zip + windeployqt 展开目录）已按用户决定整体删除——
  本期尚未到交付阶段，日常自测用 Debug 构建即可；需要发布时按原流程重新 `windeployqt` + 压缩，可随时重建。
- **保留**：`server/data/mock.db`（验收用测试数据，删掉即重置，`.gitignore` 已排除）。
- **说明**：`README.md` 保留（面向使用者的启动/数据位置说明，与 `AGENTS.md` 的开发者视角互补）。

**说明**：`TECH_DESIGN` 中出现的 `PayloadCodec.h` 不是失效引用，是 §11 明确写下的扩展点（本期不实现）。

---

## 2026-10-21 — 修复「正在登录…」卡死（登录请求根本没发出）

**现象**：发布版客户端点「登录」后，界面停在「正在登录…」且状态显示「已连接」；服务端日志反复出现
`已接入 → socket error: remote host closed the connection → 断开`。

**根因**：登录前若已有连接在途（启动即连、或刚换过服务器地址），`Session::login()` 会再调
`connectToServer()`；`NetWorker::connectToServer` 先 `abort()` 旧 socket，`abort` 触发 `disconnected`
→ `Session::onDisconnected()` 把 `m_intent` 清成 `None`。随后新连接建立、状态变成「已连接」，
但**待发的 LoginReq 已经被清掉**，于是永远停在「正在登录…」。注册同理。

**修复**
- `onDisconnected`：若 `State == Connecting`（登录意图尚未发出，等 `onConnected` 补发）直接 return，
  不清意图、不重连。
- 新增 `m_connectInFlight`：`login()` / `registerAccount()` 只在「已连接」或「无在途连接」时才发起连接，
  避免每次登录都 abort 一次正在建立的连接（服务端日志里那串 `已接入 → 断开` 就是它）。

**验证**：用 UI Automation 驱动发布版客户端（填账号密码 → 点登录），窗口标题从「登录」变为
「游戏启动器 + 聊天」，登录成功；服务端不再出现连上就断的抖动。

**顺带修的坑**：发布包放在 `dist/` 下时，`resolveDbPath()` 的「向上两级找 server/」会误命中仓库根，
导致发布版把数据写进开发目录；改为只在 exe 父目录名为 `build` 时才认定开发构建。

---

## 2026-10-21 — 发布形态修正：客户端/服务端分开打包 + 服务器地址可配置

**目标**：按真实部署形态交付——服务端常驻一台机器，用户机器只装客户端。发现客户端把服务端地址写死成
`127.0.0.1:9527`，发出去的包只能连本机，必须补配置入口。

**改动**
- **登录页增加「服务器」输入框**（`LoginDialog`）：默认带出 `Session` 当前地址，点登录/注册时校验并生效。
  格式 `主机:端口`，非法（缺端口/端口 0 或越界/主机为空）直接提示，不发请求。
- **地址持久化**：`%APPDATA%\GameLauncherChat\client.ini`（`QSettings` IniFormat，与服务端库文件同目录），
  下次启动自动带出。`Session` 提供 `savedServerAddress / saveServerAddress / parseServerAddress`。
- **`main.cpp` 用保存值建立连接**，不再硬编码。
- **打包拆成两份**：`GameLauncherChat-client-*.zip`（发给用户，23.7 MB）、
  `GameLauncherChat-server-*.zip`（自己部署，8.0 MB），各自带 `windeployqt` 依赖与 `start_*.bat`。
- **服务端数据库路径回退**：`resolveDbPath()` 原来假定「当前目录或 ../../ 下一定有 `server/`」，
  部署包里没有这个目录。改为：开发期（能从 cwd 或其上两级找到 `server/`）仍用仓库
  `server/data/mock.db`，否则落到 **exe 同级 `data/mock.db`**。

**坑与结论**
- **`QFileDialog`/`QMessageBox` 之外，`QSettings` 的默认路径依赖组织名**：本项目没设 `setOrganizationName`，
  因此显式构造 `QSettings(QSettings::IniFormat, UserScope, "GameLauncherChat", "client")`，
  避免落到 `HKCU\Software\Unknown Organization`。
- 中途试过「`Connecting` 状态时不重复 connect」以省掉改地址时的重连抖动，但会让**首次连接失败后**
  （状态停在 Connecting、已无在途连接）点登录永远发不出请求；已回退为「非 Connected 就重连」，
  重复 connect 会 abort 旧尝试，结果等价且没有卡死风险。
- 桌面进程用 `Start-Process` 启动会被工具会话回收，验证 GUI 必须经 `explorer.exe` 拉起；
  抓被遮挡窗口的截图要用 `PrintWindow(PW_RENDERFULLCONTENT)` 且**先 `SetProcessDPIAware()`**，
  否则 GetWindowRect 返回的是 DPI 虚拟化后的坐标，截图会裁掉右半边。

**验证**
- 发布版服务端：监听 9527，数据库生成在包内 `data\mock.db`。
- 发布版客户端：登录页正确显示「服务器 127.0.0.1:9527」，登录窗可正常启动；
  与服务端之间建立 1 条 TCP 连接（Established=1）。
- Debug/Release 均零 error。

**下一步**：PRD §6 手工验收（重点是局域网：另一台机器填服务器 IP 后能注册/登录/私聊）。

---

## 2026-10-21 — 交付准备：关增量链接 + Release 打包 + README

**目标**：达到「第一次交付」门槛——可分发产物、交付说明、构建防护。

**改动**
- **Debug 关闭增量链接**（顶层 `CMakeLists.txt`，对 `client` / `mock_server` 加
  `$<$<CONFIG:Debug>:/INCREMENTAL:NO>`）：上次链接被打断后会留下坏的 `.ilk/.recipe`，
  后续增量链接可能产出**导入表为空的 PE**（进程在 `main` 之前崩溃、`BEX64 / 模块 unknown`，
  就是本轮开头那次「客户端秒退」）。全量重链只需数秒，不值得为此留隐患。
  验证：`.ilk` 时间戳不再更新；`dumpbin /dependents` 可见完整 Qt/KERNEL32 依赖。
- **Release 打包**：`dist/GameLauncherChat/`
  - `client/`：`client.exe` + windeployqt 部署的 Qt DLL、`platforms/qwindows.dll`、`translations/`
  - `server/`：`mock_server.exe` + Qt DLL + `sqldrivers/qsqlite.dll` + 空 `data/`
  - VC 运行库（msvcp140 / vcruntime140(_1) / concrt140）：windeployqt 因未设 `VCINSTALLDIR`
    没带上，改为从 `E:\vs\VC\Redist\MSVC\14.38.33130\x64\Microsoft.VC143.CRT` 手工复制
  - `start_server.bat` / `start_client.bat`：先 `cd /d %~dp0` 再启动，保证数据库固定落在
    `server\data\mock.db`（服务端的 `resolveDbPath()` 是按工作目录推断的）
  - 压缩包 `dist/GameLauncherChat-0.1.0-win64.zip`（约 80 MB）
- **Qt 中文翻译**：非原生文件框的「打开/取消/文件名」来自 Qt，之前的界面会露出英文。
  `main.cpp` 装载 `qt_zh_CN.qm`（Qt 6.x 是合集包名），失败再退 `qtbase_zh_CN.qm`（旧版本命名）。
- **README.md**：交付说明（直接使用、数据位置与重置、源码构建要点、功能范围、已知限制）。
- `.gitignore` 增加 `dist/`（发布包体积大，按需重新生成）。

**坑与结论**
- `windeployqt` 不会帮你补 VC 运行时，除非 `VCINSTALLDIR` 指向 VS；否则目标机器缺
  `MSVCP140.dll` 会直接报「找不到 DLL」。
- 打包用的 bat 必须自己 `cd`：服务端按**当前工作目录**推断 `server/data`，
  从别处双击会写到错误位置（甚至因目录不存在而启动失败）。
- 写 PowerShell 辅助脚本时，**文件含中文注释且无 BOM** 会被 PS 5.1 按 ANSI 解析，
  可能吞掉后面的语句（本次 `$start` 变 null 就是这个原因）；此类脚本一律保持 ASCII。

**验证**
- 发布版 `client.exe`：启动后停在「登录」窗口，无缺 DLL。
- 发布版 `mock_server.exe`（`--port 9528`，工作目录为 dist 根）：正常监听并生成
  `server/data/mock.db`。

**待人工验收**：PRD §6 的交互项（注册/加好友/私聊/离线消息/导入并启动游戏/断线重连）。

**下一步**：30 分钟挂机 + 服务端重启监视结果确认；随后 PRD §6 手工验收；再决定是否 `git init` 留档。

---

## 2026-10-21 — 原生对话框深色化（QFileDialog / QMessageBox）

**目标**：深色界面里，选文件、删好友、移除游戏等弹窗仍是系统浅色（标题栏 + 文件列表整块发白），违反 NFR-10。

**改动**
- `ui/WindowChrome` 从「只管标题栏」扩展为**原生窗口/对话框深色适配**，新增两个封装：
  - `showDarkMessageBox(...)`：等价 `QMessageBox` 静态便捷函数，但先 `applyDarkTitleBar` 再 `exec`。
  - `getOpenFilePath(...)`：构造 `QFileDialog` 并强制 `DontUseNativeDialog`（系统原生文件框在别的进程里画，
    套不上 app.qss），同时把它的标题栏也切深色。
- 调用点替换：`GameLauncherPanel::onAddClicked / removeGame`、`ImportGameDialog::onBrowseCover`、
  `MainWindow::onRemoveFriendRequested`；`ImportGameDialog` 自身构造时也调用 `applyDarkTitleBar`。
- `app.qss` 补非原生文件框的换肤：`QFileDialog` 底色、内部 `QListView/QTreeView` 与其 item 的
  hover/selected、`QHeaderView::section`、`QComboBox`（含下拉列表）、`QToolButton`、`QSplitter::handle`。
  这些控件只在「非原生」模式下才存在于本进程，之前没有对应规则。

**决策**
- **不用系统原生文件框**：原生框是 explorer 进程的窗口，颜色不受客户端控制；改为 Qt 自绘后与整体深色一致，
  代价是少了系统「快速访问/最近位置」等原生特性（本期可接受）。
- 消息框保留 `QMessageBox`（只是包一层），不引入自绘弹窗，避免为 4 个提示框维护一套 UI。

**已知遗留**：非原生文件框里的导航图标来自 Fusion 标准图标，深色下对比度偏低；后续可换成自带图标集。

**验证**：编译通过；`mock_server` + 两个客户端已启动（23204 / 37076）。

**下一步**：按 PRD §6 用 Release 包跑完整验收，再补交付说明与版本留档。

---

## 2026-10-21 — 原生标题栏深色化

**目标**：深色界面顶部顶着一条系统浅色标题栏（用户反馈的「窗口顶部白框」）。

**改动**
- 新增 [WindowChrome.cpp](../client/src/ui/WindowChrome.h)：`applyDarkTitleBar(QWidget*)`，用 DWM 的
  `DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE)` 把原生标题栏切成深色。
  - 属性值 20（Win10 2004+ / Win11）失败时回退 19（Win10 1809~1903）；老系统不支持则保持系统默认。
  - `dwmapi.dll` 动态 `LoadLibrary` + `GetProcAddress`，**不新增链接依赖**（守住 AGENTS §6 只用 Qt 的约束）。
  - 非 Windows 平台空实现。
- `MainWindow::buildUi` 与 `LoginDialog::buildUi` 各加一行调用（与 `setWindowOpacity` 并列）。
- `client/CMakeLists.txt` 增加新源文件。

**验证**：重新编译后启动，登录窗标题栏已变深色，与 `app.qss` 的深色内容区衔接；主窗口同一入口调用。

**下一步**：确认主窗口标题栏效果；再决定 `QMessageBox` / `QFileDialog` 是否需要深色化。

---

## 2026-10-21 — 客户端启动即崩溃：增量链接产物损坏

**现象**：`client.exe` 启动后进程立刻退出，退出码 `0xC0000005`，事件日志 `BEX64 / 出错模块 unknown / 偏移 0x0`；服务端正常。

**排查过程**
- 启动跟踪（`main()` 第一行写文件）**一条都写不出来** → 崩溃发生在 `main()` 之前，不是业务代码。
- `dumpbin /dependents client.exe` 的输出里 **导入表为空**（连 `KERNEL32.dll` 都没有）→ PE 已损坏。
- 之前那次构建走了「增量链接」（MSBuild 未报错但产物不可用）：上一次链接被中途打断（旧 `client.exe`
  还在运行/构建被终止），`client.ilk` 与 `client.exe.recipe` 残留，后续增量链接在这套残留上「拼」出了坏文件。

**修复**：删除 `build/Debug/client.exe` 与 `build/client/client.dir/Debug/client.exe.recipe` 后重链，
LINK 日志出现「正在执行完全链接」，导入表恢复（Qt6Widgetsd / Qt6Cored / KERNEL32 …），启动正常。

**结论（以后遇到同类现象先查这里）**
- 「进程秒退 + 事件日志模块 unknown + 偏移 0」优先怀疑**产物本身损坏**，而不是 `main()` 逻辑；
  用 `dumpbin /dependents` 看导入表是否为空即可确诊。
- **构建前先关掉正在运行的 client.exe**；若 MSBuild 报告成功但程序行为异常，删掉 exe + `.recipe` 强制完全重链。
- 启动跟踪要写在 `main()` 的第一行才有意义（`qInfo` 在 GUI 子系统下重定向到文件时可能拿不到输出）。

**验证**：`mock_server`（14692）+ 两个 `client.exe`（10968 / 35612）同时运行，均显示「登录」窗口，TCP 9527 连通。

---

## 2026-10-21 — 同账号重复登录 + 横向滚动条/页签栏白条

**目标**：同账号第二处登录按错误处理（保留原连接）；消除窗口底部浅色横条。

**服务端**（`ClientSession::handleLogin`）
- 凭据校验通过后、写入 `m_userId` 前查 `SessionRegistry`：该 `userId` 已有**其他**在线会话则回
  `LoginResp.code = 8` +「该账号已在其他设备登录，请先退出后重试」，不注册、不广播上线。
- 错误码撞车：原有 `kErrNotLoggedIn` 占用了 8，本次顺延为 9（客户端不按码分支，只看 message，无兼容影响）。
- 单线程事件循环下「检查 → 注册」天然原子，无需加锁；`existing != this` 用于允许同一连接重复登录被
  `isLoggedIn()` 提前拦截后的边界情况。
- 旧连接若已是死连接（客户端崩溃），要等最长 30s 空闲超时被清理后该账号才能重登；这是「保留已登录方」策略的必然代价。
- 客户端侧无需改动：自动重登被拒走既有 `reloginFailed` 分支，退回登录页并显示服务端 message。

**客户端主题**
- `app.qss` 补 `QScrollBar:horizontal` 全套（handle/add-line/add-page）+ `QAbstractScrollArea::corner`，
  并把 `QTabBar` 背景设为 transparent。横向滚动条与页签栏底衬此前未声明，Qt 退回系统浅色绘制，
  表现为窗口底部/内容区下方一条白条。

**验证**：PowerShell 原始 TCP —— conn A 登录 alice 得 `code=0`；conn B 同账号得 `code=8`；
同一条 conn B 改用 bob 得 `code=0`（证明拒绝只针对重复账号，连接未被断开）。

**下一步**：确认白条是否消失（若仍有需定位具体控件）；再决定 `QMessageBox` / `QFileDialog` 是否自绘。

---

## 2026-10-21 — 已读回执（FR-3.7）

**目标**：自己发出的消息在被对方读到后，从「已送达」变为「已读」；对方离线时不补推。

**协议（AGENTS §7 三处同步已完成）**
- `MessageType.h` + TECH_DESIGN §4.1/§4.2：新增 `ChatReadPush 0x0307`，载荷
  `{ peerUserId, upToServerMsgId }`，其中 `peerUserId` 是**读消息的人**（与请求侧同名同义）。

**服务端** `ClientSession::handleChatRead`：本来已把对方发来的消息置 `delivered=1`，
现补一步——若对方在线（`SessionRegistry::find`），把已读位置回推给发送方；离线直接丢弃，不落库、不补推。

**客户端**
- `MessageStatus` 追加 `Read`；`MessageModel::markPeerRead(upTo)` 只把「已送达」的己方消息升为已读，
  已读不回退，`Sending/Failed` 不受影响。
- `Session` 分发 `ChatReadPush` → `chatReadReceived(peer, upTo)`；`MainWindow` 只转给当前会话的 `ChatPanel`。
- `ChatPanel`：打开会话整页历史落地后，取最新一条对方消息的 `serverMsgId` 上报已读
  （此前仅在收到实时消息时上报，进入会话不会清服务端未读）。
- 气泡状态文案增加「已读」。

**决策**
- 已读不新增表：复用 `messages.delivered`——对 `to_user = 我` 的行表示「已投递」，对 `from_user = 我` 的行
  只由对端已读上报置位，等价于「已读位置」；多端会话级一致性留给以后的会话已读表。
- `upToServerMsgId` 用自增 id 而非 ts，避免同毫秒歧义（与历史分页一致）。

**验证**：A 发消息 → B 打开会话或收到消息 → A 端气泡由「已送达」变「已读」；B 离线时 A 端保持「已送达」。

**补充（同日）**：切换会话后「已读」退回「已送达」。原因是已读只在内存，`openConversation` 重新拉历史，
历史行里没有已读信息，模型重建即回落到 `Delivered`。
- `ChatHistoryResp` 增加 `peerReadUpTo`（TECH_DESIGN §4.2 已同步）；
- 服务端 `MessageRepository::readUpTo(self, peer)` = `MAX(id) WHERE from=self AND to=peer AND delivered=1`
  ——`delivered` 对己方发出的行只由已读上报置位，天然就是已读位置，不需要新表；
- 客户端在历史整页落地后 `markPeerRead(peerReadUpTo)` 重建状态。向上翻页（prepend）不处理，避免重复置位。

**下一步**：主题收尾（原生控件深色化、提示分级），M5 收尾后按 PRD 复查剩余 P2 项。

---

## 2026-10-21 — 会话列表 + 未读角标（FR-3.4）

**目标**：左栏分两段——顶部「最近会话」（有历史消息的会话，右侧未读角标），下方保留「好友 / 好友请求」页签。

**改动**
- 新增 `core/ConversationModel`：`userId / 名称 / 预览 / ts / unread / online`，按 ts 倒序；
  `setFriends()` 用好友表刷新名称与在线状态并剔除已删好友；`upsert()` 记录最新一条消息（可选累加未读）。
- `FriendListWidget`：新增会话段（`#sectionTitle` + `#conversationList`），最多显示 4 条（超出滚动），
  空时整段隐藏；`setActivePeer()` 让两个列表共用一个「当前会话」选中态（只在包含该项的列表显示）。
- `ChatPanel`：新增 `messageSent(peer, content, ts)`，本地发出即刷新会话预览。
- `MainWindow`：`onChatReceived` 同时更新会话条目与未读（当前会话不计未读）；`onFriendSelected`
  同时清零两个模型的未读；`onConversationHistory` 只用于「补齐会话条目」，跳过当前打开会话
  （否则向上翻页会拿旧消息覆盖预览）；登录后对没有最后一条消息的好友各拉 1 条历史，重建已有会话。
- `app.qss`：`#conversationList` 复用好友列表的 hover/选中样式，新增 `#sectionTitle`。
- `client/CMakeLists.txt` 增加 `ConversationModel`；TECH_DESIGN §7 模块表补一行。

**决策**
- 不新增「拉会话列表」协议：好友量级下按好友拉 1 条历史即可重建，避免为 P1 功能改协议。
- 未读是本地状态（PRD 未要求跨设备同步），随 `ChatPush` 累加、打开会话清零；
  跨设备已读同步留待 FR-3.7 补 `ChatReadPush`，届时不改这里的模型接口。
- 会话列表按「最后一条消息时间」倒序而非固定好友顺序；同一 ts 保持插入顺序（stable_sort）。

**下一步**：已读同步（FR-3.7，需补 `ChatReadPush`），随后主题收尾。

---

## 2026-10-21 — 断线期间消息改为本地待发队列（outbox）

**目标**：实测暴露两个问题——断线时发的消息重连后一直「发送中」；重新打开会话后那条消息消失。

**根因**
- `ChatPanel::onSendClicked` 是「先调 `sendChat`、后插列表」，旧实现在未登录时**同步**发 `chatSendAck(false)`，
  回执早于那一行插入，`confirmOutgoing` 按 `clientMsgId` 找不到行 → 永远停在 `Sending`。
- 断线时的消息只存在于本地模型、服务端从未收到，`openConversation` 清空模型再拉历史，
  历史里没有它，于是消失（断线时历史请求本身也被丢弃，连列表都重建不出来）。

**改动**
- `Session` 新增 `PendingChat` 队列：未登录时不回失败，改为入队并照常返回 `clientMsgId`。
- `LoginResp` 成功后 `flushOutbox()` 按顺序补发；`ChatSendResp` 到达才出队（无论成败都不重发）。
- 登出 / 重登失败时清空队列，避免下次登录复用同一 `clientMsgId`。
- `ChatPanel::syncPendingOutgoing()`：打开会话、历史整页替换后把队列里属于本会话的消息补回，
  按 `clientMsgId` 去重；向上翻页（prepend）时不重复补。

**决策**
- **不用「未连接直接标失败」**：虽然能消除「发送中」假象，但重连后没有任何补发通道，用户得重打一遍。
- 队列只在内存：进程关闭即丢，不做落盘（P2）；换来的是一次重连内不丢消息。

**验证**：断线后发 2 条 → 消息停留「发送中」；重开会话仍在；重启服务端后重连，两条依次「已送达」，对端收到。

**下一步**：会话列表 + 未读角标（FR-3.4），与已读同步（FR-3.7）一起做。

---

## 2026-10-21 — M5 第一步：断线自动重连（FR-1.3）

**目标**：服务端重启 / 网络抖动后，客户端不再停在「未连接」，而是自动退避重连并恢复登录态。

**改动**
- `Session`：新增 `QTimer` 退避重连（1s→2s→4s→8s→16s→30s 封顶）；登录成功后在内存保留凭据，断线后自动重发 `LoginReq`。
- 新增信号 `reconnectScheduled(attempt, delayMs)` / `connectionRestored()` / `reloginFailed(message)`；新增 `lastError()` 供登录框显示被踢回原因。
- `Intent` 增加 `Relogin`；`logout()` / `LogoutResp` / 重登失败都会清空凭据并停止重连。
- `MainWindow`：状态标签显示「重连中…（第 N 次，X 秒后）」；恢复后提示「已重新连接」；重登失败退回登录页。
- `LoginDialog`：构造时若有 `lastError()` 直接显示，避免用户莫名被踢回。
- 文档：TECH_DESIGN §4.3 增加「客户端自动重连」、§7 模块表更新。

**坑与结论**
- **连接尝试失败时 Qt 不发 `disconnected()`**（只有连上后的中断才发）。只在 `onDisconnected` 里调度重连会卡死在第一次失败，
  因此 `onNetError` 里若「socket 从未连上且仍有凭据」也补一次调度。这是本次最关键的一处。
- 重连期间不能调 `clearUser()`：它会把 `m_uiReady` 复位，导致重登后的首批推送被缓存却无人取走。
  改为只切状态、保留身份与模型数据；真正的清理放在重登失败分支。
- 用户主动 `login()` / `registerAccount()` 先 `stopReconnect()`，否则残留定时器可能在稍后把新连接 abort 掉。

**下一步**：会话列表 + 未读角标（FR-3.4），随后连同已读同步（FR-3.7）一起做。

---

## 2026-10-21 — 窗口轻微透明 + 清理旧 games.json

**目标**：客户端背景不要完全不透明；顺手清掉按账号隔离之前的旧配置文件。

**改动**
- `Theme.h` 新增 `kWindowOpacity = 0.94`；`MainWindow::buildUi` 与 `LoginDialog::buildUi` 统一调用 `setWindowOpacity`。
- TECH_DESIGN §7 补充说明：不透明度与颜色一样集中在 `Theme.h`。
- 删除 `%APPDATA%\GameLauncherChat\games.json`（旧单文件），保留 `games-<userId>.json`；仓库内 `client/resources/games.json` 作为模板保留。

**决策**
- **用 `setWindowOpacity` 而不是 `WA_TranslucentBackground` + rgba**：后者只让背景透、文字保持全不透明，但要自行处理原生边框阴影和重绘，Windows 上容易出现圆角锯齿；`setWindowOpacity` 一行生效，0.94 下正文可读性几乎无损失。
- 0.94 这个值放在 Theme 里统一改，避免两个窗口各写一个魔法数。

---

## 2026-10-21 — 游戏库按账号隔离

**目标**：同一台机器上不同账号应看到各自独立的游戏库。

**改动**
- `GameLibrary` 构造参数改为 `userId`，存储路径由 `games.json` 变为 `games-<userId>.json`（`%APPDATA%\GameLauncherChat\` 下）。
- `MainWindow` 用 `session->userId()` 构造 `GameLibrary`，账号切换即切换文件；模板仍从 `:/games.json` 复制，新账号首次登录得到一份示例库。
- 文档同步：PRD FR-4.1 / §7、TECH_DESIGN §7.1 存储说明与 §13 验证（新增「换账号登录库独立」一项）。

**决策**
- **不引入服务端下发**：游戏库存本地按账号隔离即可满足需求，服务端 `GameListReq/Resp` 仍按 FR-4.7 留作 P2；避免为此扩展协议。
- 用 `userId` 而非 `username` 做文件名：用户名可改、且含大小写风险；`userId` 是服务端稳定主键，也不会出现非法文件名字符。
- 旧版单文件 `games.json` 不自动迁移——此前只有开发期的临时数据，不值得增加迁移分支。

**验证**：编译零 error；服务端 + 双客户端已启动。待实测：alice 导入条目 → 登出 → bob 登录 → 游戏库仅含模板示例。

---

## 2026-10-21 — M4 游戏启动器（含手动导入）实现

**目标**：按 TECH_DESIGN §7.1 落地游戏库：本地/导入列表、独立进程启动、状态与时长、结束进程、退出清理。

**改动**
- 新增 `client/src/launcher/`：`GameEntry.h`（JSON 互转）、`GameLibrary`（加载/保存/导入/移除/去重/路径校验）、`GameProcessManager`（QProcess 生命周期 + 1s 时长心跳 + 退出清理）。
- 新增 `client/src/ui/ImportGameDialog`（名称/参数/封面 + 名称校验）、`GameLauncherPanel`（卡片列表 + 添加/启动/结束/移除 + 空态与错误提示）。
- `MainWindow` 右侧改为 `QTabWidget`（会话 / 游戏），选中好友时自动切回会话页；析构时 `killAll()`（登出也会走这里）。
- `client/resources/games.json` 内置模板（一条记事本示例），并在 `resources.qrc` 注册为 `:/games.json`；`app.qss` 补游戏列表/状态/封面占位样式；`client/CMakeLists.txt` 补源文件。
- 文档校正：games.json 实际路径为 `%APPDATA%\GameLauncherChat\games.json`（未设组织名），qrc 路径为 `:/games.json`。

**坑与结论**
- **不能在按钮的 `clicked()` 里重建它所在的列表**：`GameLibrary::changed` 原本直连 `rebuildCards`，`QListWidget::clear()` 会同步销毁信号发送者，存在野指针风险。改为 `Qt::QueuedConnection` 延迟重建；进程状态变化则只就地 `updateCard`，不重建控件。
- **进程起不来时不会发 `finished`**：必须在 `errorOccurred(FailedToStart)` 里把状态回退到 `NotRunning` 并记录原因，否则界面会一直停在「启动中」。
- **工作目录必须设为 exe 目录**，否则大量游戏按相对路径找不到资源。
- 运行时长用单个 1s 定时器统一驱动，无运行中进程时自动停表；不用每游戏一个定时器。
- `QSaveFile` 原子写 + 读失败只提示不覆盖：用户手改坏 JSON 不该被客户端悄悄「修」成空库。

**验证**：`cmake --build build --config Debug` 零 error；启动服务端 + 双客户端无崩溃。GUI 交互项（导入 notepad、启动/结束、重启保留、退出清理）待人工实测。

**下一步**：GUI 实测；通过后处理 M5（自动重连 / 会话列表等 P1 项）。

---

## 2026-10-20 — M4 方案定稿（新增手动导入，仅文档）

**目标**：在 M4 启动器方案上补充「添加游戏」手动导入能力，先把需求与设计写进文档，暂不开工。

**改动**（全部为文档）
- `docs/PRD.md` → 新增 FR-4.8（手动导入 exe，可改名称/参数/封面，去重且持久化）、FR-4.9（移除条目，只删配置不删文件）；FR-4.1 明确「内置模板首次复制到用户数据目录，之后该副本为唯一读写源」；M4 验收与 §6 验收标准、§7 风险同步。
- `docs/TECH_DESIGN.md` → 新增 §7.1 游戏启动器设计：存储位置与原子写入、JSON 结构、`GameLibrary` / `GameProcessManager` 职责、导入流程、移除规则、UI 约定；§2 目录与 §7 模块表补 `ImportGameDialog`；§13 验证补导入/重复导入/持久化/退出清理四步。

**决策**
- **唯一读写源放用户目录**（`QStandardPaths::AppDataLocation/games.json`），首次运行从 qrc 模板复制。相比「内置只读 + 用户覆盖层」两处合并，单文件更简单，也允许用户手改；代价是内置示例更新不会自动同步（本期无此需求）。
- **读失败不覆盖**：JSON 损坏只提示并显示空列表，避免客户端把用户的文件覆盖成空数组。写入统一用 `QSaveFile` 原子替换。
- **去重用 `canonicalFilePath()`**，防止大小写/斜杠差异造成同一 exe 导入两次。
- **运行时长用一个 1s 定时器统一刷新**，不为每个进程各开定时器。
- **不使用 `waitForStarted/Finished`**，全部走信号，避免卡 UI 线程。
- 内置示例保留一条「记事本」便于首次自检，用户可自行移除。

**下一步**：按 §7.1 实现 M4——`GameEntry` / `GameLibrary` / `GameProcessManager`、`GameLauncherPanel` + `ImportGameDialog`、右侧改 `QTabWidget`、`resources/games.json` 模板与 qrc 注册、`client/CMakeLists.txt` 补源文件。

---

## 2026-10-20 — 聊天气泡补充来源标注

**目标**：消息气泡上方显示发送者，便于区分双方。

**改动**：`client/src/ui/ChatPanel.*` → 自绘 delegate 增加名字行：对方显示昵称（贴气泡左边缘），自己显示「昵称 · me」（贴气泡右边缘）；行高按 `名字行高 + 4px 间距` 增高；名字为空（未选会话）时不留白。名字来源由 `ChatPanel::senderLabel(mine)` 提供，delegate 只持有 `const ChatPanel*`，不复制状态。

**决策**：不新增模型角色 —— 两人会话的发送者只有「我 / 对端」两种，取名字比给每条消息塞一份冗余字段更简单，也避免切换会话时模型里残留旧昵称。

---

## 2026-10-20 — M3 好友与私聊（含登录时序修复 + 请求主动拉取）

**目标**：PRD M3 验收——两实例互加好友、互发消息实时到达；B 下线后 A 发消息、B 重登收到离线补推。

**改动**（文件 → 做了什么）
- `server/src/store/Database.cpp` → schema 升到 2，新增 `friends`（双向各一行）/ `friend_requests`（state 0待/1受/2拒）/ `messages`（id 即 serverMsgId），建索引 `idx_msg_pair`、`idx_msg_undelivered`；老库（v1）走 `CREATE TABLE IF NOT EXISTS` 原地补建。
- `server/src/store/FriendRepository.*` → 好友列表（join users）、`areFriends`、`addRequest`（同对待处理去重）、`pendingRequests`、`acceptRequest`（置 state + 双向插入同一事务）、`rejectRequest`、`removeFriendPair`。
- `server/src/store/MessageRepository.*` → `insert`、`markDelivered`、`undelivered`（按 id 升序）、`history`（beforeTs 游标）、`pairLastId`。
- `server/src/SessionRegistry.*` → 内存 `userId → ClientSession*`，登录注册、断线移除。
- `server/src/ClientSession.*` → 0x02xx / 0x03xx 全部 11 个分支 + `FriendRequestListReq`；登录后广播上线 → 按 id 升序补推离线消息 → 补推待处理请求；登出/断线广播离线；接受/删除好友后给双方主动补发 `FriendListResp`。
- `server/src/store/UserRepository.*` → 补 `findById`。
- `client/src/core/Types.h` → `FriendInfo` / `FriendRequestInfo` / `ChatMessageInfo`。
- `client/src/core/FriendModel.*` / `FriendRequestModel.*` / `MessageModel.*` → 好友列表、待处理请求、会话消息（含乐观上屏与 ack 对账）。
- `client/src/core/Session.*` → 好友/私聊请求接口与入站分发；登录成功后自动 `FriendListReq`；**UI 就绪前的首批推送缓存**；新增 `requestFriendRequests()`。
- `client/src/ui/FriendListWidget.*` → 好友/请求双页签、添加好友、接受/拒绝、未读与在线点。
- `client/src/ui/ChatPanel.*` → 消息气泡（自绘 delegate）、输入发送、加载历史、删除好友。
- `client/src/ui/MainWindow.*` → 左侧好友面板 + 右侧会话面板；消费缓存推送；UI 就绪后主动拉取请求全量。
- `client/resources/style/app.qss` + `Theme.h` → 列表项 44px / hover / 选中竖条、未读徽标、在线点、气泡、空态。
- `shared/protocol/MessageType.h` → 新增 `FriendRequestListReq 0x020B` / `FriendRequestListResp 0x020C`（其余沿用既有枚举）；`docs/TECH_DESIGN.md` §4.1/§4.2/§4.3 同步。
- `client/src/main.cpp` → 登录页 ↔ 主窗口循环同时监听 `closed` 与 `loggedOut`。

**决策 / 坑**
- **坑 1：登录瞬间的推送全部丢失**。服务端在 `LoginResp` 之后立刻补推好友列表/请求/离线消息，而客户端 `MainWindow` 要等 `login.exec()` 返回才创建，`emit` 时没有任何接收者 → bob 看不到 alice 早先发的申请，且服务端仍有 pending 记录，双方互相"已发送"而无法推进，形成死锁。修法：`Session` 用 `m_uiReady` 标记 + 三个缓存容器暂存，`notifyUiReady()` 后由主窗口一次性取走；登出清空缓存避免跨账号泄漏。`takePendingFriends` 返回 `bool` 是为了区分"服务端下发空列表"与"从未收到列表"。
- **坑 2：登出后界面不切回登录页**。`main.cpp` 的 `QEventLoop` 只监听 `closed`，`LogoutResp` 后窗口不销毁、循环不退出。修法：补 `loggedOut → loop.quit`。
- **补齐推送的兜底**：新增 `FriendRequestListReq/Resp`，主窗口就绪后主动拉一次**全量**待处理请求并整体覆盖本地列表。推送是"尽力而为"，主动拉取才是权威快照；这也是本次新增协议的唯一动机。
- **离线补推按 `messages.id` 升序**，不用 `ts`（同毫秒会乱序，且 id 即 serverMsgId 的单调序）。
- **接受/删除好友后主动补发 `FriendListResp`**：协议没有"列表已变更"推送，与其新增枚举不如复用现有下行包给双方各发一份。
- 协议改动走 AGENTS §7 三处同步（枚举 + 载荷表 + 两端实现），帧格式未变。

**下一步**：M4 启动器（`games.json` 读取 + 独立进程拉起 + 状态跟踪，FR-4）。

---

## 2026-10-20 — M2 账号与连接（注册/登录/心跳/登出 + SQLite 落盘）

**目标**：跑通 PRD M2 验收——自助注册、登录、心跳、登出可用，且**重启服务端后账号仍能登录**（SQLite 落盘）。

**改动**（文件 → 做了什么）
- `server/src/store/Database.*` → `QSQLITE` 连接 + 启动建表 + `PRAGMA user_version` 版本记录（M2 = 1，仅 `users` 表）；驱动缺失/打不开直接启动失败并打印明确原因。
- `server/src/PasswordHash.*` → 16 字节系统随机盐 + `SHA-256(salt || password)`，只存 hex 盐与哈希。
- `server/src/store/UserRepository.*` → `findByUsername` / `create`（用户名唯一约束）。
- `server/src/ClientSession.*` → 单连接拆帧、分发（Register/Login/Logout/Heartbeat）、回包；30s 无入站帧主动断开；坏载荷回 `ErrorResp` 不断连、超长帧断连；未知类型回 `ErrorResp`。
- `server/src/MockServer.*` → `QTcpServer` 监听 0.0.0.0:9527（`--port` 可覆盖）、每连接一个 `ClientSession`、会话关闭回收。
- `server/src/main.cpp` → 启动链：解析端口 → 定位数据库 → 打开 DB → 监听；失败返回 1。
- `client/src/net/NetWorker.*` → 网络线程内创建 `QTcpSocket` + 心跳 `QTimer`（5s 一次，连续 3 次无应答判死）；增量拆帧、组帧发送。
- `client/src/net/NetController.*` → 主线程门面：出站用 `QueuedConnection` 投递到网络线程，入站信号转发回主线程；析构时阻塞式回收 worker。
- `client/src/core/Session.*` → 登录态状态机（Disconnected/Connecting/Connected/LoggedIn）、登录/注册意图排队（未连接则先连再发）、`LoginResp`/`RegisterResp`/`LogoutResp`/`ErrorResp` 分发。
- `client/src/ui/LoginDialog.*` → 登录/注册双页签；注册成功**不自动登录**，切回登录页并预填用户名；显示连接状态与错误文案。
- `client/src/ui/MainWindow.*` → 左侧栏显示昵称/账号/连接状态 + 登出按钮；登出后关窗回到登录页，直接关窗则退出（退出前主动登出）。
- `client/src/main.cpp` → 登录页 ↔ 主窗口循环；启动即连接 `127.0.0.1:9527`。
- `client/resources/style/app.qss` + `Theme.h` → 补按钮/输入框/页签/侧栏/状态点样式；新增 `kTextOnAccent` token（主色按钮白字）。
- `shared/protocol/MessageType.h` → 增加 `Q_DECLARE_METATYPE(MsgType)`（跨线程信号槽需要），**帧格式与枚举取值未变**。
- 两端 `CMakeLists.txt` → 补新增源文件。

**决策 / 坑**
- **登录与注册共用一个意图队列**：`Session::login/registerAccount` 在未连接时先记录意图再发起连接，`onConnected` 时统一 `flushIntent()`。好处是 UI 不必关心连接时序，也不必禁用按钮等连接完成。
- **错误码约定**（文档只要求「非 0 即失败」）：1 参数非法 / 2 用户名占用 / 3 凭据错误 / 4 协议版本不兼容 / 5 内部错误 / 6 未知类型 / 7 坏载荷。客户端当前只展示 `message`，码值留给后续做分支处理。
- **注册不自动登录**：严格按 §4.2，注册成功后客户端切回登录页，由用户再发 `LoginReq`。
- **M2 不建 `SessionRegistry`**：M2 没有好友在线广播需求，在线表留到 M3 与 `PresencePush` 一起做；因此同一账号目前允许重复登录（无单会话互斥）。
- **登出不关连接**：`LogoutReq` 只清空登录态并回 `LogoutResp`，TCP 保持，用户可立即重新登录；真正断连由关窗/退出触发。
- **不做自动重连**（FR-1.3，P1 → M5）：断线只提示，用户重新登录。
- **超时判定**：客户端连续 3 次心跳（15s）无应答 → `abort()` 并报断线；服务端 30s 无任何入站帧 → 主动断开。
- **`FrameDecoder` 未改动**：新连接只需 `m_decoder = FrameDecoder()` 重置（值语义），避免为重置再动共享协议库。
- **数据库路径双形态兼容**：从仓库根运行 → `./server/data/mock.db`；从 `build/Debug` 运行（Qt Creator 默认工作目录）→ 回退两级找到仓库根，保证两种启动方式用同一份数据。
- 编译期无 warning、无 error；PowerShell 执行策略报错属环境噪音，与构建无关。

**验证**（`build/Debug/{mock_server.exe, client.exe}`）
- 协议级脚本联调（原始 TCP 发送二进制帧）全部通过：注册成功 → 重复注册 `code=2` → 协议版本 99 `code=4` → 错误口令 `code=3` → 正确登录 `code=0` → 心跳 3 次 `ts` 原样回传 → 登出 `code=0`。
- **落盘验证**：`server/data/mock.db` 生成；`Stop-Process` 杀掉服务端并重启后，用 `alice` 重新登录仍 `code=0`（PRD M2 验收达成）。
- 客户端启动后与 9527 建立 `Established` 连接，进程稳定存活，登录页正常展示。

**下一步（M3）**
- 好友：服务端 `FriendRepository` + `SessionRegistry` + `FriendListReq/Add/Handle/Remove` 与 `PresencePush`；客户端 `FriendModel` + 好友列表面板。

---

## 2026-10-20 — M1 工程骨架（三目标编译通过 + 空白主窗口）

**目标**：搭起 CMake 骨架，`protocol` / `client` / `mock_server` 三目标编译通过，客户端能弹出空白主窗口（PRD M1 验收）。

**改动**（文件 → 做了什么）
- `CMakeLists.txt`（顶层）→ 项目 `gamelauncher_chat`；C++20；开启 `AUTOMOC/AUTOUIC/AUTORCC`；`find_package(Qt6 ... Core Gui Widgets Network Sql Test)` + `qt_standard_project_setup(REQUIRES 6.5)`；MSVC 加 `/utf-8`；`add_subdirectory(shared/client/server)`。
- `shared/CMakeLists.txt` + `shared/protocol/` → `protocol` 静态库：`MessageType.h`（§4.1 完整枚举）、`ProtocolVersion.h`（`Protocol::kVersion=1`）、`Frame.h/.cpp`（`encodeFrame` 组帧 + `FrameDecoder` 增量解码，处理半包/粘包/超长帧/坏载荷）。
- `client/CMakeLists.txt` + `src/main.cpp` + `src/ui/{MainWindow,Theme}.h(/.cpp)` + `resources/{resources.qrc,style/app.qss}` → `client` GUI 程序（`WIN32`）：加载 Fusion 风格 + `app.qss` 深色皮肤，弹出空白主窗口。
- `server/CMakeLists.txt` + `src/main.cpp` → `mock_server` 控制台占位程序（打印协议版本后退出）。
- `.gitignore` → 忽略 `build/`、`server/data/mock.db`、`*.user`。

**决策 / 坑**
- **配色机制落地**：颜色只写进 `client/src/ui/Theme.h`，`app.qss` 用 `@token` 占位，`Theme::styleSheet()` 加载时按「长 token 优先」顺序替换（避免 `@accent` 误伤 `@accentHover/@accentPressed`）——落实 NFR-9/10 与 TECH_DESIGN §7。
- **`FrameDecoder` 的 fatal 语义**：文档 §3 只给了 `bool next(...)`，但「超长帧要断连」与「坏载荷要丢弃但不断连」是两类错误，用同一个 `bool` 无法区分。故补 `fatal()`：`next()` 返回 false 且 `err` 非空时，`fatal()==true` 才要求断连，否则仅丢弃坏帧继续。此为对 §3 参考 API 的小幅细化，帧格式本身未变。
- **产物路径统一在 `build/Debug/`**：`qt_standard_project_setup` 会把可执行文件输出归并到顶层 `build/Debug`，而非 `build/client/Debug`（AGENTS.md §4 原写的 `build\client\Debug\client.exe` 实际不成立）——已按实测为准。
- 编译期唯一「噪音」是 PowerShell 执行策略报错，与构建无关；configure/build 均成功。

**验证**
- 配置：`cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=D:/true_etc/QT/6.12.0/msvc2022_64` → `Configuring/Generating done`。
- 编译：`cmake --build build --config Debug` → 产出 `build/Debug/{client.exe, mock_server.exe}` 与 `build/shared/Debug/protocol.lib`，**零 error**。
- 运行：`mock_server.exe` 退出码 0；`client.exe` 启动后持续存活（无缺 DLL 崩溃），随后手动结束。

**下一步（M2）**
- 服务端接 `QTcpServer` + `ClientSession`（拆帧/分发）、`Database`（SQLite 建表）、`UserRepository`、`PasswordHash`；客户端接 `NetWorker`（网络线程）+ `NetController` + 登录/注册页，跑通注册/登录/心跳/登出。

---

## 2026-10-20 — 项目立项与设计定稿

**目标**：确定「做什么、怎么做」，先产出文档再动手写代码（文档驱动开发）。

**产出**
- `docs/PRD.md` — 产品需求：本地游戏启动器 + 账号与实时聊天；含 FR-1.x～FR-4.x、NFR-1～8、M1–M5 里程碑、验收标准。
- `docs/TECH_DESIGN.md` — 技术设计：三层架构、二进制帧格式、完整消息类型与载荷表、线程模型、mock 服务端与 SQLite 表结构、CMake 目标、视觉规范、扩展点。
- `AGENTS.md`（仓库根目录）— 工具链实测路径、构建/运行流程、编码约定、协议变更流程、AI 行为准则。
- `docs/DEVLOG.md` — 本文件。

**关键决策与理由**
- **客户端 = C++ + Qt6 Widgets（非 QML）**：服务端是 C++，客户端复用同一语言栈，正好把网络/并发/线程在两侧各练一遍。
- **传输 = 自定义 TCP 二进制帧**：本机 Qt 未装 `QtWebSockets`，走 `QtNetwork` 原生 TCP；帧头二进制（4B 长度 + 2B 类型）+ JSON 载荷。
- **载荷用 JSON 而非手写二进制**：Qt 原生支持、零额外依赖、抓包可读；编解码隔离在 `protocol` 层，未来换 protobuf 只需替换该层。
- **网络层独立线程 + 信号槽跨线程**：网络阻塞绝不进 UI 线程；不用共享可变状态。
- **账号体系（本期新增）**：客户端**自助注册** + 服务端 **SQLite 落盘** + 密码**加盐哈希**（`SHA-256(salt||password)`）。理由：账号本就是服务端职责，无需引入 Web 后端。
- **mock 服务端单线程事件循环**：单线程串行处理 → 无需加锁；SQLite 同步访问也无竞争。
- **UI = 深色现代风格**：深色底 + 卡片化 + 主色强调 + 圆角；颜色抽成 Theme 变量，便于以后换肤（详见 TECH_DESIGN §7 视觉规范）。

**修正的设计隐患**
- 离线补推与历史分页原按 `ts` 排序 → 同毫秒会乱序，改用 SQLite 自增 `id` 单调排序；`ChatReadReq` 字段同步由 `upToTs` 改为 `upToServerMsgId`。

**环境事实（重要，勿忘）**
- Qt **6.12.0 msvc2022_64** → `D:\true_etc\QT\6.12.0\msvc2022_64`
- MSVC 2022 → `E:\vs`；CMake **4.5.0-rc1** → `D:\true_etc\CMAKE\bin\cmake.exe`；**无 ninja**
- Qt 是 MSVC 构建 → **必须用 MSVC 编译**；生成器用 `Visual Studio 17 2022 (x64)`

**踩过的坑**
- 起初按默认路径找工具链，误判「Qt/CMake 未安装」并触发了一次无效的全局扫描。教训：**先问用户，再扫盘**（路径在 `D:\true_etc` 这种非默认位置）。

**下一步（M1）**
- 搭 CMake 骨架：`shared/`(protocol) + `client/` + `server/` 三目标能编译通过 → 客户端弹出空白主窗口。

---

## 条目模板（复制下面这段）

```
## YYYY-MM-DD — 一句话主题

**目标**：

**改动**（文件 → 做了什么）：
-

**决策 / 坑**：
-

**下一步**：
-
```
