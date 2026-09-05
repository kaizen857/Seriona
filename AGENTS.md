# Seriona 代理说明

所有面向用户的回复及本仓库内新写的文档均使用中文。以根目录 `CMakeLists.txt`、`CMakePresets.json`、`src/`、`qml/` 和 `scripts/` 为准；不要从 `build/qml-modules/qml/` 的生成或残留副本推断源码状态。

## 分支约束
- 本仓库后续所有更改必须且只能提交到 `develop` 分支；禁止直接在 `main` 分支提交。
- 修改、暂存或提交前运行 `git status --short --branch`，确认当前分支为 `develop`；发现处于 `main` 或其它分支时先切换到 `develop`。
- `main` 只作为稳定基线或同步来源；是否推送 `develop`、创建合并请求或合并回 `main`，必须由用户明确要求。

## 入口与边界
- 这是单可执行 Qt Quick/CMake 项目（要求 Qt 6.8+，`qt_standard_project_setup(REQUIRES 6.8)`）；`src/main.cpp` 通过 `engine.loadFromModule("Seriona", "Main")` 加载 `qml/Main.qml`。
- `qt_add_qml_module(...)` 注册本地 URI `Seriona`。新增或重命名 `src/app` C++、模块内 QML、JS（如 `qml/components/menuRegistry.js`）、SVG 或 `tests/frontend/adapter/` 测试源时，同步更新 `CMakeLists.txt` 的 `SERIONA_APP_LAYER_SOURCES`、`SERIONA_QML_MODULE_FILES`、`SERIONA_QML_MODULE_RESOURCES` 及对应 `add_executable(...)`；不要把入口或测试 C++ 塞进 QML 模块。
- `qml/Main.qml` 实例化唯一的 `AppFacade`。它拥有播放、曲库、歌词、通知和导航对象，并持有 `BackendBridge` 与 `WaveformProvider`（两者始终实例化；`SERIONA_HAS_BACKEND=0` 时不编译后端调用）；不要把这些状态重新放回 QML mock 属性。
- 扫描进度：`LibraryController` 暴露 `scannedSongCount`/`totalSongCount`（库扫描时更新）；`MainContent` 经自身 `scanRunning`/`scanToastTitle`/`scanToastMessage`/`scanToastProgress` 展示扫描进度 toast（扫描完成自动隐藏），`Sidebar`/`StartupView` 经 `scanMessage`。
- `PlaybackController`、`NavigationController`、`NotificationController`、`LibraryModel`、`SettingsController` 和 `TrackStatsController` 是 `QML_UNCREATABLE`；`LibraryController` 定义在 `library_model.h/.cpp`（没有独立文件），`LyricsModel` 同为 `QML_ELEMENT`。正式 QML 统一使用 `appFacade.playback/library/lyrics/notifications/navigation`，不要另建可创建的控制器或模型。

