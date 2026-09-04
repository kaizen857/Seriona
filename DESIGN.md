# Seriona 前端设计文档

> 本文档由源码逆向分析重建，内容以当前源码实现为准（根目录 `CMakeLists.txt`、`src/`、`qml/`、`scripts/`、`tests/`）。
> 与 `docs/backend-integration-strategy.md`（历史验证记录）不一致处，以本文档与源码为准。

## 1. 项目简介

Seriona 是一个 Qt Quick 桌面音乐播放器的**前端**。它本身不实现文件系统扫描、音频解码、数据库等能力，而是通过 `BackendBridge` 与一个独立仓库的后端 `Seriona_Backend` 交互：前端提交命令（播放、扫描、排序等），后端推送快照（播放状态、曲库树、扫描进度、域通知）作为 UI 的唯一事实来源。

关键事实：

- 单可执行程序 `Seriona`（`qt_add_executable`），QML 模块 URI 为 `Seriona`，入口 `src/main.cpp` 经 `engine.loadFromModule("Seriona", "Main")` 加载 `qml/Main.qml`。
- 后端为可选依赖：`SERIONA_BACKEND_SOURCE_DIR` 置空时为 **mock-only 模式**（无任何真实媒体能力，前端 UI 可完整构建运行）；非空时从本地路径或 GitHub（`main` 分支）引入后端的 control/audio/app 三个目标。
- UI 为无边框单窗口（360×720 起），包含启动页、播放页、歌词页与侧栏曲库。

## 2. 技术栈与构建

| 项 | 值 |
|---|---|
| 语言 | C++23（根 CMake 显式设置 `CMAKE_CXX_STANDARD 23` 并要求该标准） |
| 框架 | Qt 6.8+（`qt_standard_project_setup(REQUIRES 6.8)`） |
| Qt 模块 | Quick、Concurrent、QuickDialogs2、Widgets；C++ Qt Test 测试另需 `Qt6::Test`，无 Qt Quick Test 入口 |
| 构建系统 | mock-only 前端最低 CMake 3.16；默认后端集成与 Windows 发布要求 3.20+；日常开发沿用现有生成器，Windows 发布使用 Visual Studio 17 2022 x64 |
| QML 效果 | 实际使用 `Qt5Compat.GraphicalEffects`（ColorOverlay/RectangularGlow/OpacityMask/DropShadow）；`MainContent.qml` 虽导入 `QtQuick.Effects` 但未使用 |
| 语言服务 | `.clangd` 读 `build/`（相对）；`.qmlls.ini` 硬编码 `build/` 绝对路径 |

标准构建顺序：

```bash
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/seriona
```

Release 模式（`CMAKE_BUILD_TYPE=Release`）按编译器启用全量优化：GCC `-O3 -march=native -flto=N`、Clang `-O3 -march=native -flto`、MSVC `/O2 /GL /LTCG`。

Windows x64 发布由根目录 `build.bat` 调用 `scripts/build-package-windows.ps1`：固定使用 Visual Studio 2022 的 MSVC x64 多配置生成器，通过固定 builtin baseline 的 vcpkg manifest 恢复动态依赖，并以外部 `windeployqt` 加独立包验证器生成 `dist/Seriona-windows-x64/` 与 ZIP。该流程不改变日常构建和现有 CMake 后端引入架构。

## 3. 整体架构

```
┌──────────────────────────── QML 视图层（qml/）────────────────────────────┐
│  Main.qml（无边框窗口外壳，实例化唯一 AppFacade）                            │
│  ├─ views/：StartupView（启动页）、MainContent（播放页/歌词页双态）          │
│  ├─ components/：Sidebar、WaveformProgressBar、BubbleMenu、SortDialog、    │
│  │                DynamicBackground、MarqueeText、StyleButton、WindowControls…│
│  └─ theme/Theme.qml（singleton 设计 token）                                 │
└──────────────┬──────────────────────────────────────────────────────────────┘
               │ 读写 appFacade.playback / library / lyrics / notifications / navigation
┌──────────────▼────────────────── 应用层（src/app，C++ 中间层）──────────────┐
│  AppFacade（唯一组合根；持有全部控制器；快照投影中枢；执行器注入；生命周期）    │
│  ├─ PlaybackController    播放命令 + 曲目视图状态 + 时间轴平滑 + 封面渐变     │
│  ├─ LibraryController/LibraryModel/LibraryTreeStore  曲库树、双游标、排序、扫描│
│  ├─ LyricsModel           歌词列表模型（分隔符切分、时间同步）               │
│  ├─ NotificationController 有界通知队列（容量 12）+ 不支持项本地反馈          │
│  ├─ NavigationController   视图/侧栏/启动屏状态 + 曲库根路径持久化（应用设置存储）│
│  └─ WaveformProvider / ArtworkPaletteWorker（异步波形、封面取色）            │
└──────────────┬──────────────────────────────────────────────────────────────┘
               │ BackendBridge（命令/快照边界）
┌──────────────▼──────────── 后端（Seriona_Backend，独立仓库，可选）──────────┐
│  MediaController（control/audio/app 目标）：播放、扫描、元数据、通知           │
│  推送 PlayerStateSnapshot / LibraryStateSnapshot / PlaylistTreeSnapshot      │
└──────────────────────────────────────────────────────────────────────────────┘
```

