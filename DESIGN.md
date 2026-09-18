# Seriona 前端设计文档

> 本文档由源码逆向分析重建，内容以当前源码实现为准（根目录 `CMakeLists.txt`、`src/`、`qml/`、`scripts/`、`tests/`）。
> 与 `docs/backend-integration-strategy.md`（历史验证记录）不一致处，以本文档与源码为准。

## 1. 项目简介

Seriona 是一个 Qt Quick 桌面音乐播放器的**前端**。它本身不实现文件系统扫描、音频解码、数据库等能力，而是通过 `BackendBridge` 与一个独立仓库的后端 `Seriona_Backend` 交互：前端提交命令（播放、扫描、排序等），后端推送快照（播放状态、曲库树、扫描进度、域通知）作为 UI 的唯一事实来源。

关键事实：

- 单可执行程序 `Seriona`（`qt_add_executable`），QML 模块 URI 为 `Seriona`，入口 `src/main.cpp` 经 `engine.loadFromModule("Seriona", "Main")` 加载 `qml/Main.qml`。
- 后端为可选依赖：`SERIONA_BACKEND_SOURCE_DIR` 置空时为 **mock-only 模式**（无任何真实媒体能力，前端 UI 可完整构建运行）；非空时优先复用已安装的 `SerionaBackend`（`find_package(SerionaBackend CONFIG QUIET)`），未命中再从本地路径或 GitHub（`main` 分支）引入后端的 control/audio/app 三个目标。
- 主界面为无边框单窗口（360×720 起），包含启动页、播放页、歌词页与侧栏曲库；设置、均衡器、曲目详情是独立的辅助窗口（不参与 `lastWindowClosed` 判定，主窗关闭链路显式退出，见 §6）。

## 2. 技术栈与构建

| 项 | 值 |
|---|---|
| 语言 | C++23（根 CMake 显式设置 `CMAKE_CXX_STANDARD 23` 并要求该标准） |
| 框架 | Qt 6.8+（`qt_standard_project_setup(REQUIRES 6.8)`） |
| Qt 模块 | Quick、Concurrent、QuickDialogs2、Widgets、Network（单实例守卫的本地 socket）；C++ Qt Test 测试另需 `Qt6::Test`，无 Qt Quick Test 入口 |
| 构建系统 | mock-only 前端最低 CMake 3.27；默认后端集成与 Windows 发布要求 3.27+；日常开发沿用现有生成器，Windows 发布使用 Visual Studio 17 2022 x64 |
| QML 效果 | 实际使用 `Qt5Compat.GraphicalEffects`（ColorOverlay/RectangularGlow/OpacityMask/DropShadow）；`MainContent.qml` 虽导入 `QtQuick.Effects` 但未使用 |
| 语言服务 | `.clangd` 读 `build/`（相对）；`.qmlls.ini` 硬编码 `build/` 绝对路径 |

标准构建顺序：

```bash
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/seriona
```

Release 优化分两处提供：编译优化由 CMake 默认 Release 配置给出（GCC `-O3 -DNDEBUG`、MSVC `/O2 /DNDEBUG`）；LTO 由 `CMakePresets.json` 的 `release` 预设给出（`CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE=ON`：GCC `-flto`、MSVC `/GL /LTCG`）。另有 `CMakeLists.txt` 统一附加的指令集基线：GNU/Clang 且 x86_64 时 `-march=x86-64`（替代 `-march=native`，保证产物可跨机器分发），**不限 Release，任何构建类型均生效**。

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
               │ 读写 appFacade.playback / library / lyrics / notifications / navigation / settings / trackStats
┌──────────────▼────────────────── 应用层（src/app，C++ 中间层）──────────────┐
│  AppFacade（唯一组合根；持有全部控制器；快照投影中枢；执行器注入；生命周期）    │
│  ├─ PlaybackController    播放命令 + 曲目视图状态 + 时间轴平滑 + 封面渐变     │
│  ├─ LibraryController/LibraryModel/LibraryTreeStore  曲库树、双游标、排序、扫描│
│  ├─ LyricsModel           歌词列表模型（分隔符切分、时间同步）               │
│  ├─ NotificationController 有界通知队列（容量 12）+ 不支持项本地反馈          │
│  ├─ NavigationController   视图/侧栏/启动屏状态 + 曲库根路径持久化（应用设置存储）│
│  ├─ SettingsController     输出/过渡/均衡器设置 + 应用设置存储注入 + 只读镜像面   │
│  ├─ TrackStatsController   播放次数/星级持久化                              │
│  └─ WaveformProvider / ArtworkPaletteWorker（异步波形、封面取色）            │
└──────────────┬──────────────────────────────────────────────────────────────┘
               │ BackendBridge（命令/快照边界）