## 构建与验证
- 顺序是 `cmake -B build`、`cmake --build build`、`ctest --test-dir build --output-on-failure`；应用为 `./build/seriona`。
- `CMakePresets.json` 提供 `release` 预设（输出 `build/release`，LTO 经 `CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE=ON` 提供）；GNU/Clang 下编译选项统一 `-march=x86-64` 基线（替代 `-march=native`，产物可跨机器分发）。
- Windows 打包链（git 跟踪）：`build.bat` + `scripts/build-package-windows.ps1` + `scripts/verify-windows-package.ps1`（产出 `dist\logs`）；依赖经根目录 `vcpkg.json` manifest 提供（catch2/ffmpeg/libiconv/pkgconf/spdlog/sqlite3/xxhash，ffmpeg 开 zlib feature）。
- Linux 安装/卸载（git 跟踪）：`scripts/install-linux.sh` / `scripts/uninstall-linux.sh`，配套 desktop 启动器条目 `assets/org.kaizen857.Seriona.desktop.in` 与 `assets/hicolor/` 多尺寸图标；安装模式走 XDG 数据目录。应用窗口图标在 `src/main.cpp` 统一设置（多档 `QIcon` + `setDesktopFileName("org.kaizen857.Seriona")`，覆盖 Windows 运行时与 Linux X11/Wayland 任务栏）。
- `.clangd` 读取 `build/`，`.qmlls.ini` 则硬编码当前工作区 `build/` 的绝对路径；该文件被 `.gitignore` 忽略、未纳入 git 跟踪（`*.qmlls.ini`），全新 clone/worktree 需自行创建；移动目录或配置新构建目录后先修正该路径，再运行语言服务诊断。
- 聚焦一个 CTest：`ctest --test-dir build -R '^seriona_frontend_command_result_mapping$' --output-on-failure`。同一测试二进制中的单个 QTest case 直接作为参数传入，例如 `./build/seriona_frontend_library_sort_tests titleAscendingAndDescendingSortCurrentFolderProjection`（该目标依赖后端）。
- 前端测试全部是 C++ Qt Test（`tests/frontend/adapter/tst_*.cpp` + `Qt6::Test`），仓库没有 qmltestrunner / Qt Quick Test（`.qml` 单测）入口；QML 级验证只有 `--smoke-scenario` 场景 smoke。
- `./scripts/verify-middle-layer.sh`（依赖 `rg`）会配置、构建、检查中间层/CMake 不变量并运行 `QT_QPA_PLATFORM=offscreen timeout 5s ./build/seriona`，预期退出码为 `124`；它要求 `docs/architecture/backend-integration-contract.md` 存在，但不会运行 CTest。可用 `SERIONA_BUILD_DIR` 覆盖构建目录。它是必要子集门禁而非穷举：QML 与测试目标只查固定子集（新增文件不会自动被查），新增文件仍须手动同步 CMakeLists（`SERIONA_QML_MODULE_FILES`、各 test 目标源列表）。
- Smoke CLI：`./build/seriona --smoke-scenario=<name> --smoke-exit-ms=<ms>`；场景为 `startup`/`main-playback`/`lyrics`/`sidebar-tree`/`settings-menu`/`empty-library`。默认 1000 ms 后退出并写入 `.omo/evidence/smoke/smoke-<scenario>.log`；`--smoke-output-dir=<dir>` 可改目录。`Main.qml` 的 `smokeVisualStateJson()` 已定义但当前无调用方（为扩展预留），不要删除或自行接线。
- 仓库没有独立的 lint、format 或代码生成命令；QML/MOC/RCC 生成由 CMake/Qt 完成。`CMakeLists.txt` 显式 `set(CMAKE_CXX_STANDARD 23)` 并 `set(CMAKE_CXX_STANDARD_REQUIRED ON)`，C++23 是前端自身声明，不是由 Qt/后端传递。
- CI/CD：`.github/workflows/release.yml`——push 到 main 自动构建+测试+打包（产物存 Actions artifact，不发 release）；Actions 页 "Run workflow"（输入 version）为发版按钮：全量构建后自动创建 `v<version>` tag + GitHub Release 并挂载全部资产。产物矩阵：AppImage + tar.gz（ubuntu:22.04 容器，glibc 2.35 基线；QML 依赖经 `QML_SOURCES_PATHS=qml/` 注入 linuxdeploy-plugin-qt）、deb（debian:trixie / ubuntu:resolute 容器，`cpack -G DEB` + dpkg-shlibdeps 自动依赖）、rpm（fedora:43 + rpmfusion / rockylinux:10 + EPEL/CRB 容器，`cpack -G RPM` + `qt6-qtbase >= 6.8, qt6-qtdeclarative >= 6.8`；Rocky 用 AppStream 的 `ffmpeg-free-devel`，EPEL 10 无完整 ffmpeg；F43 起系统包才含 sdbus-c++ v2，故 rpm-fedora 用 43）、Windows zip（windows-2022 runner 复用 `build-package-windows.ps1`，自动探测 `Qt6_DIR`/`C:\vcpkg`/VS2022）、macOS app zip（macos-15 = Apple Silicon，Homebrew 依赖 + 官方 Qt clang_64，产物 `Seriona-<ver>-macos-arm64.zip`，未签名未公证）。版本号经 `-DSERIONA_VERSION` 注入（`CMakeLists.txt` 默认 `0.1`）。
- A2 顺序链（Linux 与 macOS 构建 job 内三段，任一段失败即 job 失败、不产出）：① TagReader 独立 `cmake --preset release -G Ninja` 建测（161）→ `cmake --install build/release --prefix /opt/seriona-deps`（macOS 用 `$GITHUB_WORKSPACE/seriona-deps`）；② 后端 `find_package(TagReaderCore)` 复用① 建测（140）+ `-DSERIONA_BUILD_APP=OFF -DSERIONA_INSTALL_EXPORT=ON -DSERIONA_INSTALLED_MODE=ON`（宏须在后端②段开启：在 Seriona_Backend 编译期作用于 seriona_app/seriona，测试目标不携带；2026-09-05 修复前五个 Linux 系 job 漏传，产物只读数据根崩溃）→ install 前缀；③ 前端 `find_package(SerionaBackend)` 复用①② 建测（151）→ 打包 → 冒烟。三段各自 `working-directory` 仓库目录、preset binaryDir 固定 `build/release`；linux job 的 Qt（install-qt-action）在①②段之后才安装（仅前端需要）；linux 前端段显式 `-DCMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE=OFF`（ubuntu:22.04 容器 GCC 13 + Qt 官方二进制组合下开 LTO 会让前端 10 个用例段错误，关掉后 151 个全过；deb/rpm job 用发行版 GCC+Qt 开 LTO 正常，故仅此 job 关闭）。崩溃诊断仅失败轮：linux 用 gdb（4 个崩溃测试二进制，`cd build/release`、先确认构建目录/二进制存在（缺失显式提示）、不 grep 保留完整 stderr），macOS 用 lldb（`continue-on-error: true`，纯诊断步骤不制造额外失败标记）。windows job 走脚本（脚本内三段链见 `scripts/build-package-windows.ps1`）；macOS job 依赖走 Homebrew（ninja pkg-config ffmpeg spdlog xxhash）+ install-qt-action（clang_64 + qt5compat/qtshadertools），macdeployqt 后补 `libqoffscreen.dylib`、`scripts/bundle-macos-dylibs.py --strict` 收拢非 Qt 动态库（残留构建机绝对路径引用即失败）、ad-hoc codesign，产物经 offscreen smoke 后 `ditto` 打包 zip；deb/rpm 容器补 `dbus`、`dbus-run-session`、libxkbcommon 和 qtshadertools，后端 CTest 在独立 session bus 中运行。后端 MPRIS 用 sdbus-c++ v2 自由函数 API：ubuntu:22.04 的系统包为 v1，CI 在其容器 checkout 后源码安装 sdbus-c++ v2.1.0（header-only，装到 /usr）并加 `libsystemd-dev`；fedora:43 起 Fedora 系统包已含 v2（42 为 v1，故 rpm-fedora 容器用 43 免源码安装）；debian:trixie / ubuntu:resolute / EPEL 10 的系统包已是 v2。deb/rpm job 打包后各有"安装冒烟验证"步骤（dpkg/dnf 安装产物 → XDG 数据根断言 + media controller started 断言，防 CPack 依赖集与 INSTALLED_MODE 回归）；linux job 冒烟为真实后端启动（解包后整树 chmod 只读模拟 FUSE，不接 smoke 场景，timeout 124 + 日志断言）。deb/rpm 打包命令为 `cpack -G DEB|RPM --config build/release/CPackConfig.cmake`（仓库根运行不带 config 会报 project name not specified）。publish job `needs` 覆盖 linux/deb×2/rpm×2/windows/macos 全部 7 个产物 job。
- CPack 段位于 `CMakeLists.txt` 末尾（`UNIX AND NOT APPLE AND SERIONA_INSTALLED_MODE` 时启用）：包名 `seriona`、`CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON`、`CPACK_DEBIAN_PACKAGE_DEPENDS`（9 个 qml6-module-* 全量 QML 运行时：qtcore/qtqml/qtqml-models/qtquick/qtquick-controls/qtquick-layouts/qtquick-effects/qt5compat-graphicaleffects/qt-labs-platform；2026-09-05 修复前仅 graphicaleffects，裸机缺 QtCore/Controls.Basic 即 QML 拒绝加载）、`CPACK_RPM_PACKAGE_REQUIRES "qt6-qtbase >= 6.8, qt6-qtdeclarative >= 6.8, qt6-qt5compat"`（Fedora/RHEL 系不拆 QML 模块包，运行时全在 qt6-qtdeclarative）、文件名 `seriona-<ver>-<arch>`。
- `assets/org.kaizen857.Seriona.metainfo.xml`：AppStream 元数据（`oars-1.1` 内容评级、`developer id="io.github.kaizen857"`），Flathub/Discover 展示前置；`SERIONA_INSTALLED_MODE` 安装规则会装到 `share/metainfo/`。
- 接入后端时终端日志统一走 spdlog：Qt 消息（qDebug/QML `console.log`）重定向到 spdlog 默认 logger，`find_package(spdlog CONFIG REQUIRED)` 在配置期强制（与后端同源，mock-only 不要求）；smoke 调试日志仅 Debug 构建输出（`main.cpp` 注入 `smokeLoggingEnabled`，`NDEBUG` 下恒为 false），mock-only 下通知类日志不输出到终端。