核心约束（由 `scripts/verify-middle-layer.sh` 强制检查）：

- 前端（`src/`、`qml/`）不得出现直接实现痕迹：`QDir` 的目录操作成员（`entryList/mkdir/mkpath/rmdir/removeRecursively/setCurrent`）、`QFileSystem*`、`QNetwork*`、`QSql*`、`QTcp/QUdp`、`HTTP`、`database/SQLite`、`persistence` 等（`QDir::cleanPath`、`QFileInfo` 等只读辅助属允许范围，verify 脚本按精确模式检查）。
- 一切后端能力经 `BackendBridge` 的命令/快照边界；QML 不得持有业务状态（mock 属性、假数据）。
- UI-only 操作不得伪造后端命令；不支持的设置项必须走本地通知反馈。

## 4. 目录结构

```
├── CMakeLists.txt            # 全部构建逻辑（1233 行，单文件）
├── src/
│   ├── main.cpp              # 入口：QApplication、smoke CLI、加载 Seriona/Main
│   ├── app/                  # 中间层（AppFacade、控制器、模型、桥接、工具）
│   └── providers/            # thumbnail_image_provider（未接入 CMake，游离源码）
├── qml/
│   ├── Main.qml              # 窗口外壳
│   ├── views/                # MainContent、StartupView
│   ├── components/           # 可复用组件（Sidebar、BubbleMenu、QueueView、TrackContextMenu、ConfirmDeleteDialog 等）
│   ├── windows/              # SettingsWindow、TrackDetailWindow、EqualizerWindow（设置/曲目详情/均衡器弹窗）
│   ├── theme/Theme.qml       # singleton token
│   └── assets/               # 25 个 SVG 图标 + MaterialIcons-Regular.ttf（QML 未引用）
├── tests/frontend/adapter/   # 28 个 QTest 测试源
├── scripts/verify-middle-layer.sh
├── docs/
│   ├── architecture/backend-integration-contract.md   # 现行契约（verify 要求存在）
│   └── backend-integration-strategy.md                # 历史策略记录
└── test_popup.qml            # 未接入 CMake/engine 的实验文件
```

## 5. 模块说明

### 5.1 AppFacade（`src/app/app_facade.{h,cpp}`）

组合根与快照投影中枢：

- 按值持有 7 个控制器：`m_playback`、`m_library`（`LibraryController` 类型）、`m_lyrics`、`m_notifications`、`m_navigation`、`m_settings`（`SettingsController`）、`m_trackStats`（`TrackStatsController`）；以 `unique_ptr` 持有 `BackendBridge`（始终存在）与 `WaveformProvider`（仅 `SERIONA_HAS_BACKEND=1` 时编译）。
- Q_PROPERTY 全部 `CONSTANT`：`layerName`（"Seriona C++ Middle Layer"）、`foundationReady`（恒 true）、`playback/library/lyrics/notifications/navigation/settings/trackStats`。
- 构造时注入执行器（`SERIONA_HAS_BACKEND` 分支内共 8 个：playback/library 命令、文件夹排序、扫描，以及 SettingsController 的输出组 apply、播放过渡组 apply、设备枚举、日志级别），连接播放开始回调（`trackStarted` → `m_trackStats.recordPlayback` 自增播放计数）与 2 类快照信号（playerSnapshotChanged/librarySnapshotChanged）+ 1 类域通知信号（domainNotificationQueued），另连接 `WaveformProvider::waveformReady`；`backendBridgeAutostartEnabled()` 读取 `QCoreApplication` 动态属性 `seriona.backendBridgeAutostartEnabled`（默认 true）决定是否自动 `BackendBridge::start()`。
- `shutdown()` 幂等；析构与 `QCoreApplication::aboutToQuit`（DirectConnection）均触发。
- Q_INVOKABLE：`shutdown()`、`scanLibrary(QUrl)`、`restorePlaylistFromStartup()`；测试钩子：`backendBridgeStartedForTests()` 等。

### 5.2 PlaybackController（`QML_UNCREATABLE`）