┌──────────────▼──────────── 后端（Seriona_Backend，独立仓库，可选）──────────┐
│  MediaController（control/audio/app 目标）：播放、扫描、元数据、通知           │
│  推送 PlayerStateSnapshot / LibraryStateSnapshot / PlaylistTreeSnapshot      │
└──────────────────────────────────────────────────────────────────────────────┘
```

核心约束（架构红线；`scripts/verify-middle-layer.sh` 会机械检查其中大部分）：

- 前端（`src/`、`qml/`）不得出现直接实现痕迹：`QDir` 的目录操作成员（`entryList/mkdir/mkpath/rmdir/removeRecursively/setCurrent`）、`QFileSystem*`、`QNetwork*`、`QSql*`、`QTcp/QUdp`、`HTTP`、`database/SQLite`、`persistence` 等（`QDir::cleanPath`、`QFileInfo` 等只读辅助属允许范围，verify 脚本按精确模式检查）。
- **一切后端能力经 `BackendBridge` 的命令/快照边界**；QML 不得持有业务状态（mock 属性、假数据）。要求"唯一边界"的理由很具体：单一边界让前端在完全没有后端（mock-only）时仍能完整构建运行，并让"快照即事实来源"可被机械验证——任何绕开它的直接文件/网络/数据库访问都会同时破坏这两点。
- **路径文本一律 UTF-8**：与后端边界交换的路径文本必须经 `src/app/path_text.h` 的 `pathTextUtf8` / `pathFromUtf8`；禁止直接使用 `std::filesystem::path::string()` / `generic_string()`——它们在 Windows 上按 ANSI 代码页转换，遇到不可表示字符会抛异常或产生乱码。
- **UI-only 操作不得伪造后端命令**；不支持的设置项必须经 `NotificationController::showUnsupportedAction()` 提供本地反馈，不得静默吞掉。**`Exit` 是唯一例外**：它不属于"不支持项"，必须走真实关闭链路（见 §6 关闭链路）。
- **不要另建可创建的控制器/模型**：正式 QML 统一经 `appFacade.playback / library / lyrics / notifications / navigation / settings / trackStats` 访问状态；需要新状态时加到中间层并由 `AppFacade` 暴露，而不是在 QML 里造可创建实例（见 §11.4）。

## 4. 目录结构

```
├── CMakeLists.txt            # 全部构建逻辑（1521 行，单文件）
├── src/
│   ├── main.cpp              # 入口：QApplication、应用图标/版本、smoke CLI、单实例守卫、加载 Seriona/Main
│   └── app/                  # 中间层（AppFacade、控制器、模型、桥接、均衡器渲染、工具）
├── qml/
│   ├── Main.qml              # 窗口外壳
│   ├── views/                # MainContent、StartupView
│   ├── components/           # 可复用组件（Sidebar、BubbleMenu、QueueView、TrackContextMenu、ConfirmDeleteDialog 等）
│   ├── windows/              # SettingsWindow、TrackDetailWindow、EqualizerWindow、EqualizerPresetDialog（设置/曲目详情/均衡器弹窗）
│   ├── theme/Theme.qml       # singleton token
│   └── assets/               # 25 个 SVG 图标 + 8 个 app-icon-*.png（窗口/应用图标，均注册进 CMake）
├── tests/frontend/adapter/   # 32 个 QTest 测试源（CMake 注册集中在顶层 CMakeLists.txt）
├── scripts/verify-middle-layer.sh
├── docs/
│   ├── architecture/backend-integration-contract.md   # 现行契约（verify 要求存在）
│   ├── folder-navigation-scroll-state.md              # 文件夹导航滚动状态方案（背景材料，非契约）
│   └── backend-integration-strategy.md                # 历史策略记录
```

## 5. 模块说明

### 5.1 AppFacade（`src/app/app_facade.{h,cpp}`）

组合根与快照投影中枢：

- 按值持有 7 个控制器：`m_playback`、`m_library`（`LibraryController` 类型）、`m_lyrics`、`m_notifications`、`m_navigation`、`m_settings`（`SettingsController`）、`m_trackStats`（`TrackStatsController`）；以 `unique_ptr` 持有 `BackendBridge`（始终存在）与 `WaveformProvider`（仅 `SERIONA_HAS_BACKEND=1` 时编译）。
- Q_PROPERTY 全部 `CONSTANT`：`layerName`（"Seriona C++ Middle Layer"）、`foundationReady`（恒 true）、`playback/library/lyrics/notifications/navigation/settings/trackStats`。
- 构造时注入执行器（`SERIONA_HAS_BACKEND` 分支内共 9 个：playback/library 命令、文件夹排序、扫描，以及 SettingsController 的输出组 apply、播放过渡组 apply、均衡器组 apply、设备枚举、日志级别），连接播放开始回调（`trackStarted` → `m_trackStats.recordPlayback` 自增播放计数）、2 类快照信号（playerSnapshotChanged/librarySnapshotChanged）、均衡器与频谱订阅信号（equalizerStateChanged/spectrumChanged → SettingsController 只读镜像面）、1 类域通知信号（domainNotificationQueued，含 FolderSortRulesApplied 应用与 ConfigureOutput 拒绝回退两支），另连接 `WaveformProvider::waveformReady`（`m_shuttingDown` 时丢弃）；`backendBridgeAutostartEnabled()` 读取 `QCoreApplication` 动态属性 `seriona.backendBridgeAutostartEnabled`（默认 true）决定是否自动 `BackendBridge::start()`。SettingsController 的歌词分隔符变化另经本地信号同步到 `LyricsModel`。
- `shutdown()` 幂等；析构与 `QCoreApplication::aboutToQuit`（DirectConnection）均触发。
- Q_INVOKABLE：`shutdown()`、`scanLibrary(QUrl)`、`restorePlaylistFromStartup()`；曲目/队列命令入口 `deleteTarget(path, folder)`、`playNextTrack(trackId)`、`removeFromQueue(queueIndex)`（后两者与删除链消费命令结果的 `accepted` 并本地 toast，mock-only 下走 `NotificationController::showUnsupportedAction`）与只读查询 `filePathForNodeId(nodeId)`；测试钩子：`backendBridgeStartedForTests()`、`backendNotificationCountForTests()` 等。

### 5.2 PlaybackController（`QML_UNCREATABLE`）

- **双通道状态模式**：公开 setter/Q_INVOKABLE（用户意图）→ 构造 `MediaControlCommand` 提交后端；私有 `apply*`（后端确认状态）→ 更新属性并发信号。
- **mock-only 行为**：用户意图入口在无后端时几乎全部退化为空操作——`setPlaying`/`setVolume`/`setShuffle`/`setRepeatMode`/`seek`/`setMuted` 是显式 `Q_UNUSED` 空体，`play`/`pause`/`togglePlay`/`skipPrevious`/`skipNext` 是守卫下的空体，`setCurrentPosition`（委托 `seek`）、`toggleShuffle`（委托 `setShuffle`）、`cycleRepeatMode`（委托 `setRepeatMode`）经组合继承空操作。**唯一例外**是 `totalDuration` 的 Q_PROPERTY WRITE setter `setTotalDuration`：它直接委托 `applyTotalDuration` 写 read model，不经后端，mock-only 下依然改状态——排查"UI 点了没反应"时不可把它当作空操作。
- 命令映射：`play/pause/togglePlay`→`Play/Pause/TogglePlayPause`、`seek`→`SeekTo`（毫秒）、`setVolume`→`SetVolume`（钳制 0..1）、`setShuffle`→`SetShuffle`、`setRepeatMode`（0=关/1=全部/2=单曲）→`SetRepeatMode`、`skipPrevious/skipNext`、`setMuted`→`SetMuted`；便捷入口 `toggleShuffle()`/`cycleRepeatMode()` 分别取反与循环（0→1→2→0）。
- 时间轴平滑：后端快照 `smooth=true` 时以 100ms `QTimer`（`Qt::PreciseTimer`）从 `position + elapsedSince(sampledAt)` 插值推进，到达总时长停表；否则直接吸附快照位置。
- 封面渐变：`ArtworkPaletteWorker` 后台线程从封面缩略图提取 3 色，用 generation 号丢弃过期取色结果；渐变经 `gradientColor0/1/2` 暴露给 QML `DynamicBackground`。
- 波形：`applyWaveform(heights, barWidth)` 由 `WaveformProvider::waveformReady` 驱动，带去重。
- 31 个 Q_PROPERTY：播放状态、曲目信息、时间文本（`mm:ss`）、波形、渐变、临时队列（`queueEntries`，映射后端快照 `[{trackId, nodeId}]`）等。

### 5.3 曲库模块（`library_model.{h,cpp}`、`library_tree_store.{h,cpp}`）

- **LibraryTreeStore**（非 QObject 纯容器）：以节点 id 为键保存整棵曲库树；`setSnapshot()` 一次重建（含悬空子节点剔除、孤儿回补、root 缺失回退到"无父节点集合"、`descendantTrackCount` 递归）。
- **LibraryModel**（`QAbstractListModel`，`QML_UNCREATABLE`）：把树投影为扁平列表；19 个角色（type/name/title/artist/album/songCount/duration/…/artworkSource/year）；行⇄节点 id 映射；三种投影：根投影、当前文件夹投影（仅直接子级）、搜索投影（当前文件夹子树内只匹配歌曲，文件夹条目不出现；按标题10/歌手5/专辑3/文件名2 加权评分，完全/前缀/包含分别 ×10/×5/×1）；多规则稳定排序（搜索激活时按评分降序，清空恢复用户规则）；`projectionRevision` 在投影完整替换或快照更新后递增（完整替换用 reset 语义）。
- **LibraryFolderProjectionModel**（`library_folder_projection_model.{h,cpp}`，非 QML_ELEMENT，归 LibraryController 所有）：按 folderNodeId 缓存的每级文件夹独立投影模型（`QString()` 根键恒为根投影）。每个 FolderPage 绑定各自模型实例，页面与模型都常驻不销毁 → 滚动位置零成本保留（Qt 无 reset 后恢复滚动位置的契约，故不做恢复，而是让视图与模型都不换）；数据为某文件夹的直接子级投影（过滤/排序规则与主模型投影一致，复用 `sortedProjectionNodeIds`）；监听主模型 `treeChanged` 原地增量自重建（`rebuildFromSource` 经 `row_diff` 只发行操作/`rowsMoved`/批量 `dataChanged`，不 reset，视图视口不归零；仅 `setSource` 首建/数据源切换/文件夹切换走 `resetFromSource`，同一源+同一文件夹的排序规则变更同样走增量行操作；实例身份不变，revision 递增）、`playingTrackIdChanged`/`focusedNodeIdChanged` 仅对投影内行发 `dataChanged`。主模型投影能力保留给搜索/曲库页等其他使用者。
- **LibraryController**（定义于 `library_model.{h,cpp}`，`QML_ELEMENT`，**无独立文件**）：QML 可见门面。
  - **文件夹导航与投影模型缓存**：投影模型按 folderNodeId 缓存（`projectionModelForNodeId(nodeId)` get-or-create，`QString()` 根键）；`enterFolder`/`goBack` 只修改当前文件夹、不销毁任何缓存模型；主树 `treeChanged` 时保留全部缓存模型原地自重建（实例身份不变），`projectionGeneration` 递增供测试与诊断，排序变更同样原地重建；`folderStackDepth` = 当前文件夹祖先链（`ancestorChainForNode`，从根向目标、排除根）的长度（根浏览为 0）；`locateNodeInFolderStack(nodeId)` 从根逐级进入直到目标所在级（目标不在任何已建投影时进入其直接父级）。
  - **双游标分离**：`playingTrackId`（播放身份）与 `selectedBrowserNodeId`/`focusedNodeId`（浏览焦点）独立；`setPlayingTrackId` 仅当 `followCurrentlyPlaying=true` 才移动浏览游标；`locateCurrentSong()` 手动定位（切文件夹、清搜索、选中、发滚动请求），不发播放命令；浏览动作一律不污染播放身份。
  - **扫描状态机**：`scanStatus` ∈ pending/running/error/completed（终态由快照映射）；`scanProgress` 0-100 钳制；同时暴露 `scannedSongCount`/`totalSongCount` 供视图展示扫描进度（`MainContent` 经自身 scan 状态呈现进度 toast、`Sidebar`/`StartupView` 经 `scanMessage`——进度状态归 LibraryController 所有，视图只消费不持有）；`libraryState` 派生为 backendUnavailable/empty/ready；`refresh()` 以 Incremental 重扫已存根，`forceRescan()` 显式 Full 强制全量重扫；`clearSavedRootPath(msg)` 清根并上报错误消息（启动恢复失败时由 NavigationController 调用）。
  - **排序规则**：内存缓存按 `rootPath\nfolderNodeId` 键存；保存经 `FolderSortExecutor`（未注入则降级 `ApplyFolderSortRules` 命令）持久化到后端，`missingValuePolicy=Last`；搜索期间按相关性评分临时排序、不覆盖已存文件夹规则（搜索中应用排序规则为 no-op）；快照 reconcile 时按回退链恢复焦点/选中/文件夹与规则。
  - 快照 reconcile：`setPlaylistTreeSnapshot` 前记录聚焦/选中节点的祖先链，节点消失后沿链回退，最终落到首个可见节点。

### 5.4 LyricsModel（`QML_ELEMENT`，可创建但约定用 `appFacade.lyrics`）

- 5 角色：rawLine/displayLine/translation/isCurrent/timestampSec；`Qt::DisplayRole` 与 displayLine 等价。
- 按分隔符（默认 `" / "`，可配置）切分显示行与翻译行；`showTranslation` 属性控制翻译显示，`toggleTranslation()`/`selectLyric()` 为 QML 交互入口；`setPlaybackPosition` 同步当前行（最后一条 timestamp ≤ 位置）；无时间戳歌词恒指向第 0 行。
- 快照投影：按 trackId 在曲库树中查歌，再用快照歌词整体替换模型内容。**去重按内容判定**（`hasTimedLyrics` + 行数 + 逐行 timestamp/text 全等）：内容相同则只重新同步当前行索引，不 reset、不清空；内容不同才 `beginResetModel` 重建。因此"同一曲目快照刷新不清空歌词"成立，而播放中切换曲目（内容必然不同）才真正清空；快照中该曲无歌词时同样清空。

### 5.5 NavigationController（`QML_UNCREATABLE`）

- 三组外壳状态：`currentView`（仅 `"playback"`/`"lyrics"` 两值）、`sidebarOpen`（dock/overlay 双模式，`syncSidebarForDockCapability` 同步停靠能力；`manualSidebarToggle` 标记手动开关，Main.qml 用 310ms 定时器在 dock 模式下协调 x/width 动画）、`startupScreenVisible`（启动页）。
- 曲库根路径持久化：应用设置存储（默认内存；AppFacade 接入后端时注入 BackendBridge → 后端键值存储；键 `library/lastScanRoot`）——这是前端唯一的本地持久化（设置类，非媒体数据）。
- 启动恢复：读上次根 → 缺失/非目录时清持久化并给用户错误消息；否则触发扫描（后端模式为 Incremental）并进入主界面。

### 5.6 NotificationController（`QML_UNCREATABLE`）

- 有界队列（容量 12，入队截断）；`notifications` 为 `{kind,code,message,title,severity}` 列表，另暴露 latest* 便捷属性。
- `showUnsupportedAction(name)`：本地构造 `kind="UnsupportedAction"`、severity=warning、`"%1 暂未支持"`——所有不支持设置项的统一反馈通道。
- 后端域通知经 `enqueueDomainNotification` 映射入队（severity 规则：默认 info；播放/扫描错误=error；扫描停止/输出模式回退=warning；`CommandRejected` 专用路径）。
- 日志策略：仅 error 与 warning 级别打日志（经 spdlog 输出，避免进度类 info 刷屏）。

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
- **应用设置存储**（`app_settings_storage.{h,cpp}`）：三控制器共用的 read/write/remove 存储抽象，默认内存实现（进程内）；AppFacade 接入后端时注入 `BackendBridge` 键值存储实现（见 §9.2）。
- **均衡器 C++ 面**（无后端依赖，随应用编译）：`spectrum_graph.{h,cpp}`（`QML_ELEMENT` 的 QSG 自绘频谱柱/频响曲线图元，坐标映射函数是 QML 刻度覆盖层的几何单一源）、`spectrum_display_model.{h,cpp}`（频谱柱平滑模型，纯 C++，无 QtQuick 依赖）、`equalizer_curve_synth.{h,cpp}`（181 点频响曲线本地合成：物理叠加响应与后端 reducer 逐点同源、显示曲线为手柄点 PCHIP 包络）、`equalizer_presets.h`（增益域与内置预设单一源）。这些模块只服务 `windows/EqualizerWindow.qml` 的图谱区，不参与快照投影链。

### 5.8 QML 视图层

- **Main.qml**：360×720 无边框透明窗口（`OpacityMask` 圆角 24，最大化 0）；全局拖拽 + 标题栏 + 封面拖拽（`startSystemMove`）、八向缩放（`startSystemResize`，Maximized 时隐藏）——**改动标题栏、遮罩或边框时必须保住这两条交互**（无边框窗口没有系统装饰兜底，破坏即失去移动/缩放能力）；侧栏 dock（窗口宽 ≥ 800）/overlay 双模式；`smokeScenario` 初始属性在 `Component.onCompleted` 应用；关闭链路 `close() → onClosing → appFacade.shutdown() → Qt.quit()`（设置/均衡器/详情为独立窗口、不参与 lastWindowClosed 判定，主窗关闭时显式退出）。
- **MainContent.qml**（1853 行）：播放/歌词双 state 共享元素迁移（400ms InOutCubic）；播放控制条、音量、进度（波形拖拽 seek、歌词态线性滑杆）、封面三层回退（全图 `coverArtworkSource` → 缩略图 `coverThumbnailSource` → 占位符"🎵"，逐层降级）、设置 BubbleMenu（"设置"→`openSettingsRequested` 打开 SettingsWindow，歌词分隔符等真实设置项在窗口内；"均衡器"→`openEqualizerRequested` 打开 EqualizerWindow；"关于 Seriona"→AboutOverlay 真实关于界面；退出→真实关闭）、通知 toast（3200ms 自动隐藏）。
- **StartupView.qml**：启动页；恢复播放列表、添加文件夹（`Qt.labs.platform.FolderDialog` → `appFacade.scanLibrary`）。
- **Sidebar.qml**：曲库主交互面（树列表带滚动条、表头空白区可拖拽移动窗口、搜索、排序对话框入口、定位当前歌曲 FAB、扫描状态 banner）；delegate 右键菜单（`TrackContextMenu`：详情/下一首播放/删除，删除经 `ConfirmDeleteDialog` 确认）、顶部队列视图（`QueueView`：`PlayNextTrack`/`RemoveFromQueue`）、头部按钮悬停提示（`SharedToolTip`）。文件夹浏览采用 **StackView 页面栈 + FolderPage 实例缓存**：`folderStack` 承载第 1 层及更深文件夹，根视图 `playlistView` 常驻栈外（depth 0 时可见）；`folderPages` 按 folderNodeId 缓存 FolderPage 实例，push/pop 一律传实例、pop/clear 不销毁页面，每层滚动位置与动画状态零成本保留；导航配对调用固定"先栈后 controller"，controller 是导航状态唯一真源，幂等收敛处理器把栈镜像到 controller（重扫/定位等非配对路径自动收敛）；返回根视图时对视口可见 delegate 执行错落滑入。
- 组件清单：`AboutOverlay`（关于弹层）、`BubbleMenu`（气泡菜单+子页 StackView）、`BubbleMenuItem/BubbleSubMenuItem`、`menuRegistry.js`（BubbleMenu 实例注册表，`.pragma library` 单例，多弹窗互斥）、`ConfirmDeleteDialog`（删除确认弹窗）、`DynamicBackground`（3 对角渐变背景）、`FolderPage`（每层文件夹页面：ListView + ScrollBar + 错落滑入动画，实例按 folderNodeId 缓存复用）、`PlaylistDelegate`（共享曲目行 delegate，Sidebar 各列表与 FolderPage 共用）、`MarqueeText`（溢出滚动）、`QueueView`（临时队列视图）、`SharedToolTip`（悬停提示，delay 500ms）、`StyleButton`、`TrackContextMenu`（曲目右键菜单）、`WaveformProgressBar`（数据切换先缩后弹动画）、`SortDialog/SortRuleRow`（最多 5 条规则）、`StyledScrollBar`（Sidebar 列表滚动条）、`WindowControls`；`windows/` 下另有 `SettingsWindow`（卡片结构：音频输出〔采样率/位深/缓冲时长〕→ 播放过渡〔9 键过渡设置：自动前进与手动切歌两档位、传送/进度两个开关与五只长度滑块（交叉/传送淡变/进度淡变/预加载/手动短交叉），键组 `transition` 持久化并经真实 `SetTransitionConfig` 命令下发〕→ 设备与系统〔输出设备下拉/日志等级〕→ 歌词分隔符；UI 仅 Mixed 输出模式，采样率/位深/过渡设置恒可用）、`TrackDetailWindow`（曲目详情：年份/播放次数/星级）、`EqualizerWindow`（四段布局：工具行〔10/31 档切换、总开关、复位〕、图谱区〔`SpectrumGraph` 自绘 QSG 渲染：120 桶频谱柱（输入 bins 为 dBFS，0dBFS 满刻度、−120 静音地板；**显示域取 −60..0dBFS 线性映射到柱高 [0,1]**，≤−60 与 −120 地板同归 0、≥0 钳 1，随频谱开关显隐）+ 峰值保持线 + 181 点频响曲线（本地 PCHIP 包络——手柄点单调插值，恒过手柄不过冲；与 DSP 物理响应解耦，消费级 GEQ 惯例，R5）+ 10/31 档 band 编辑手柄；刻度为 QML 静态覆盖层：左缘 dBFS 0/−20/−40/−60 标签与每 20dB 网格、底部对数频率标签，几何全部经 SpectrumGraph 映射函数定位（QML 侧无轴公式）；空态三态提示：活动档增益缺省→播放引导兜底 / 频谱开关关→「频谱已关闭」 / 开关开但桶数据未到→「暂无频谱数据」〕、控制条〔限幅器、预设快捷应用、保存、管理〕、GEQ 竖条区〔pre-gain 固定 + 横滚 band 行〕；均衡器组〔键组 `equalizer`，7 用户键 + 8 内置只读/用户预设 CRUD〕经 SettingsController 持久化并经 `BackendBridge::submitEqualizerConfig` 下发 `SetEqualizerConfig`；频谱显示链路已接通（R2/R3）：频谱开关为组内离散开关、点击即经独立真命令 `SetSpectrumEnabled` 即时外发（缓存去重防同值重发，不经 50ms EQ 滑块去抖），120 桶快照按后端分析发布频率（~10ms 轮询节流、上界 ~40Hz 级@44.1/48k 的 `SpectrumUpdated` 事件）经订阅镜像 bins 驱动柱区律动，空态仅在开关关/无数据时呈现；采样率仍不镜像、fs<40k 截断标注未实现（镜像面未存 sampleRate——settings.sampleRate 是输出目标率，后端已把不可测桶置 −120 dB 地板），配套组件 `EqBandSlider`/`EqualizerPresetDialog`）。
- **Theme.qml**：singleton token（颜色/尺寸/动画时长；`animationDuration` 150ms、`colorTransitionDuration` 500ms 等）。注意：`Theme.sidebarWidth` 与 Main.qml 内同名常量并存（两者均 350，Main.qml 的停靠判定用自身常量）；`Theme.gradientColor0` 仅 `AboutOverlay.qml` 使用、`gradientColor1/2` 未见使用点（`DynamicBackground` 实际消费 `PlaybackController.gradientColor0/1/2`）。

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

BackendBridge::domainNotificationQueued ──►（FolderSortRulesApplied → LibraryController.applyFolderSortSetting；ConfigureOutput 拒绝 → SettingsController.rollbackRejectedOutputConfig）──► NotificationController.enqueueDomainNotification

BackendBridge::equalizerStateChanged ──► AppFacade::handleEqualizerStateChanged ──► SettingsController.mirrorEqualizerCurve
BackendBridge::spectrumChanged       ──► AppFacade::handleSpectrumChanged       ──► SettingsController.mirrorSpectrumBins
```

