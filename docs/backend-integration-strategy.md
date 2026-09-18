# 后端整合说明

## 现状

Seriona 现在是单可执行 Qt Quick 应用，入口是 `src/main.cpp`，通过 `engine.loadFromModule("Seriona", "Main")` 加载 QML。前端不是直接摸后端内部对象，而是通过 `AppFacade`、`PlaybackController`、`LibraryController`、`LibraryModel`、`NotificationController` 这层 Qt/C++ seam 读写状态。

## 真实 CMake 选项

前端根 `CMakeLists.txt` 里真实存在的后端开关只有一个：

```cmake
set(SERIONA_BACKEND_SOURCE_DIR "../Seriona_Backend" CACHE PATH "Seriona backend source tree")
```

它是相对 `Seriona` 仓库根目录解析的，不要在已提交文档里写个人绝对路径。当前逻辑是：

- 为空时，`Seriona` 走 mock-only mode（**这是进入 mock-only 的唯一方式**）
- 非空时先 `find_package(SerionaBackend CONFIG QUIET)`：命中已安装产物（存在 `SerionaBackend::seriona_control`）就直接复用，不再 `add_subdirectory`，并要求 `find_package(spdlog CONFIG REQUIRED)`
- 未命中且路径存在且有 `CMakeLists.txt` 时，前端 `add_subdirectory(...)` 引入后端
- 路径不存在时，经 `FetchContent_Populate` 抓取 `https://github.com/kaizen857/Seriona_Backend.git` 的 `main` 分支；**抓取失败即配置失败，不会静默退化为 mock-only**
- 嵌入源码子树时 `SERIONA_BUILD_APP` 与 `SERIONA_BUILD_TESTS` 都会被强制设为 `OFF`（随后恢复前端自身的 `BUILD_TESTING`），避免把后端独立 app 与后端测试一起拉进来
- 前端最终链接后端三个目标：控制面、音频面与应用面，分别落在 `SERIONA_BACKEND_CONTROL_TARGET`、`SERIONA_BACKEND_AUDIO_TARGET` 与 `SERIONA_BACKEND_APP_TARGET`

## 相对路径布局

当前可复现的开发布局是同级 checkout，前端从自己的仓库根目录去找后端 checkout。文档里只写相对形式，不写机器路径。

如果本机布局不同，用命令行覆盖 `SERIONA_BACKEND_SOURCE_DIR`，不要改成写死路径的仓库配置。

## 后端 API 白名单和使用 seam

前端现在只消费这些真实 seam：

- `BackendBridge::start()`, `shutdown()`, `submitCommand()`, `scanLibrary()`
- `BackendBridge::playerSnapshot()`, `librarySnapshot()`, `notifications()`
- `AppFacade` 把 `BackendBridge::playerSnapshotChanged` 和 `librarySnapshotChanged` 投影到 `PlaybackController` / `LibraryController`
- `PlaybackController::setCommandExecutor()` 和 `LibraryController::setCommandExecutor()` 都只接 `BackendBridge::submitCommand()`
- `LibraryController::setScanExecutor()` 只接 `BackendBridge::scanLibrary()`

白名单之外的后端内部服务不应被前端直接碰。已确认的工具型例外只有波形生成这类独立能力，其他控制面仍以 `MediaController` 快照和命令为边界。

## 双游标播放和浏览规则

播放游标和浏览游标是分开的，不能再合成一个状态。

- `playingTrackId` 只表示当前播放曲目
- `selectedBrowserNodeId` 只表示当前浏览焦点
- `setSelectedBrowserNodeId()` 会同步焦点，但不会自动替换播放身份
- `locateCurrentSong()` 只根据 `playingTrackId` 找节点，然后本地改浏览选择和滚动请求，不发播放命令
- `followCurrentlyPlaying` 只影响浏览游标是否跟随播放，不改变后端播放身份
- `setPlaylistTreeSnapshot()` 会保留 `rootNodeId` 优先级；找不到 root 时先回退到“无父节点集合”，该集合为空再回退到 snapshot 的原始节点顺序，仍为空才保持空树策略

这条规则的核心是，浏览可以追随播放，但播放不能被浏览反向污染。

## 不支持和 UI only 政策

当前不支持的设置项必须走本地反馈，不得伪造后端命令，也不得静默吞掉。

当前生产代码里 `showUnsupportedAction()` 的实际调用点是：`AppFacade` 在 mock-only 下对删除 / 下一首播放 / 从队列移除三条命令的本地兜底，以及 `MainContent.qml`、`Sidebar.qml` 各自保留的 `showUnsupportedFeedback()` 本地反馈通道（该通道由 ui-only policy 测试锚定，供后续 UI-only 项使用）。`Equalizer`、`About Seriona`、`Crossfade`、`Gapless Playback` 均已接真实界面或真实命令链路，不属于本地反馈项；`ReplayGain` 与 `Sidebar` 的 `Sort by Name`/`Sort by Date` 在当前 `src/` 与 `qml/` 中不存在。

`Exit` 是例外，必须走真实关闭链路，继续触发 `requestApplicationClose()` 和 `AppFacade::shutdown()`。

政策很简单，UI only handler 不准调用 `submitCommand()`，unsupported 项必须走 `NotificationController::showUnsupportedAction()` 或等价本地通知，退出项不能被当成 unsupported。

## 已验证命令

- `cmake -B build-doc-missing -DSERIONA_BACKEND_SOURCE_DIR=../missing`：**不会退化为 mock-only**。该变量非空但路径不存在时会打印 `Seriona backend source not found at ...; fetching from https://github.com/kaizen857/Seriona_Backend.git` 并转到 FetchContent；抓取失败则配置失败（`FATAL_ERROR ... unless SERIONA_BACKEND_SOURCE_DIR is empty for mock-only mode`）。只有把 `SERIONA_BACKEND_SOURCE_DIR` 显式设为空才进入 mock-only。
- `cmake --build build-doc-missing --target Seriona`：该目录按上述行为并不是 mock-only 配置（非空路径已触发 FetchContent 拉取后端源码），前端目标是在真实后端配置下完成构建的；要得到 mock-only 配置必须改用 `-DSERIONA_BACKEND_SOURCE_DIR=`。
- 本机环境特有的验证记录（代理地址与个人 checkout 路径属当时机器环境，不构成文档化的默认值或推荐配置）：在启用本机 HTTP 代理后，以命令行指向本机后端的 checkout 完成配置，随后构建真实后端路径下的前端目标：`cmake -B build-doc-check -DSERIONA_BACKEND_SOURCE_DIR=<本地后端 checkout 的相对路径>` 与 `cmake --build build-doc-check --target Seriona`，两者均已成功。
- `git diff --check`：已通过，没有空白或补丁格式错误。

当前 T21 记录的验证状态均为成功；其中真实后端路径配置依赖代理启用后的网络环境（或本地后端 checkout），mock-only 则需显式传入空的 `SERIONA_BACKEND_SOURCE_DIR`。