- **双通道状态模式**：公开 setter/Q_INVOKABLE（用户意图）→ 构造 `MediaControlCommand` 提交后端；私有 `apply*`（后端确认状态）→ 更新属性并发信号。mock-only 下全部 setter 为 `Q_UNUSED` 空操作。
- 命令映射：`play/pause/togglePlay`→`Play/Pause/TogglePlayPause`、`seek`→`SeekTo`（毫秒）、`setVolume`→`SetVolume`（钳制 0..1）、`setShuffle`→`SetShuffle`、`setRepeatMode`（0=关/1=全部/2=单曲）→`SetRepeatMode`、`skipPrevious/skipNext`、`setMuted`→`SetMuted`；便捷入口 `toggleShuffle()`/`cycleRepeatMode()` 分别取反与循环（0→1→2→0）。
- 时间轴平滑：后端快照 `smooth=true` 时以 100ms `QTimer`（`Qt::PreciseTimer`）从 `position + elapsedSince(sampledAt)` 插值推进，到达总时长停表；否则直接吸附快照位置。
- 封面渐变：`ArtworkPaletteWorker` 后台线程从封面缩略图提取 3 色，用 generation 号丢弃过期取色结果；渐变经 `gradientColor0/1/2` 暴露给 QML `DynamicBackground`。
- 波形：`applyWaveform(heights, barWidth)` 由 `WaveformProvider::waveformReady` 驱动，带去重。
- 31 个 Q_PROPERTY：播放状态、曲目信息、时间文本（`mm:ss`）、波形、渐变、临时队列（`queueEntries`，映射后端快照 `[{trackId, nodeId}]`）等。

### 5.3 曲库模块（`library_model.{h,cpp}`、`library_tree_store.{h,cpp}`）

- **LibraryTreeStore**（非 QObject 纯容器）：以节点 id 为键保存整棵曲库树；`setSnapshot()` 一次重建（含悬空子节点剔除、孤儿回补、root 缺失回退到"无父节点集合"、`descendantTrackCount` 递归）。
- **LibraryModel**（`QAbstractListModel`，`QML_UNCREATABLE`）：把树投影为扁平列表；19 个角色（type/name/title/artist/album/songCount/duration/…/artworkSource/year）；行⇄节点 id 映射；三种投影：根投影、当前文件夹投影（仅直接子级）、搜索投影（当前文件夹子树内只匹配歌曲，文件夹条目不出现；按标题10/歌手5/专辑3/文件名2 加权评分，完全/前缀/包含分别 ×10/×5/×1）；多规则稳定排序（搜索激活时按评分降序，清空恢复用户规则）；`projectionRevision` 在投影完整替换或快照更新后递增（完整替换用 reset 语义）。
- **LibraryFolderProjectionModel**（`library_folder_projection_model.{h,cpp}`，非 QML_ELEMENT，归 LibraryController 所有）：按 folderNodeId 缓存的每级文件夹独立投影模型（`QString()` 根键恒为根投影）。每个 FolderPage 绑定各自模型实例，页面与模型都常驻不销毁 → 滚动位置零成本保留（Qt 无 reset 后恢复滚动位置的契约，故不做恢复，而是让视图与模型都不换）；数据为某文件夹的直接子级投影（过滤/排序规则与主模型投影一致，复用 `sortedProjectionNodeIds`）；监听主模型 `treeChanged` 原地自重建（`setSource`→`rebuildFromSource`，实例身份不变，revision 递增）、`playingTrackIdChanged`/`focusedNodeIdChanged` 仅对投影内行发 `dataChanged`。主模型投影能力保留给搜索/曲库页等其他使用者。
- **LibraryController**（定义于 `library_model.{h,cpp}`，`QML_ELEMENT`，**无独立文件**）：QML 可见门面。
  - **文件夹导航与投影模型缓存**：投影模型按 folderNodeId 缓存（`projectionModelForNodeId(nodeId)` get-or-create，`QString()` 根键）；`enterFolder`/`goBack` 只修改当前文件夹、不销毁任何缓存模型；主树 `treeChanged` 时保留全部缓存模型原地自重建（实例身份不变），`projectionGeneration` 递增供测试与诊断，排序变更同样原地重建；`folderStackDepth` = 当前文件夹祖先链（`ancestorChainForNode`，从根向目标、排除根）的长度（根浏览为 0）；`locateNodeInFolderStack(nodeId)` 从根逐级进入直到目标所在级（目标不在任何已建投影时进入其直接父级）。
  - **双游标分离**：`playingTrackId`（播放身份）与 `selectedBrowserNodeId`/`focusedNodeId`（浏览焦点）独立；`setPlayingTrackId` 仅当 `followCurrentlyPlaying=true` 才移动浏览游标；`locateCurrentSong()` 手动定位（切文件夹、清搜索、选中、发滚动请求），不发播放命令；浏览动作一律不污染播放身份。
  - **扫描状态机**：`scanStatus` ∈ pending/running/error/completed（终态由快照映射）；`scanProgress` 0-100 钳制；`libraryState` 派生为 backendUnavailable/empty/ready；`refresh()` 以 Full 模式重扫已存根；`clearSavedRootPath(msg)` 清根并上报错误消息（启动恢复失败时由 NavigationController 调用）。
  - **排序规则**：内存缓存按 `rootPath\nfolderNodeId` 键存；保存经 `FolderSortExecutor`（未注入则降级 `ApplyFolderSortRules` 命令）持久化到后端，`missingValuePolicy=Last`；搜索期间按相关性评分临时排序、不覆盖已存文件夹规则（搜索中应用排序规则为 no-op）；快照 reconcile 时按回退链恢复焦点/选中/文件夹与规则。
  - 快照 reconcile：`setPlaylistTreeSnapshot` 前记录聚焦/选中节点的祖先链，节点消失后沿链回退，最终落到首个可见节点。