### 命令流（UI → 后端）

```
QML 控件 ──► PlaybackController setter/Q_INVOKABLE ──► submitCommand(MediaControlCommand)
         ──► AppFacade 注入的 executor（= BackendBridge.submitCommand）──► MediaController
QML 曲库 ──► LibraryController.playItem/scanLibrary/applySortRules ──► submitCommand / scanLibrary / applyFolderSortRules
```

播放/曲库控制器的命令执行结果只用于判定成败（不落地为前端状态）；被拒命令由 `BackendBridge` 转成 `CommandRejected` 域通知回流。删除/入队/移除队列三个曲目命令入口（`AppFacade` 的 Q_INVOKABLE）额外消费 `accepted` 做本地 toast。

### 关闭链路

```
WindowControls.closeRequested / 设置菜单"退出" ──► requestApplicationClose ──► window.close()
──► onClosing ──► appFacade.shutdown()（幂等：取消波形 → BackendBridge.shutdown → 提交 Stop 命令 → 关闭控制器）
```

## 7. 启动流程

1. `main.cpp`：构造 `QApplication` 并设置应用图标（多档 `QIcon`）、`setDesktopFileName`、`setApplicationVersion`；接入后端时初始化后端日志（spdlog + Qt 消息重定向，FFmpeg av_log 级别按 NDEBUG 调整）。
2. 解析 `--smoke-*` 参数（见 §9.3）：未知 smoke 选项或非法 `--smoke-exit-ms` 直接退出 2；smoke 启用时校验场景名（未知同样退出 2）、把 `seriona.backendBridgeAutostartEnabled` 置 false、写 smoke 日志（失败退出 3）并挂定时退出。
3. 非 smoke 且未设 `SERIONA_DISABLE_SINGLE_INSTANCE` 时创建 `SingleInstanceGuard`（`src/app/single_instance_guard.{h,cpp}`，QLockFile + 用户级本地 socket）：非主实例把激活请求（含 Wayland 激活令牌）转交已运行实例后直接退出 0，主实例收到激活后恢复最小化 / `raise()` / `requestActivate()`。
4. 创建 `QQmlApplicationEngine`，注入 QML 初始属性 `smokeScenario`（正常模式为空串）与 `smokeLoggingEnabled`（仅非 Release 且 smoke 启用时为 true）。
5. `loadFromModule("Seriona", "Main")` → Main.qml 实例化 `AppFacade`。
6. AppFacade 构造：注入执行器、连接快照信号、`start()` 后端桥（默认自动；smoke 模式已在第 2 步置 false）。
7. Main.qml `Component.onCompleted`：应用 smoke 场景（若有）；正常模式默认显示启动页（`startupScreenVisible=true`）。
8. 启动页用户动作：恢复播放列表（`AppFacade::restorePlaylistFromStartup` → 读应用设置中的上次根 → 扫描）或添加文件夹 → 进入主界面（`enterMainShell`：隐藏启动页、切到 playback 视图）。

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
| `BUILD_TESTING` | ON | mock-only 只注册 11 个测试二进制；其余 21 个要求后端目标 |
| `SERIONA_BACKEND_SOURCE_DIR` | `../Seriona_Backend` | 空= mock-only；存在=本地引入（或 `find_package(SerionaBackend CONFIG)` 命中则复用已安装产物）；不存在=FetchContent GitHub main |
| `SERIONA_INSTALLED_MODE` | OFF | Linux 安装模式：ON 走 XDG 数据目录、启用安装规则与 CPack deb/rpm；OFF 为便携版（数据在可执行文件旁） |
| `SERIONA_VERSION` | `0.1` | 版本号，CI 经 `-DSERIONA_VERSION` 注入；进 `setApplicationVersion` 与 macOS `CFBundleShortVersionString` |
| `CMAKE_BUILD_TYPE` | — | Release 启用 LTO（由 `release` 预设提供）；`-march=x86-64`（GNU/Clang + x86_64 基线，替代 `-march=native`）不限构建类型恒生效 |
| `SERIONA_FETCHCONTENT_CATCH2_DIR` / `SERIONA_FETCHCONTENT_THREAD_POOL_DIR` | — | 离线 configure 注入后端子树的依赖源码（转发为 `FETCHCONTENT_SOURCE_DIR_CATCH2` / `_THREAD_POOL`）；前端自身已无 Catch2 FetchContent，Catch2 变量只在后端嵌入的 TagReader 走 FetchContent 路径时生效 |

