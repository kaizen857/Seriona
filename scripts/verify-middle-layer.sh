#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"
: "${SERIONA_BACKEND_SOURCE_DIR:=../Seriona_Backend}"
# 构建目录可覆盖；未设置时等价于历史硬编码的 build
: "${SERIONA_BUILD_DIR:=build}"
# 离线配置可选参数：新构建目录缺网时通过环境变量注入 FetchContent 源码目录，
# 未设置时这些参数完全不出现在 configure 命令行上（与改动前字节等价）
: "${SERIONA_FETCHCONTENT_CATCH2_DIR:=}"
: "${SERIONA_FETCHCONTENT_THREAD_POOL_DIR:=}"

log() {
    printf '\n==> %s\n' "$1"
}

assert_no_match() {
    local description="$1"
    local pattern="$2"
    shift 2

    log "$description"
    if rg -n "$pattern" "$@"; then
        printf 'Invariant failed: %s\n' "$description" >&2
        return 1
    fi
}

assert_match() {
    local description="$1"
    local pattern="$2"
    shift 2

    log "$description"
    rg -n "$pattern" "$@" >/dev/null
}

assert_fixed_match() {
    local description="$1"
    local pattern="$2"
    shift 2

    log "$description"
    rg -n -F "$pattern" "$@" >/dev/null
}

assert_path_exists() {
    local description="$1"
    local path="$2"

    log "$description"
    if [[ ! -e "$path" ]]; then
        printf 'Invariant failed: missing path %s\n' "$path" >&2
        return 1
    fi
}

assert_registered_paths() {
    local group="$1"
    shift

    local path
    for path in "$@"; do
        assert_path_exists "${group} path exists: ${path}" "$path"
        assert_fixed_match "CMake registers ${group} path: ${path}" "$path" CMakeLists.txt
    done
}

required_app_layer_sources=(
    src/app/app_facade.cpp
    src/app/app_facade.h
    src/app/playback_controller.cpp
    src/app/playback_controller.h
    src/app/navigation_controller.cpp
    src/app/navigation_controller.h
    src/app/settings_controller.cpp
    src/app/settings_controller.h
    src/app/app_settings_storage.cpp
    src/app/app_settings_storage.h
    src/app/backend_snapshot_mapper.cpp
    src/app/backend_snapshot_mapper.h
    src/app/backend_command_adapter.cpp
    src/app/backend_command_adapter.h
    src/app/backend_bridge.cpp
    src/app/backend_bridge.h
    src/app/notification_controller.cpp
    src/app/notification_controller.h
    src/app/waveform_provider.cpp
    src/app/waveform_provider.h
    src/app/lyrics_model.cpp
    src/app/lyrics_model.h
    src/app/library_model.cpp
    src/app/library_model.h
    src/app/library_tree_store.cpp
    src/app/library_tree_store.h
)

required_qml_module_files=(
    qml/Main.qml
    qml/theme/Theme.qml
    qml/components/BubbleMenu.qml
    qml/components/BubbleMenuItem.qml
    qml/components/BubbleSubMenuItem.qml
    qml/components/StyleButton.qml
    qml/components/Sidebar.qml
    qml/components/WindowControls.qml
    qml/components/WaveformProgressBar.qml
    qml/components/MarqueeText.qml
    qml/views/MainContent.qml
    qml/views/StartupView.qml
)

required_qml_resources=(
    qml/assets/play.svg
    qml/assets/pause.svg
    qml/assets/prev.svg
    qml/assets/next.svg
    qml/assets/shuffle_on.svg
    qml/assets/shuffle_off.svg
    qml/assets/repeat_list.svg
    qml/assets/repeat_one.svg
    qml/assets/repeat_off.svg
    qml/assets/playlist.svg
    qml/assets/settings.svg
    qml/assets/minimize.svg
    qml/assets/maximize.svg
    qml/assets/restore.svg
    qml/assets/close.svg
    qml/assets/volume_down.svg
    qml/assets/volume_up.svg
    qml/assets/back.svg
    qml/assets/arrow_back.svg
    qml/assets/search.svg
    qml/assets/more_vert.svg
    qml/assets/folder.svg
    qml/assets/music_note.svg
    qml/assets/my_location.svg
    qml/assets/translate.svg
)

required_frontend_test_sources=(
    tests/frontend/adapter/tst_app_facade_smoke_mode.cpp
    tests/frontend/adapter/tst_artwork_transition.cpp
    tests/frontend/adapter/tst_backend_bridge.cpp
    tests/frontend/adapter/tst_command_result_mapping.cpp
    tests/frontend/adapter/tst_current_track_lookup.cpp
    tests/frontend/adapter/tst_frontend_notifications.cpp
    tests/frontend/adapter/tst_library_dual_cursor.cpp
    tests/frontend/adapter/tst_library_scan_flow.cpp
    tests/frontend/adapter/tst_library_select_track.cpp
    tests/frontend/adapter/tst_library_tree_mapping.cpp
    tests/frontend/adapter/tst_library_tree_model.cpp
    tests/frontend/adapter/tst_lyrics_model.cpp
    tests/frontend/adapter/tst_playback_command_mapping.cpp
    tests/frontend/adapter/tst_playback_snapshot_mapping.cpp
    tests/frontend/adapter/tst_sidebar_local_browsing.cpp
    tests/frontend/adapter/tst_snapshot_mapping.cpp
    tests/frontend/adapter/tst_settings_controller.cpp
    tests/frontend/adapter/tst_startup_restore.cpp
    tests/frontend/adapter/tst_ui_only_handler_policy.cpp
    tests/frontend/adapter/tst_waveform_worker.cpp
)