### 5.4 LyricsModel（`QML_ELEMENT`，可创建但约定用 `appFacade.lyrics`）

- 5 角色：rawLine/displayLine/translation/isCurrent/timestampSec；`Qt::DisplayRole` 与 displayLine 等价。
- 按分隔符（默认 `" / "`，可配置）切分显示行与翻译行；`showTranslation` 属性控制翻译显示，`toggleTranslation()`/`selectLyric()` 为 QML 交互入口；`setPlaybackPosition` 同步当前行（最后一条 timestamp ≤ 位置）；无时间戳歌词恒指向第 0 行。
- 快照投影：按 trackId 在曲库树中查歌；同曲目快照刷新保留歌词不清空（去重逻辑只同步索引不 reset）；播放中切换曲目才清空。

### 5.5 NavigationController（`QML_UNCREATABLE`）

- 三组外壳状态：`currentView`（仅 `"playback"`/`"lyrics"` 两值）、`sidebarOpen`（dock/overlay 双模式，`syncSidebarForDockCapability` 同步停靠能力；`manualSidebarToggle` 标记手动开关，Main.qml 用 310ms 定时器在 dock 模式下协调 x/width 动画）、`startupScreenVisible`（启动页）。
- 曲库根路径持久化：应用设置存储（默认内存；AppFacade 接入后端时注入 BackendBridge → 后端键值存储；键 `library/lastScanRoot`）——这是前端唯一的本地持久化（设置类，非媒体数据）。
- 启动恢复：读上次根 → 缺失/非目录时清持久化并给用户错误消息；否则触发扫描（后端模式为 Incremental）并进入主界面。

### 5.6 NotificationController（`QML_UNCREATABLE`）

- 有界队列（容量 12，入队截断）；`notifications` 为 `{kind,code,message,title,severity}` 列表，另暴露 latest* 便捷属性。
- `showUnsupportedAction(name)`：本地构造 `kind="UnsupportedAction"`、severity=warning、`"%1 暂未支持"`——所有不支持设置项的统一反馈通道。
- 后端域通知经 `enqueueDomainNotification` 映射入队（severity 规则：默认 info；播放/扫描错误=error；扫描停止/输出模式回退=warning；`CommandRejected` 专用路径）。
- 日志策略：仅 error（qWarning）与 warning（qInfo）打日志，避免进度类 info 刷屏。

### 5.7 桥接层与异步工具