`SERIONA_HAS_BACKEND` 由 CMake 生成器表达式推导（`$<BOOL:${SERIONA_BACKEND_CONTROL_TARGET}>`），不手动设置。

后端引入有三段优先级，且**没有静默回退**：① 非空时先 `find_package(SerionaBackend CONFIG QUIET)`，命中已安装产物即直接链接 `seriona_control/audio/app` 三个目标（CI 顺序链；不引入源码子树）；② 未命中且本地源码存在则 `add_subdirectory` 嵌入，同时强制关闭后端自身的 app 与测试目标、再恢复前端自身的测试开关（因此 ctest 中不会出现后端测试）；③ 本地源码不存在则经 FetchContent 抓取 GitHub `main` 分支，**抓取失败即配置失败**，不会退化为 mock-only。只有显式把 `SERIONA_BACKEND_SOURCE_DIR` 设为空才进入 mock-only。

### 9.2 运行期

- 应用设置存储：三个控制器（SettingsController / NavigationController / TrackStatsController）共用；默认内存存储（进程内，mock-only/smoke 有效）；AppFacade 接入后端时注入 BackendBridge（命令/快照边界）→ 后端键值存储（`app_settings` 表）。写入一律先落内存缓存再尝试后端；读取时若后端不可用（返回空）即回退到该内存缓存——后端缺失因此不会让设置面出现空洞。
- QCoreApplication 动态属性：`seriona.backendBridgeAutostartEnabled`（false 时跳过 `BackendBridge::start()`，默认 true）。
- QML 初始属性（`QQmlApplicationEngine::setInitialProperties`）：`smokeScenario`（smoke 场景名，见 §9.3；正常模式为空串）、`smokeLoggingEnabled`（仅非 Release 且 smoke 启用为 true）。
- 环境变量：`SERIONA_DISABLE_SINGLE_INSTANCE=1` 跳过 `SingleInstanceGuard`（smoke 模式本身亦旁路该守卫；门禁/自动化需要独立进程行为，`verify-middle-layer.sh` 的 offscreen 冒烟即使用它）。
- `Theme.qml` token：共享颜色/尺寸/动画参数。