## 中间层行为契约
- 前端不得直接实现文件系统/网络/数据库访问（`QDir::*`、`QFileSystem*`、`QNetwork*`、`QSql*` 等）；一切经 `BackendBridge` 的命令/快照边界，`verify-middle-layer.sh` 会对 `src/` 和 `qml/` 强制检查。
- 与后端边界交换的路径文本一律 UTF-8：路径文本必须经 `src/app/path_text.h` 的 `pathTextUtf8` 处理，禁止直接 `std::filesystem::path::string()/generic_string()`（Windows 下按 ANSI 代码页转换，非 ASCII 路径会抛异常或乱码）。
- 未实现的设置项（均衡器等）必须走 `NotificationController::showUnsupportedAction()` 本地反馈，禁止伪造后端命令或静默吞掉；`Exit` 是例外，必须走真实关闭链路。播放过渡组（键组 `transition`，9 键：交叉淡入淡出/预加载/传送与进度淡变/手动短交叉）已接真实 `SetTransitionConfig` 命令链路（SettingsController → BackendBridge，含 400ms 滑块去抖与启动一次 apply），不属本地反馈项。均衡器（Equalizer）仍未实现：`qml/windows/EqualizerWindow.qml` 只是"开发中"占位窗口（已注册进 QML 模块，Main.qml 实例化并经 `onOpenEqualizerRequested` 打开，MainContent 的 `equalizerMenuItem` 触发，含 smoke 覆盖），内容区仅显示"均衡器（开发中）"文本、无任何均衡器操作，不要把它当已实现功能或伪造均衡器命令。
- 播放游标与浏览游标分离：`playingTrackId` 与 `selectedBrowserNodeId` 各自独立，浏览/定位不得反向污染播放身份。
- 详细契约见 `docs/architecture/backend-integration-contract.md`（verify 脚本强制要求存在）与 `docs/backend-integration-strategy.md`。