required_frontend_test_targets=(
    seriona_frontend_app_facade_smoke_mode
    seriona_frontend_artwork_transition_tests
    seriona_frontend_backend_bridge_tests
    seriona_frontend_command_result_mapping
    seriona_frontend_current_track_lookup_tests
    seriona_frontend_library_dual_cursor_tests
    seriona_frontend_library_scan_flow_tests
    seriona_frontend_library_select_track_tests
    seriona_frontend_library_tree_mapping
    seriona_frontend_library_tree_model_tests
    seriona_frontend_lyrics_model_tests
    seriona_frontend_notifications_tests
    seriona_frontend_playback_command_tests
    seriona_frontend_playback_snapshot_tests
    seriona_frontend_sidebar_local_browsing_tests
    seriona_frontend_snapshot_mapping
    seriona_frontend_settings_controller_tests
    seriona_frontend_startup_restore_tests
    seriona_frontend_ui_only_handler_policy_tests
    seriona_frontend_waveform_worker_tests
)

log "Configure"
configure_args=(
    -B "$SERIONA_BUILD_DIR"
    -DSERIONA_BACKEND_SOURCE_DIR="$SERIONA_BACKEND_SOURCE_DIR"
)
if [[ -n "$SERIONA_FETCHCONTENT_CATCH2_DIR" ]]; then
    configure_args+=(-DFETCHCONTENT_SOURCE_DIR_CATCH2="$SERIONA_FETCHCONTENT_CATCH2_DIR")
fi
if [[ -n "$SERIONA_FETCHCONTENT_THREAD_POOL_DIR" ]]; then
    configure_args+=(-DFETCHCONTENT_SOURCE_DIR_THREAD_POOL="$SERIONA_FETCHCONTENT_THREAD_POOL_DIR")
fi
cmake "${configure_args[@]}"

log "Build"
cmake --build "$SERIONA_BUILD_DIR"

assert_match \
    "QML module URI remains Seriona" \
    'URI[[:space:]]+Seriona' \
    CMakeLists.txt

assert_fixed_match \
    "Theme.qml remains registered as QML singleton" \
    'set_source_files_properties(qml/theme/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)' \
    CMakeLists.txt

assert_registered_paths "app-layer source" "${required_app_layer_sources[@]}"
assert_registered_paths "QML module file" "${required_qml_module_files[@]}"
assert_registered_paths "QML resource" "${required_qml_resources[@]}"
assert_registered_paths "frontend adapter test source" "${required_frontend_test_sources[@]}"

for test_target in "${required_frontend_test_targets[@]}"; do
    assert_fixed_match "CMake defines frontend adapter target: ${test_target}" "add_executable(${test_target}" CMakeLists.txt
done

assert_no_match \
    "No orphaned legacy app-layer source references in CMake" \
    'src/app/(snapshot_mapper|library_browser_model)\.(cpp|h)' \
    CMakeLists.txt

assert_no_match \
    "No root QML navigation business state" \
    'property (string|bool) (currentView|isSidebarOpen|showStartupScreen)' \
    qml/Main.qml

assert_no_match \
    "No MainContent QML playback/song/lyrics mock ownership" \
    'property (bool|real|int|string|var) (isPlaying|currentPosition|totalDuration|volume|isShuffle|repeatMode|songTitle|artistName|albumName|waveformHeights)|lyricsData|Song Title|Artist Name|Album Name|Seriona shines|Music playing in the night' \
    qml/views/MainContent.qml

assert_no_match \
    "No Sidebar QML library/search mock ownership" \
    'ListModel|mockModel|mockSubModel|property string searchQuery|property var pageModel|Stairway to Heaven|Bohemian Rhapsody|Hi-Res Collection|Jazz Essentials' \
    qml/components/Sidebar.qml

assert_no_match \
    "No production library mock strings in app/QML paths" \
    'Stairway to Heaven|Bohemian Rhapsody|Hi-Res Collection|Jazz Essentials|rootEntries|childEntries|mockFileEntry|mockFolderEntry' \
    src/app qml

assert_no_match \
    "No backend/network/database/filesystem/persistence implementation" \
    'QDir::entryList|QDir::mkdir|QDir::mkpath|QDir::rmdir|QDir::removeRecursively|QDir::setCurrent|QFileSystem|QNetwork|QSql|QTcp|QUdp|HTTP|database|SQLite|filesystem scan|persistence|persistent' \
    src qml

assert_match \
    "Backend handoff still references middle-layer owners" \
    'PlaybackController|LyricsModel|LibraryController|NavigationController' \
    docs/architecture/backend-integration-contract.md

if [[ -n "${VERIFY_MIDDLE_LAYER_EXTRA_FORBIDDEN_PATTERN:-}" ]]; then
    assert_no_match \
        "Extra injected forbidden pattern" \
        "$VERIFY_MIDDLE_LAYER_EXTRA_FORBIDDEN_PATTERN" \
        src qml docs/architecture
fi

log "Offscreen startup smoke"
set +e
# SERIONA_DISABLE_SINGLE_INSTANCE：门禁要求被测进程独立启动——开发者若已运行 Seriona，
# 单实例守卫会转发激活并立即以 0 退出，破坏下方 124 断言。
QT_QPA_PLATFORM=offscreen SERIONA_DISABLE_SINGLE_INSTANCE=1 timeout 5s "./$SERIONA_BUILD_DIR/seriona"
smoke_status=$?
set -e

if [[ "$smoke_status" -ne 124 ]]; then
    printf 'Expected offscreen smoke to exit with timeout code 124, got %s\n' "$smoke_status" >&2
    exit 1
fi

log "Middle-layer verification passed"