### 9.3 Smoke CLI（`./build/seriona`）

```
--smoke-scenario=<startup|main-playback|lyrics|sidebar-tree|settings-menu|empty-library>
--smoke-exit-ms=<ms>          # 默认 1000
--smoke-output-dir=<dir>      # 默认 .omo/evidence/smoke
```

- 启用时禁后端自启、写入 `smoke-<scenario>.log`（scenario/exit_ms/timestamp_utc/artifact 四行）后定时退出；`smokeScenario` 经初始属性传入 QML，由 `applySmokeScenario` 驱动各场景视图动作（`smokeVisualStateJson()` 聚合窗口/曲库/播放文本供后续扩展）。
- 退出码：0 正常；2 参数/场景非法；3 日志写入失败；-1 QML 创建失败。
- `verify-middle-layer.sh` 的 offscreen 冒烟：`QT_QPA_PLATFORM=offscreen SERIONA_DISABLE_SINGLE_INSTANCE=1 timeout 5s ./build/seriona` 必须退出码 124（旁路单实例守卫，保证被测进程独立启动）。

## 10. 测试体系

- 位置：`tests/frontend/adapter/`（32 个 `tst_*.cpp`，QTest）。所有测试目标与 CTest 注册都写在顶层 `CMakeLists.txt`（`tests/` 下**没有** `CMakeLists.txt`），测试名统一 `seriona_frontend_` 前缀，共 186 条 CTest。
- mock-only 注册 11 个测试二进制：`seriona_frontend_command_result_mapping`、`seriona_frontend_snapshot_mapping`、`seriona_frontend_library_tree_mapping`、`seriona_frontend_settings_controller_tests`、`seriona_frontend_spectrum_display_model_tests`、`seriona_frontend_track_stats_tests`、`seriona_frontend_about_overlay_tests`、`seriona_frontend_single_instance_tests`、`seriona_frontend_queue_view_tests`、`seriona_frontend_row_diff_tests`（CTest 名 `seriona_frontend_row_diff`）、`seriona_frontend_app_facade_smoke_mode`（无后端时映射测试降级为占位用例，facade 用例 QSKIP）。另有 `seriona_frontend_smoke_startup` 直接以产品二进制跑 startup 场景（offscreen），是唯一的 QML 级 CTest 验证。
- 后端模式另注册 21 个二进制（强制 `SERIONA_HAS_BACKEND=1`），覆盖：桥接线程/关闭、库模型投影、文件夹投影/排序、双游标、侧栏浏览/队列切换、选曲上下文、扫描流程、启动恢复、播放快照/命令、通知、波形、当前曲目、歌词、UI-only 策略、文件夹页面、曲目详情、封面迁移（唯一加载真实 `MainContent.qml` 的集成测试，6000 行）。
- 测试缝（生产代码中为测试开出的接口）：`setScanExecutor`、`setPlaylistTreeSnapshot`、`applyPlayerStateSnapshot`、`BackendBridge::ControllerFactory`/`drainForTests`、`WaveformProvider::setGeneratorForTests`、AppFacade `*ForTests` 钩子、QCoreApplication 动态属性 `seriona.backendBridgeAutostartEnabled`。
- fake 后端范式：测试内实现后端纯虚接口（`AudioPlaybackService`/`FileScannerService`/`MetadataSharingService`/`FolderSortSettingsStore`）经 `ControllerHarness` 组装。
- 单用例运行：`./build/seriona_frontend_library_sort_tests titleAscendingAndDescendingSortCurrentFolderProjection`。
- `verify-middle-layer.sh`（依赖 `rg`）：configure → build → 不变量断言（源码/目标注册、禁 mock 文案、禁直接 IO、契约文档存在）→ offscreen smoke；`SERIONA_BUILD_DIR` 可覆盖构建目录。