## DESIGN.md 维护规范
- `DESIGN.md` 是描述项目长期稳定设计的架构文档，面向新开发者理解整体设计；它不是开发日志、变更记录或实现细节文档。它只是描述性文档，若与源码冲突，以源码为准，并按本节标准判断是否更新。
- 完成任何开发任务时，必须把"本次修改是否导致 `DESIGN.md` 已无法准确描述当前项目"列为检查项：若会，必须在同一次任务内同步更新；若不会，则不修改，也不为"保持同步"做无意义更新。确认无需更新时，不必在 `DESIGN.md` 中留下任何痕迹。
- 判断是否需要更新的唯一依据：新开发者只读旧版 `DESIGN.md` 是否会错误理解当前项目。与是否修改了代码、是否修改了架构名称无关。
- 通常不应更新（包括但不限于）：Bug 修复、代码重构、性能优化、实现细节调整、内部接口调整、参数修改、不影响整体设计的小功能开发、代码风格调整、测试补充。
- 通常应当更新（包括但不限于）：整体架构调整、模块新增或删除、模块职责变化、模块协作关系变化、启动流程变化、核心运行流程变化、配置体系变化、扩展机制变化、长期维护方式变化，以及其他会影响开发者理解项目整体设计的重要修改。
- `DESIGN.md` 应保持稳定：仅在长期设计变化时更新，避免随项目开发逐渐演变为实现文档或变更日志，不给开发增加不必要的维护负担。