- **BackendBridge**（类与信号无条件存在，后端成员守卫）：
  - 生命周期：`start()` 幂等（惰性工厂创建 `MediaController`、注册订阅、启动；异常回滚不置 started）；`shutdown()` 先提交 `Stop` 命令再取消订阅、关闭并 reset；析构兜底。
  - 线程模型：命令为调用线程（GUI）同步直调；后端回调线程经 `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 把快照/通知投递回 GUI 线程后应用——所有信号发射均在 GUI 线程。
  - 边界：`m_shuttingDown`/无控制器时返回 `ControllerStopped` 并本地入队 `CommandRejected` 通知（不伪造成功）；通知队列上限 64。
  - `applyFolderSortRules` 是"前端 payload → 类型化后端命令"的转换点（校验 rootPath/folderNodeId、解析 field/order、固定 missingValuePolicy=Last）。
  - `submitTransitionConfig` 是播放过渡组（键组 `transition`，9 键）的转换点：本地按后端 reducer 同界预校验（枚举 0-2、滑块量程一致，越界 → `CommandRejected` 通知），组包为单意图 `SetTransitionConfig` 命令（无 LoadTrack/设备重开副作用）；SettingsController 内部 400ms 去抖（滑块）与立即推送（档位/开关）后经 AppFacade 注入的 executor 到达此处，启动/重连时 apply 一次。
- **BackendCommandAdapter**：后端命令结果/域通知 → 前端视图状态（`CommandResultViewState`/`NotificationViewState`）的映射器（注意：它**不**做前端动作→命令的正向转换）。
- **BackendSnapshotMapper**：快照 → 视图状态纯函数映射：曲目查找（trackId 线性查找，debug 构建有路径兜底）、标题优先级（display→元数据→trackId→默认值）、repeatMode Off/All/One→0/1/2、capability 布尔集合→逗号字符串、扫描状态 Idle/Stopped→pending、Scanning→running、Completed→completed、Error→error 等。
- **WaveformProvider**（整个文件在 `#if SERIONA_HAS_BACKEND` 内，mock 下不存在）：从快照推导请求，`QtConcurrent::run` 线程池调后端 `buildAudioWaveform`；缓存键含 trackId/文件/时间窗/波形参数；"最新请求胜出"（requestId 比对）；失败 emit `waveformFailed` + 空 `waveformReady`；默认参数 60 柱/总宽 320/高 68（柱宽初始占位 3 为 PlaybackController 属性，实际由后端波形输出决定）。
- **ArtworkPaletteWorker**（无后端依赖，始终编译）：单工作线程 + 条件变量；`m_pending` 单槽（新请求覆盖旧）；generation 防陈旧；算法：缩略图缩放 32×32 → 权重筛选像素 → 感知色距合并 → 贪心 3 色 → 压饱和/钳亮度的背景色调；默认 `#4a2c2a/#2b1a1a/#1a1212`。
- **thumbnail_image_provider**：`CMakeLists.txt` 零引用，未接入构建/引擎（同根目录 `test_popup.qml`），修改不影响正式应用。

### 5.8 QML 视图层

- **Main.qml**：360×720 无边框透明窗口（`OpacityMask` 圆角 24，最大化 0）；全局拖拽 + 标题栏 + 封面拖拽（`startSystemMove`）、八向缩放（`startSystemResize`，Maximized 时隐藏）；侧栏 dock（窗口宽 ≥ 800）/overlay 双模式；`smokeScenario` 初始属性在 `Component.onCompleted` 应用；关闭链路 `close() → onClosing → appFacade.shutdown()`。
- **MainContent.qml**（1372 行）：播放/歌词双 state 共享元素迁移（400ms InOutCubic）；播放控制条、音量、进度（波形拖拽 seek、歌词态线性滑杆）、封面三层回退（全图 `coverArtworkSource` → 缩略图 `coverThumbnailSource` → 占位符"🎵"，逐层降级）、设置 BubbleMenu（"设置"→`openSettingsRequested` 打开 SettingsWindow，歌词分隔符等真实设置项在窗口内；"均衡器"→`openEqualizerRequested` 打开 EqualizerWindow；"关于 Seriona"→AboutOverlay 真实关于界面；退出→真实关闭）、通知 toast（3200ms 自动隐藏）。
- **StartupView.qml**：启动页；恢复播放列表、添加文件夹（`Qt.labs.platform.FolderDialog` → `appFacade.scanLibrary`）。
- **Sidebar.qml**：曲库主交互面（树列表带滚动条、表头空白区可拖拽移动窗口、搜索、排序对话框入口、定位当前歌曲 FAB、扫描状态 banner）；delegate 右键菜单（`TrackContextMenu`：详情/下一首播放/删除，删除经 `ConfirmDeleteDialog` 确认）、顶部队列视图（`QueueView`：`PlayNextTrack`/`RemoveFromQueue`）、头部按钮悬停提示（`SharedToolTip`）。文件夹浏览采用 **StackView 页面栈 + FolderPage 实例缓存**：`folderStack` 承载第 1 层及更深文件夹，根视图 `playlistView` 常驻栈外（depth 0 时可见）；`folderPages` 按 folderNodeId 缓存 FolderPage 实例，push/pop 一律传实例、pop/clear 不销毁页面，每层滚动位置与动画状态零成本保留；导航配对调用固定"先栈后 controller"，controller 是导航状态唯一真源，幂等收敛处理器把栈镜像到 controller（重扫/定位等非配对路径自动收敛）；返回根视图时对视口可见 delegate 执行错落滑入。
- 组件清单：`AboutOverlay`（关于弹层）、`BubbleMenu`（气泡菜单+子页 StackView）、`BubbleMenuItem/BubbleSubMenuItem`、`menuRegistry.js`（BubbleMenu 实例注册表，`.pragma library` 单例，多弹窗互斥）、`ConfirmDeleteDialog`（删除确认弹窗）、`DynamicBackground`（3 对角渐变背景）、`FolderPage`（每层文件夹页面：ListView + ScrollBar + 错落滑入动画，实例按 folderNodeId 缓存复用）、`PlaylistDelegate`（共享曲目行 delegate，Sidebar 各列表与 FolderPage 共用）、`MarqueeText`（溢出滚动）、`QueueView`（临时队列视图）、`SharedToolTip`（悬停提示，delay 500ms）、`StyleButton`、`TrackContextMenu`（曲目右键菜单）、`WaveformProgressBar`（数据切换先缩后弹动画）、`SortDialog/SortRuleRow`（最多 5 条规则）、`WindowControls`；`windows/` 下另有 `SettingsWindow`（卡片结构：音频输出〔输出模式/采样率/位深/缓冲时长，Direct 输出时采样率与位深灰化〕→ 播放过渡〔9 键过渡设置：自动前进与手动切歌两档位、传送/进度两个开关与五只长度滑块（交叉/传送淡变/进度淡变/预加载/手动短交叉），键组 `transition` 持久化并经真实 `SetTransitionConfig` 命令下发；Direct 输出时仅 Mixed 生效的 {1 自动档、4 预加载、5 交叉长度、8 手动档、9 手动短交叉} 行灰化并提示「仅混合输出可用」〕→ 设备与系统〔输出设备下拉/日志等级〕→ 歌词分隔符）、`TrackDetailWindow`（曲目详情：年份/播放次数/星级）、`EqualizerWindow`。
- **Theme.qml**：singleton token（颜色/尺寸/动画时长；`animationDuration` 150ms、`colorTransitionDuration` 500ms 等）。注意：`Theme.sidebarWidth` 与 Main.qml 内同名常量并存、`Theme.gradientColor0/1/2` 未见使用点（可能遗留）。