## 11. 扩展方式

1. **新设置项**：① 前端本地项（如歌词分隔符）：加真实绑定/写回即可；② 未实现项（如有）：必须 `showUnsupportedAction()` 本地反馈，禁止伪造命令；③ 需后端支持（范例：播放过渡 9 键组 → 后端 `SetTransitionConfig`，前端经 `SettingsController` executor + `BackendBridge::submitTransitionConfig` 接线，滑块 400ms 去抖、启动 apply 一次；均衡器组为同模式实例：键组 `equalizer` 经 `BackendBridge::submitEqualizerConfig` → 后端 `SetEqualizerConfig`，开关/档位/预设/复位即时、滑块 50ms 去抖、启动 apply 一次；频谱开关同组但命令面独立（R3 起离散即时外发 `SetSpectrumEnabled`，不经 EQ 去抖、缓存去重，不占 SetEqualizerConfig 载荷））：先在后端加命令与快照字段，前端经 `BackendBridge` 接线，并同步更新 `docs/architecture/backend-integration-contract.md`。
2. **新页面**：`NavigationController.currentView` 增加视图名 + MainContent 增加对应 State/Transition；或独立 QML 组件挂到 Main.qml 组件树。
3. **新图标**：SVG 放 `qml/assets/` 并追加到 CMake `SERIONA_QML_MODULE_RESOURCES`；引用用绝对 QRC 路径 `qrc:/qt/qml/Seriona/qml/assets/<name>.svg`。
4. **新 C++ 控制器/模型/工具**：放入 `src/app/`，追加到 CMake `SERIONA_APP_LAYER_SOURCES`（及测试目标清单）；QML 侧经 `AppFacade` 暴露，不要另建可创建的控制器实例。
5. **新测试**：`tests/frontend/adapter/` 新增 `tst_*.cpp`，在 CMakeLists 注册 `add_executable`/`add_test`（后端依赖的放 `if(SERIONA_BACKEND_CONTROL_TARGET)` 内），并把文件/目标追加到 `verify-middle-layer.sh` 的必需清单。
6. **对接/更新后端**：默认同级目录 `../Seriona_Backend`；版本由 `SERIONA_BACKEND_SOURCE_DIR` 指向的 checkout 决定，勿在仓库内写死路径。