## 后端集成（可选）
- `SERIONA_BACKEND_SOURCE_DIR` 默认是相对仓库根目录的 `../Seriona_Backend`；设为 `""` 才会强制 mock-only。非空时优先 `find_package(SerionaBackend CONFIG QUIET)`（CI 顺序链复用已安装产物：命中则链接 `SerionaBackend::seriona_control/audio/app`、要求 `find_package(spdlog CONFIG REQUIRED)` 并跳过 add_subdirectory）；未命中走源码嵌入链，路径不存在时从 `https://github.com/kaizen857/Seriona_Backend.git` 的 `main` 分支 FetchContent，抓取失败会配置失败，不会自动回退。
- 接入后端时，前端链接后端的 control/audio/app 目标并定义 `SERIONA_HAS_BACKEND=1`；CMake 会关闭后端子树自身的 app/tests，再恢复前端的 `BUILD_TESTING`。
- `BUILD_TESTING` 默认为 `ON`；启用测试时，mock-only 只注册纯 QML 面的 8 个测试（`seriona_frontend_command_result_mapping`、`seriona_frontend_snapshot_mapping`、`seriona_frontend_library_tree_mapping`、`seriona_frontend_settings_controller_tests`、`seriona_frontend_track_stats_tests`、`seriona_frontend_about_overlay_tests`、`seriona_frontend_queue_view_tests`、`seriona_frontend_app_facade_smoke_mode`），其余前端测试都要求后端目标。
- 离线运行验证脚本时，可用 `SERIONA_FETCHCONTENT_CATCH2_DIR` 和 `SERIONA_FETCHCONTENT_THREAD_POOL_DIR` 指向已有依赖源码。注意：前端 `CMakeLists.txt` 已无 Catch2 FetchContent（catch2 由后端嵌入的 TagReader 子树负责：`TAGREADER_USE_SYSTEM_CATCH2` 优先 find_package，缺失才 FetchContent），`CATCH2` 变量只在 TagReader 走 FetchContent 路径时生效；`THREAD_POOL` 变量仍有效（后端 FetchContent 拉取）。

## QML 与资源
- `qml/theme/Theme.qml` 注册为 singleton；共享颜色、尺寸和动画参数复用 `Theme.*`。
- SVG 资源使用绝对 QRC 路径，例如 `qrc:/qt/qml/Seriona/qml/assets/play.svg`；新增图标沿用该模式。
- `qml/Main.qml` 是无边框窗口；标题栏、遮罩、边框改动必须保住 `window.startSystemMove()` 拖拽和 `window.startSystemResize(...)` 八向缩放。
- 项目以 `Qt5Compat.GraphicalEffects` 为主（Main/MainContent/DynamicBackground/Sidebar/StyleButton/QueueView/PlaylistDelegate/TrackDetailWindow 实际使用）；`QtQuick.Effects` 仅 MainContent.qml 导入、当前无 MultiEffect 用法。改图形效果前先看当前文件依赖哪套。