## 6. 模块关系与数据流

### 快照投影链（后端 → UI）

```
BackendBridge::playerSnapshotChanged ──► AppFacade::handlePlayerSnapshotChanged
   ├─► PlaybackController.applyPlayerStateSnapshot(player, &library)
   ├─► LyricsModel.applyPlayerStateSnapshot(player, &library)
   ├─► LibraryController.applyPlayerStateSnapshot(player, false)
   └─► WaveformProvider.requestForSnapshots(player, library) ──► waveformReady ──► PlaybackController.applyWaveform

BackendBridge::librarySnapshotChanged ──► AppFacade::handleLibrarySnapshotChanged
   ├─► LibraryController.applyLibraryStateSnapshot(library)   （扫描状态/进度/错误）
   ├─►（重复上述三条投影，library 侧 forceReapply=true）
   └─► WaveformProvider.requestForSnapshots(...)

BackendBridge::domainNotificationQueued ──►（FolderSortRulesApplied → LibraryController.applyFolderSortSetting）──► NotificationController.enqueueDomainNotification
```

### 命令流（UI → 后端）

```
QML 控件 ──► PlaybackController setter/Q_INVOKABLE ──► submitCommand(MediaControlCommand)
         ──► AppFacade 注入的 executor（= BackendBridge.submitCommand）──► MediaController
QML 曲库 ──► LibraryController.playItem/scanLibrary/applySortRules ──► submitCommand / scanLibrary / applyFolderSortRules
```

命令执行结果在控制器侧被丢弃；被拒命令由 `BackendBridge` 转成 `CommandRejected` 域通知回流。

### 关闭链路

```
WindowControls.closeRequested / 设置菜单"退出" ──► requestApplicationClose ──► window.close()
──► onClosing ──► appFacade.shutdown()（幂等：取消波形 → BackendBridge.shutdown → 提交 Stop 命令 → 关闭控制器）
```

## 7. 启动流程

1. `main.cpp`：解析 `--smoke-*` 参数（见 §9.3）；`QApplication`；接入后端时初始化后端日志（FFmpeg av_log 级别按 NDEBUG 调整）。
2. 创建 `QQmlApplicationEngine`；smoke 模式设置 `seriona.backendBridgeAutostartEnabled=false` 并注入初始属性 `smokeScenario`。
3. `loadFromModule("Seriona", "Main")` → Main.qml 实例化 `AppFacade`。
4. AppFacade 构造：注入执行器、连接快照信号、`start()` 后端桥（默认自动）。
5. Main.qml `Component.onCompleted`：应用 smoke 场景（若有）；正常模式默认显示启动页（`startupScreenVisible=true`）。
6. 启动页用户动作：恢复播放列表（`AppFacade::restorePlaylistFromStartup` → 读应用设置中的上次根 → 扫描）或添加文件夹 → 进入主界面（`enterMainShell`：隐藏启动页、切到 playback 视图）。