## 12. 开发与维护建议

- **守契约**：任何改动后跑 `./scripts/verify-middle-layer.sh`；不要绕过中间层直接 IO，不要在 QML 放业务状态或 mock 文案。
- **CMake 同步**：新增/重命名任何 `src/app` 源、模块 QML、SVG、测试源都要同步 CMakeLists 三张清单；`.qmlls.ini` 的 buildDir 是绝对路径，换 worktree 后先修正。
- **mock-only 差异**：无后端时 PlaybackController 的用户意图入口基本全是空操作（例外：`totalDuration` 的 write setter 仍直接写 read model，见 §5.2）、WaveformProvider 类型不存在、大多数测试不注册——排查"UI 点了没反应"先确认构建模式。
- **双游标**：改曲库交互时保持播放身份（`playingTrackId`）与浏览焦点分离；"定位当前歌曲"只动浏览。
- **快照是事实来源**：前端状态一律由 `apply*` 从后端快照更新，UI 直写只用于提交意图；排序/扫描/播放的终态以后端回推为准。
- **`docs/` 的定位**：`docs/architecture/backend-integration-contract.md` 是现行跨端契约（§11.1 要求同步维护）；`docs/backend-integration-strategy.md` 与 `docs/folder-navigation-scroll-state.md` 是历史记录与背景材料，不作为事实来源，一切以后 CMakeLists.txt 与实际源码为准。