## 8. 核心运行流程

| 场景 | 流程 |
|---|---|
| 播放/暂停/切歌 | QML 按钮 → PlaybackController Q_INVOKABLE → 后端命令；后端快照回流 → `applyPlayerStateSnapshot` 更新 UI（含时间轴平滑） |
| 点播曲目 | Sidebar 点击 → `LibraryController.playItem` → 构造带文件夹/搜索上下文的 `StartPlaybackFromContext` 命令 |
| 浏览/定位 | `enterFolder`/`goBack`/`selectBrowserNode` 本地改投影；`locateCurrentSong` 按播放曲目切文件夹+滚动，不发命令 |
| 排序 | SortDialog 编辑（≤5 条）→ `applySortRules` → 本地重投影 + `FolderSortExecutor`/`ApplyFolderSortRules` 持久化；后端回推 `FolderSortRulesApplied` 同步 |
| 扫描 | `scanLibrary(QUrl)` → 后端扫描命令 → 快照回流更新状态机与曲库树 |
| 歌词 | 快照按 trackId 匹配 → 行/翻译切分 → `playbackPosition` 绑定驱动当前行 |
| 波形 | 快照 → `requestForSnapshots` → 线程池生成 → `waveformReady` → 波形条渲染，拖拽 seek |
| 封面背景 | 缩略图 → `ArtworkPaletteWorker` → 3 色渐变 → `DynamicBackground` 500ms 渐变 |

## 9. 配置方式

### 9.1 构建期（CMake 缓存变量）

| 变量 | 默认 | 说明 |
|---|---|---|
| `BUILD_TESTING` | ON | mock-only 只注册 8 个测试；其余 20 个测试二进制要求后端 |
| `SERIONA_BACKEND_SOURCE_DIR` | `../Seriona_Backend` | 空= mock-only；存在=本地引入；不存在=FetchContent GitHub main |
| `CMAKE_BUILD_TYPE` | — | Release 触发 LTO/`-march=native` |
| `SERIONA_FETCHCONTENT_CATCH2_DIR` / `SERIONA_FETCHCONTENT_THREAD_POOL_DIR` | — | 离线 configure 注入后端依赖源码 |

`SERIONA_HAS_BACKEND` 由 CMake 生成器表达式推导（`$<BOOL:${SERIONA_BACKEND_CONTROL_TARGET}>`），不手动设置。

### 9.2 运行期

- 应用设置存储：三个控制器（SettingsController / NavigationController / TrackStatsController）共用；默认内存存储（进程内，mock-only/smoke 有效），AppFacade 接入后端时注入 BackendBridge（命令/快照边界）→ 后端键值存储（`app_settings` 表），读取失败回退内存缓存。
- QCoreApplication 动态属性：`seriona.backendBridgeAutostartEnabled`（跳过后端自启）、`seriona.smokeScenario`（smoke 场景名，见 §9.3）。
- `Theme.qml` token：共享颜色/尺寸/动画参数。

### 9.3 Smoke CLI（`./build/seriona`）

```
--smoke-scenario=<startup|main-playback|lyrics|sidebar-tree|settings-menu|empty-library>
--smoke-exit-ms=<ms>          # 默认 1000
--smoke-output-dir=<dir>      # 默认 .omo/evidence/smoke
```

- 启用时禁后端自启、写入 `smoke-<scenario>.log`（scenario/exit_ms/timestamp_utc/artifact 四行）后定时退出；`smokeScenario` 经初始属性传入 QML，由 `applySmokeScenario` 驱动各场景视图动作（`smokeVisualStateJson()` 聚合窗口/曲库/播放文本供后续扩展）。
- 退出码：0 正常；2 参数/场景非法；3 日志写入失败；-1 QML 创建失败。
- `verify-middle-layer.sh` 的 offscreen 冒烟：`QT_QPA_PLATFORM=offscreen timeout 5s ./build/seriona` 必须退出码 124。

## 10. 测试体系