## 13. 跨平台约束（Windows / Linux / macOS）

- 三端（Windows / Linux / macOS）可配置、可构建、可运行是**硬约束**，不是发布前适配；三端产物形态为 Windows zip、Linux AppImage + deb + rpm、macOS app zip。
- 平台差异只允许出现在既有平台边界内：`scripts/` 的三平台脚本、CI 构建矩阵、以及 `src/main.cpp` 的应用图标与 `setDesktopFileName` 设置。**禁止**在 QML 或共享 C++ 中散落裸 `#ifdef _WIN32`、POSIX-only 或 Windows-only 调用；新增平台行为必须并入上述入口或建立等价抽象。
- 工具链不可替换：Windows 固定 MSVC（VS2022 x64 + vcpkg manifest，禁止 MinGW/msys2），Linux 用 GCC/Clang，macOS 用 Apple Clang + Homebrew。
- 引入新依赖前先确认三端均可供给（Windows 走 vcpkg manifest，Linux/macOS 走系统包或 Homebrew）；无法三端覆盖的能力必须经 CMake 条件关闭或 mock-only 降级，不得阻塞其它平台构建。
- 改动涉及平台行为时，至少按另一平台的文档化入口验证（offscreen smoke / 打包脚本 / CI job）；无法本机验证时在提交信息中说明受影响面与验证方式。