- 位置：`tests/frontend/adapter/`（28 个 `tst_*.cpp`，QTest）。
- mock-only 注册 8 个：`seriona_frontend_command_result_mapping`、`seriona_frontend_snapshot_mapping`、`seriona_frontend_library_tree_mapping`、`seriona_frontend_settings_controller_tests`、`seriona_frontend_track_stats_tests`、`seriona_frontend_about_overlay_tests`、`seriona_frontend_queue_view_tests`、`seriona_frontend_app_facade_smoke_mode`（无后端时映射测试降级为占位用例，facade 用例 QSKIP）。
- 后端模式另注册 20 个二进制（强制 `SERIONA_HAS_BACKEND=1`），覆盖：桥接线程/关闭、库模型投影、文件夹投影/排序（15 个 CTest 用例）、双游标、侧栏浏览/队列切换、选曲上下文、扫描流程、启动恢复、播放快照/命令、通知、波形、当前曲目、歌词、UI-only 策略、文件夹页面、曲目详情、封面迁移（唯一加载真实 `MainContent.qml` 的集成测试，5954 行）。
- 测试缝（生产代码中为测试开出的接口）：`setScanExecutor`、`setPlaylistTreeSnapshot`、`applyPlayerStateSnapshot`、`BackendBridge::ControllerFactory`/`drainForTests`、`WaveformProvider::setGeneratorForTests`、AppFacade `*ForTests` 钩子、两个 QCoreApplication 属性。
- fake 后端范式：测试内实现后端纯虚接口（`AudioPlaybackService`/`FileScannerService`/`MetadataSharingService`/`FolderSortSettingsStore`）经 `ControllerHarness` 组装。
- 单用例运行：`./build/seriona_frontend_library_sort_tests titleAscendingAndDescendingSortCurrentFolderProjection`。
- `verify-middle-layer.sh`（依赖 `rg`）：configure → build → 不变量断言（源码/目标注册、禁 mock 文案、禁直接 IO、契约文档存在）→ offscreen smoke；`SERIONA_BUILD_DIR` 可覆盖构建目录。

## 11. 扩展方式

1. **新设置项**：① 前端本地项（如歌词分隔符）：加真实绑定/写回即可；② 未实现项（均衡器类）：必须 `showUnsupportedAction()` 本地反馈，禁止伪造命令；③ 需后端支持（范例：播放过渡 9 键组 → 后端 `SetTransitionConfig`，前端经 `SettingsController` executor + `BackendBridge::submitTransitionConfig` 接线，滑块 400ms 去抖、启动 apply 一次）：先在后端加命令与快照字段，前端经 `BackendBridge` 接线，并同步更新 `docs/architecture/backend-integration-contract.md`。
2. **新页面**：`NavigationController.currentView` 增加视图名 + MainContent 增加对应 State/Transition；或独立 QML 组件挂到 Main.qml 组件树。
3. **新图标**：SVG 放 `qml/assets/` 并追加到 CMake `SERIONA_QML_MODULE_RESOURCES`；引用用绝对 QRC 路径 `qrc:/qt/qml/Seriona/qml/assets/<name>.svg`。
4. **新 C++ 控制器/模型/工具**：放入 `src/app/`，追加到 CMake `SERIONA_APP_LAYER_SOURCES`（及测试目标清单）；QML 侧经 `AppFacade` 暴露，不要另建可创建的控制器实例。
5. **新测试**：`tests/frontend/adapter/` 新增 `tst_*.cpp`，在 CMakeLists 注册 `add_executable`/`add_test`（后端依赖的放 `if(SERIONA_BACKEND_CONTROL_TARGET)` 内），并把文件/目标追加到 `verify-middle-layer.sh` 的必需清单。
6. **对接/更新后端**：默认同级目录 `../Seriona_Backend`；版本由 `SERIONA_BACKEND_SOURCE_DIR` 指向的 checkout 决定，勿在仓库内写死路径。

## 12. 开发与维护建议

- **守契约**：任何改动后跑 `./scripts/verify-middle-layer.sh`；不要绕过中间层直接 IO，不要在 QML 放业务状态或 mock 文案。
- **CMake 同步**：新增/重命名任何 `src/app` 源、模块 QML、SVG、测试源都要同步 CMakeLists 三张清单；`.qmlls.ini` 的 buildDir 是绝对路径，换 worktree 后先修正。
- **mock-only 差异**：无后端时 PlaybackController 的 setter 全是空操作、WaveformProvider 类型不存在、大多数测试不注册——排查"UI 点了没反应"先确认构建模式。
- **双游标**：改曲库交互时保持播放身份（`playingTrackId`）与浏览焦点分离；"定位当前歌曲"只动浏览。
- **快照是事实来源**：前端状态一律由 `apply*` 从后端快照更新，UI 直写只用于提交意图；排序/扫描/播放的终态以后端回推为准。
- **遗留/游离文件**：`src/providers/`、`test_popup.qml`、`MaterialIcons-Regular.ttf`（QML 未引用）未接入构建，修改不影响应用；清理前确认无引用。
- **已知文档漂移**：`docs/backend-integration-strategy.md` 中的默认路径/链接目标与现 CMake 不一致，属历史记录，以 CMakeLists.txt 为准。
