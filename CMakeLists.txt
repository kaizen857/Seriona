cmake_minimum_required(VERSION 3.16)

# 版本号由 CI 注入（-DSERIONA_VERSION=1.0.0）；本地默认 0.1
set(SERIONA_VERSION "0.1" CACHE STRING "Seriona release version (overridden by CI)")
project(Seriona VERSION ${SERIONA_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(QT_QML_GENERATE_QMLLS_INI ON)

include(FetchContent)

# Release 优化基线（三仓库统一方案）：LTO 由 CMakePresets.json 的
# CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE 提供（GCC -flto / MSVC /GL+/LTCG），
# 编译选项由 CMake 默认 Release 提供（GCC -O3 -DNDEBUG / MSVC /O2 /DNDEBUG）；
# 指令集显式 x86-64 基线（替代 -march=native，保证产物可跨机器分发）。
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"
   AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    add_compile_options(-march=x86-64)
endif()

# 多线程编译配置（充分利用 32 核 CPU）
if(CMAKE_GENERATOR MATCHES "Ninja")
    # Ninja 自动检测 CPU 核心数，无需额外配置
    message(STATUS "Using Ninja generator - parallel compilation enabled automatically")
elseif(CMAKE_GENERATOR MATCHES "Unix Makefiles")
    include(ProcessorCount)
    ProcessorCount(N)
    if(NOT N EQUAL 0)
        set(CMAKE_BUILD_PARALLEL_LEVEL ${N} CACHE STRING "Number of parallel build jobs")
        message(STATUS "Parallel build enabled: ${N} jobs")
    endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS Quick Concurrent QuickDialogs2 Widgets)

qt_standard_project_setup(REQUIRES 6.8)

option(BUILD_TESTING "Build tests" ON)

set(SERIONA_BACKEND_SOURCE_DIR "../Seriona_Backend" CACHE PATH "Seriona backend source tree")
set(SERIONA_BACKEND_CONTROL_TARGET "")
set(SERIONA_BACKEND_AUDIO_TARGET "")
set(SERIONA_BACKEND_BINARY_DIR "")

# Linux 安装模式：数据目录遵循 XDG Base Directory（由后端 SERIONA_INSTALLED_MODE 宏
# 驱动）；默认 OFF 保持便携版（数据在可执行文件旁 SerionaData）。Windows 恒为便携版。
option(SERIONA_INSTALLED_MODE "Linux installed build: use XDG data dirs" OFF)

if(SERIONA_BACKEND_SOURCE_DIR STREQUAL "")
    message(STATUS "SERIONA_BACKEND_SOURCE_DIR is empty; building Seriona in mock-only mode.")
else()
    # CI 顺序链：优先复用已安装的 SerionaBackend（TagReader/后端独立建测后 install
    # 到共享前缀，find_package 命中则不再重复编译）；未命中走源码嵌入链（本地开发）。
    find_package(SerionaBackend CONFIG QUIET)
    if(TARGET SerionaBackend::seriona_control)
        message(STATUS "Seriona frontend: reusing installed SerionaBackend via find_package")
        set(SERIONA_BACKEND_CONTROL_TARGET SerionaBackend::seriona_control)
        set(SERIONA_BACKEND_AUDIO_TARGET SerionaBackend::seriona_audio)
        set(SERIONA_BACKEND_APP_TARGET SerionaBackend::seriona_app)
        find_package(spdlog CONFIG REQUIRED)
        message(STATUS "Seriona frontend will link backend target: ${SERIONA_BACKEND_CONTROL_TARGET}")
    else()
        get_filename_component(SERIONA_BACKEND_ABSOLUTE_SOURCE_DIR
        "${SERIONA_BACKEND_SOURCE_DIR}"
        ABSOLUTE
        BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    if(EXISTS "${SERIONA_BACKEND_ABSOLUTE_SOURCE_DIR}/CMakeLists.txt")
        message(STATUS "Seriona backend source found: ${SERIONA_BACKEND_ABSOLUTE_SOURCE_DIR}")
        set(SERIONA_BACKEND_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/Seriona_Backend")
    else()
        message(STATUS "Seriona backend source not found at ${SERIONA_BACKEND_ABSOLUTE_SOURCE_DIR}; fetching from https://github.com/kaizen857/Seriona_Backend.git")
        FetchContent_Populate(seriona_backend
            GIT_REPOSITORY https://github.com/kaizen857/Seriona_Backend.git
            GIT_TAG main
            GIT_SHALLOW TRUE
        )
        set(SERIONA_BACKEND_ABSOLUTE_SOURCE_DIR "${seriona_backend_SOURCE_DIR}")
        set(SERIONA_BACKEND_BINARY_DIR "${seriona_backend_BINARY_DIR}")
    endif()

    if(EXISTS "${SERIONA_BACKEND_ABSOLUTE_SOURCE_DIR}/CMakeLists.txt")
        set(SERIONA_BUILD_APP OFF CACHE BOOL "Build the standalone Seriona backend app" FORCE)
        set(SERIONA_BUILD_TESTS OFF CACHE BOOL "Build Seriona tests" FORCE)
        set(SERIONA_FRONTEND_BUILD_TESTING "${BUILD_TESTING}")
        set(BUILD_TESTING OFF CACHE BOOL "Build tests" FORCE)
        add_subdirectory("${SERIONA_BACKEND_ABSOLUTE_SOURCE_DIR}" "${SERIONA_BACKEND_BINARY_DIR}" EXCLUDE_FROM_ALL)
        set(BUILD_TESTING "${SERIONA_FRONTEND_BUILD_TESTING}" CACHE BOOL "Build tests" FORCE)

        if(TARGET SerionaBackend::control)
            set(SERIONA_BACKEND_CONTROL_TARGET SerionaBackend::control)
        elseif(TARGET seriona_control)
            set(SERIONA_BACKEND_CONTROL_TARGET seriona_control)
        else()
            message(FATAL_ERROR "Seriona backend was added, but neither SerionaBackend::control nor seriona_control exists.")
        endif()

        if(TARGET SerionaBackend::audio)
            set(SERIONA_BACKEND_AUDIO_TARGET SerionaBackend::audio)
        elseif(TARGET seriona_audio)
            set(SERIONA_BACKEND_AUDIO_TARGET seriona_audio)
        else()
            message(FATAL_ERROR "Seriona backend was added, but neither SerionaBackend::audio nor seriona_audio exists.")
        endif()

        if(TARGET SerionaBackend::app)
            set(SERIONA_BACKEND_APP_TARGET SerionaBackend::app)
        elseif(TARGET seriona_app)
            set(SERIONA_BACKEND_APP_TARGET seriona_app)
        else()
            message(FATAL_ERROR "Seriona backend was added, but neither SerionaBackend::app nor seriona_app exists.")
        endif()

        message(STATUS "Seriona frontend will link backend target: ${SERIONA_BACKEND_CONTROL_TARGET}")
        find_package(spdlog CONFIG REQUIRED)
    else()
            message(FATAL_ERROR "Seriona backend source is required unless SERIONA_BACKEND_SOURCE_DIR is empty for mock-only mode.")
        endif()
    endif()
endif()

include(CTest)

if(SERIONA_BACKEND_CONTROL_TARGET)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/SerionaFrontendCTestIsolation.cmake" [=[
function(subdirs)
    foreach(directory IN LISTS ARGN)
        if(NOT directory STREQUAL "Seriona_Backend")
            message(FATAL_ERROR "Unexpected CTest subdir '${directory}' in Seriona frontend test tree.")
        endif()
    endforeach()
endfunction()
]=])
    set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${CMAKE_CURRENT_BINARY_DIR}/SerionaFrontendCTestIsolation.cmake")
endif()

if(WIN32)
    # Windows 应用图标：.rc 资源编译进 .exe（IDI_ICON1），Qt Windows QPA 自动
    # 拾取为任务栏/标题栏图标（WM_SETICON）；也保证启动前资源管理器图标正确。
    set(SERIONA_APP_ICON_RESOURCE "${CMAKE_CURRENT_SOURCE_DIR}/assets/app.rc")
endif()

qt_add_executable(seriona
    src/main.cpp
    ${SERIONA_APP_ICON_RESOURCE}
)

# 注册 Theme.qml 为 Singleton
set_source_files_properties(qml/theme/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

set(SERIONA_APP_LAYER_SOURCES
    src/app/path_text.h
    src/app/app_facade.cpp
    src/app/track_stats_controller.cpp
    src/app/track_stats_controller.h
    src/app/app_facade.h
    src/app/playback_controller.cpp
    src/app/playback_controller.h
    src/app/artwork_palette_worker.cpp
    src/app/artwork_palette_worker.h
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
    src/app/library_folder_projection_model.cpp
    src/app/library_folder_projection_model.h
    src/app/library_tree_store.cpp
    src/app/library_tree_store.h
    src/app/track_stats_controller.cpp
    src/app/track_stats_controller.h
)

set(SERIONA_QML_MODULE_FILES
    qml/Main.qml
    qml/theme/Theme.qml
    qml/components/BubbleMenu.qml
    qml/components/menuRegistry.js
    qml/components/BubbleMenuItem.qml
    qml/components/BubbleSubMenuItem.qml
    qml/components/DynamicBackground.qml
    qml/components/StyleButton.qml
    qml/components/Sidebar.qml
    qml/components/SharedToolTip.qml
    qml/components/WindowControls.qml
    qml/components/WaveformProgressBar.qml
    qml/components/MarqueeText.qml
    qml/components/SortDialog.qml
    qml/components/SortRuleRow.qml
    qml/components/ConfirmDeleteDialog.qml
    qml/components/TrackContextMenu.qml
    qml/components/PlaylistDelegate.qml
    qml/components/FolderPage.qml
    qml/components/QueueView.qml
    qml/components/AboutOverlay.qml
    qml/components/StyledScrollBar.qml
    qml/views/MainContent.qml
    qml/views/StartupView.qml
    qml/windows/SettingsWindow.qml
    qml/windows/EqualizerWindow.qml
    qml/windows/TrackDetailWindow.qml
)

set(SERIONA_QML_MODULE_RESOURCES
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
    qml/assets/app-icon-16.png
    qml/assets/app-icon-22.png
    qml/assets/app-icon-24.png
    qml/assets/app-icon-32.png
    qml/assets/app-icon-48.png
    qml/assets/app-icon-64.png
    qml/assets/app-icon-128.png
    qml/assets/app-icon-256.png
)

qt_add_qml_module(seriona
    URI Seriona
    # QML 模块产物输出到 qml-modules/ 子目录：qt_add_qml_module 默认在构建根
    # 生成与目标同名的模块目录（build/seriona/），会与可执行文件输出冲突。
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/qml-modules
    SOURCES
        ${SERIONA_APP_LAYER_SOURCES}
    QML_FILES
        ${SERIONA_QML_MODULE_FILES}
    RESOURCES
        ${SERIONA_QML_MODULE_RESOURCES}
)

# 测试进程复用 Seriona 的 QML 类型注册器：AUTOGEN 生成的 qmltyperegistrations.cpp
# 带 GENERATED 属性，测试目标直接引用会让 VS 生成器产生空 CustomBuild（MSB4035）。
# 复制为独立源文件供测试目标引用（避免 unset GENERATED 触发 CMP0118）。
# 必须用 add_custom_command(OUTPUT ...) 声明真正的文件生成规则：POST_BUILD 复制
# 只是 target 附加命令，在 ninja 依赖图里不构成该文件的生成 edge，全新构建目录下
# 测试目标 autogen 直接依赖它时会报 "missing and no known rule to make it"。
# OUTPUT 产物自动带 GENERATED 属性（配置期不校验存在性），DEPENDS 上 AUTOGEN 产物
# 把复制 edge 连到 seriona 的 qmltyperegistrations 生成步骤，测试目标另有
# add_dependencies(seriona) 保证整体顺序。
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/seriona_qmltyperegistrations_test.cpp"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_BINARY_DIR}/seriona_qmltyperegistrations.cpp"
        "${CMAKE_CURRENT_BINARY_DIR}/seriona_qmltyperegistrations_test.cpp"
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/seriona_qmltyperegistrations.cpp"
    VERBATIM
)

target_include_directories(seriona
    PRIVATE src/app
)

target_compile_definitions(seriona
    PRIVATE SERIONA_HAS_BACKEND=$<BOOL:${SERIONA_BACKEND_CONTROL_TARGET}>
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(seriona PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.Seriona
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(seriona
    PRIVATE Qt6::Quick Qt6::Concurrent Qt6::QuickDialogs2 Qt6::Widgets
)

if(SERIONA_BACKEND_CONTROL_TARGET)
    target_link_libraries(seriona
        PRIVATE ${SERIONA_BACKEND_CONTROL_TARGET} ${SERIONA_BACKEND_AUDIO_TARGET} ${SERIONA_BACKEND_APP_TARGET} spdlog::spdlog
    )
endif()

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Quick Test)

    add_executable(seriona_frontend_command_result_mapping
        tests/frontend/adapter/tst_command_result_mapping.cpp
        src/app/backend_command_adapter.cpp
        src/app/backend_command_adapter.h
    )
    target_include_directories(seriona_frontend_command_result_mapping
        PRIVATE src/app
    )
    target_compile_definitions(seriona_frontend_command_result_mapping
        PRIVATE SERIONA_HAS_BACKEND=$<BOOL:${SERIONA_BACKEND_CONTROL_TARGET}>
    )
    target_link_libraries(seriona_frontend_command_result_mapping
        PRIVATE Qt6::Test
    )
    if(SERIONA_BACKEND_CONTROL_TARGET)
        target_link_libraries(seriona_frontend_command_result_mapping
            PRIVATE ${SERIONA_BACKEND_CONTROL_TARGET}
        )
    endif()
    add_test(NAME seriona_frontend_command_result_mapping COMMAND seriona_frontend_command_result_mapping)

    add_executable(seriona_frontend_snapshot_mapping
        tests/frontend/adapter/tst_snapshot_mapping.cpp
        src/app/backend_snapshot_mapper.cpp
        src/app/backend_snapshot_mapper.h
    )
    target_include_directories(seriona_frontend_snapshot_mapping
        PRIVATE src/app
    )
    target_compile_definitions(seriona_frontend_snapshot_mapping
        PRIVATE SERIONA_HAS_BACKEND=$<BOOL:${SERIONA_BACKEND_CONTROL_TARGET}>
    )
    target_link_libraries(seriona_frontend_snapshot_mapping
        PRIVATE Qt6::Test
    )
    if(SERIONA_BACKEND_CONTROL_TARGET)
        target_link_libraries(seriona_frontend_snapshot_mapping
            PRIVATE ${SERIONA_BACKEND_CONTROL_TARGET}
        )
    endif()
    add_test(NAME seriona_frontend_snapshot_mapping COMMAND seriona_frontend_snapshot_mapping)

    add_executable(seriona_frontend_library_tree_mapping
        tests/frontend/adapter/tst_library_tree_mapping.cpp
    )
    target_link_libraries(seriona_frontend_library_tree_mapping
        PRIVATE Qt6::Test
    )
    add_test(NAME seriona_frontend_library_tree_mapping COMMAND seriona_frontend_library_tree_mapping)

    # 设置控制器为纯 QML 面（无后端依赖），mock-only 构建同样注册
    add_executable(seriona_frontend_settings_controller_tests
        tests/frontend/adapter/tst_settings_controller.cpp
        src/app/settings_controller.cpp
        src/app/app_settings_storage.cpp
        src/app/app_settings_storage.h
        src/app/settings_controller.h
    )
    target_include_directories(seriona_frontend_settings_controller_tests
        PRIVATE src/app
    )
    target_link_libraries(seriona_frontend_settings_controller_tests
        PRIVATE Qt6::Quick Qt6::Test
    )
    add_test(NAME seriona_frontend_settings_controller COMMAND seriona_frontend_settings_controller_tests defaults)
    add_test(NAME seriona_frontend_settings_persist_notify COMMAND seriona_frontend_settings_controller_tests propertySettersPersistAndNotify)
    add_test(NAME seriona_frontend_settings_invalid_values COMMAND seriona_frontend_settings_controller_tests invalidValuesRejected)
    add_test(NAME seriona_frontend_settings_apply_payload COMMAND seriona_frontend_settings_controller_tests applyAssemblesPayload)
    add_test(NAME seriona_frontend_settings_discrete_push COMMAND seriona_frontend_settings_controller_tests discreteControlsPushImmediately)
    add_test(NAME seriona_frontend_settings_debounce COMMAND seriona_frontend_settings_controller_tests bufferDurationDebounces)
    add_test(NAME seriona_frontend_settings_persistence_roundtrip COMMAND seriona_frontend_settings_controller_tests persistenceRoundTrip)
    add_test(NAME seriona_frontend_settings_set_defaults COMMAND seriona_frontend_settings_controller_tests setDefaultsLandsPropertiesWithoutPersistenceOrPush)
    add_test(NAME seriona_frontend_settings_enumerate_devices COMMAND seriona_frontend_settings_controller_tests enumerateDevicesUpdatesList)
    add_test(NAME seriona_frontend_settings_startup_push COMMAND seriona_frontend_settings_controller_tests startupPushSequence)
    add_test(NAME seriona_frontend_settings_lyrics_persist COMMAND seriona_frontend_settings_controller_tests lyricDelimitersPersistRoundTrip)
    add_test(NAME seriona_frontend_settings_sample_format COMMAND seriona_frontend_settings_controller_tests sampleFormatPersistRoundTrip)
    add_test(NAME seriona_frontend_settings_apply_sample_format COMMAND seriona_frontend_settings_controller_tests applyIncludesSampleFormat)
    add_test(NAME seriona_frontend_settings_rollback COMMAND seriona_frontend_settings_controller_tests rollbackRejectedOutputConfigRestoresSnapshot)
    add_test(NAME seriona_frontend_settings_grey_state COMMAND seriona_frontend_settings_controller_tests directOutputGreyState)
    add_test(NAME seriona_frontend_settings_device_caps_empty COMMAND seriona_frontend_settings_controller_tests deviceCapsEmptyShowsAllOptions)
    add_test(NAME seriona_frontend_settings_device_caps_filter COMMAND seriona_frontend_settings_controller_tests deviceCapsFilterSampleRatesAndFormats)
    add_test(NAME seriona_frontend_settings_device_caps_follow COMMAND seriona_frontend_settings_controller_tests deviceCapsFollowSelectedDevice)
    add_test(NAME seriona_frontend_settings_device_caps_expose COMMAND seriona_frontend_settings_controller_tests enumerateDevicesExposesCapabilities)
    add_test(NAME seriona_frontend_settings_unsupported_saved_value COMMAND seriona_frontend_settings_controller_tests unsupportedSavedValueKeptAndMarked)
    add_test(NAME seriona_frontend_settings_log_level_persist COMMAND seriona_frontend_settings_controller_tests logLevelPersistRoundTrip)
    add_test(NAME seriona_frontend_settings_log_level_mapping COMMAND seriona_frontend_settings_controller_tests logLevelMapping)
    add_test(NAME seriona_frontend_settings_log_level_push_defense COMMAND seriona_frontend_settings_controller_tests logLevelPushAndDefense)
    add_test(NAME seriona_frontend_settings_follow_restore_persist COMMAND seriona_frontend_settings_controller_tests followRestoreDelayPersistRoundTrip)
    add_test(NAME seriona_frontend_settings_transition_defaults COMMAND seriona_frontend_settings_controller_tests transitionDefaults)
    add_test(NAME seriona_frontend_settings_transition_persist_notify COMMAND seriona_frontend_settings_controller_tests transitionPropertySettersPersistAndNotify)
    add_test(NAME seriona_frontend_settings_transition_invalid_values COMMAND seriona_frontend_settings_controller_tests transitionInvalidValuesRejected)
    add_test(NAME seriona_frontend_settings_transition_debounce COMMAND seriona_frontend_settings_controller_tests transitionSliderSettersDebounceMerged)
add_test(NAME seriona_frontend_settings_transition_roundtrip COMMAND seriona_frontend_settings_controller_tests transitionPersistRoundTrip)
add_test(NAME seriona_frontend_settings_transition_greyed COMMAND seriona_frontend_settings_controller_tests advanceTransitionsGreyedFollowsOutputMode)
add_test(NAME seriona_frontend_settings_transition_apply_pack COMMAND seriona_frontend_settings_controller_tests transitionApplyPacksCurrentNineFields)
add_test(NAME seriona_frontend_settings_transition_executor_unset COMMAND seriona_frontend_settings_controller_tests transitionExecutorUnsetNoopPersistence)
add_test(NAME seriona_frontend_settings_transition_startup_apply COMMAND seriona_frontend_settings_controller_tests transitionStartupApplySequence)

    # 播放次数/星级持久化（T16）：纯 QML 面（无后端依赖），mock-only 构建同样注册
    add_executable(seriona_frontend_track_stats_tests
        tests/frontend/adapter/tst_track_stats_controller.cpp
        src/app/track_stats_controller.cpp
        src/app/app_settings_storage.cpp
        src/app/app_settings_storage.h
        src/app/track_stats_controller.h
    )
    target_include_directories(seriona_frontend_track_stats_tests
        PRIVATE src/app
    )
    target_link_libraries(seriona_frontend_track_stats_tests
        PRIVATE Qt6::Quick Qt6::Test
    )
    add_test(NAME seriona_frontend_track_stats_defaults COMMAND seriona_frontend_track_stats_tests defaultsAreZeroAndUnrated)
    add_test(NAME seriona_frontend_track_stats_play_count COMMAND seriona_frontend_track_stats_tests recordPlaybackIncrementsPerTrack)
    add_test(NAME seriona_frontend_track_stats_play_count_signal COMMAND seriona_frontend_track_stats_tests recordPlaybackEmitsSignalWithTrackAndCount)
    add_test(NAME seriona_frontend_track_stats_rating COMMAND seriona_frontend_track_stats_tests ratingRoundTripAndClamping)
    add_test(NAME seriona_frontend_track_stats_rating_signal COMMAND seriona_frontend_track_stats_tests ratingEmitsSignal)
    add_test(NAME seriona_frontend_track_stats_persist COMMAND seriona_frontend_track_stats_tests persistenceRoundTripAcrossControllerInstances)
    add_test(NAME seriona_frontend_track_stats_empty_id COMMAND seriona_frontend_track_stats_tests emptyTrackIdIgnored)

    # 关于界面 overlay（T19）：纯 QML 面（无后端依赖），mock-only 构建同样注册
    add_executable(seriona_frontend_about_overlay_tests
        tests/frontend/adapter/tst_about_overlay.cpp
    )
    target_include_directories(seriona_frontend_about_overlay_tests
        PRIVATE src/app
    )
    target_link_libraries(seriona_frontend_about_overlay_tests
        PRIVATE Qt6::Quick Qt6::Test
    )
    # 测试进程复用 Seriona 的 QML 模块资源（qrc 目录映射），使
    # `import Seriona`（含 Theme singleton）可解析；qrc 由 qt_add_qml_module
    # 构建期生成，故测试目标先依赖 Seriona。
    qt6_add_resources(SERIONA_ABOUT_TEST_QRC
        ${CMAKE_CURRENT_BINARY_DIR}/.qt/rcc/qmake_Seriona.qrc
        ${CMAKE_CURRENT_BINARY_DIR}/.qt/rcc/seriona_raw_qml_0.qrc
        ${CMAKE_CURRENT_BINARY_DIR}/.qt/rcc/seriona_raw_qml_0_extra_qmldirs.qrc
    )
    target_sources(seriona_frontend_about_overlay_tests PRIVATE ${SERIONA_ABOUT_TEST_QRC})
    # 测试加载构建目录的 Seriona QML 模块产物（build/seriona/qmldir），
    # 依赖 Seriona 目标的 qt_add_qml_module 输出先就绪。
    add_dependencies(seriona_frontend_about_overlay_tests seriona)
    add_test(NAME seriona_frontend_about_overlay COMMAND seriona_frontend_about_overlay_tests)

    add_executable(seriona_frontend_queue_view_tests
        tests/frontend/adapter/tst_queue_view.cpp
    )
    target_include_directories(seriona_frontend_queue_view_tests
        PRIVATE src/app
    )
    target_link_libraries(seriona_frontend_queue_view_tests
        PRIVATE Qt6::Quick Qt6::Test
    )
    # 与 about overlay 测试同模式：复用 Seriona 的 QML 模块资源（qrc 目录
    # 映射），使 `import Seriona`（含 Theme singleton）可解析。
    qt6_add_resources(SERIONA_QUEUE_VIEW_TEST_QRC
        ${CMAKE_CURRENT_BINARY_DIR}/.qt/rcc/qmake_Seriona.qrc
        ${CMAKE_CURRENT_BINARY_DIR}/.qt/rcc/seriona_raw_qml_0.qrc
        ${CMAKE_CURRENT_BINARY_DIR}/.qt/rcc/seriona_raw_qml_0_extra_qmldirs.qrc
    )
    target_sources(seriona_frontend_queue_view_tests PRIVATE ${SERIONA_QUEUE_VIEW_TEST_QRC})
    add_dependencies(seriona_frontend_queue_view_tests seriona)
    add_test(NAME seriona_frontend_queue_view COMMAND seriona_frontend_queue_view_tests)

    if(SERIONA_BACKEND_CONTROL_TARGET)
        add_executable(seriona_frontend_library_tree_model_tests
            tests/frontend/adapter/tst_library_tree_model.cpp
            src/app/library_model.cpp
            src/app/library_model.h
        src/app/library_folder_projection_model.cpp
        src/app/library_folder_projection_model.h
            src/app/library_tree_store.cpp
            src/app/library_tree_store.h
            src/app/backend_snapshot_mapper.cpp
            src/app/backend_snapshot_mapper.h
        )
        target_include_directories(seriona_frontend_library_tree_model_tests
            PRIVATE src/app
        )
        target_compile_definitions(seriona_frontend_library_tree_model_tests
            PRIVATE SERIONA_HAS_BACKEND=1
        )
        target_link_libraries(seriona_frontend_library_tree_model_tests
            PRIVATE Qt6::Quick Qt6::Test ${SERIONA_BACKEND_CONTROL_TARGET}
        )
        add_test(NAME seriona_frontend_library_tree_model COMMAND seriona_frontend_library_tree_model_tests)
        add_test(NAME seriona_frontend_library_tree_invalid_child COMMAND seriona_frontend_library_tree_model_tests invalidChildNodeIdsAreSkipped)

        add_executable(seriona_frontend_library_folder_projection_tests
            tests/frontend/adapter/tst_library_folder_projection_model.cpp
            src/app/library_model.cpp
            src/app/library_model.h
            src/app/library_folder_projection_model.cpp
            src/app/library_folder_projection_model.h
            src/app/library_tree_store.cpp
            src/app/library_tree_store.h
            src/app/backend_snapshot_mapper.cpp
            src/app/backend_snapshot_mapper.h
        )
        target_include_directories(seriona_frontend_library_folder_projection_tests
            PRIVATE src/app
        )
        target_compile_definitions(seriona_frontend_library_folder_projection_tests
            PRIVATE SERIONA_HAS_BACKEND=1
        )
        target_link_libraries(seriona_frontend_library_folder_projection_tests
            PRIVATE Qt6::Quick Qt6::Test ${SERIONA_BACKEND_CONTROL_TARGET}
        )
        add_test(NAME seriona_frontend_library_folder_projection COMMAND seriona_frontend_library_folder_projection_tests)
        add_test(NAME seriona_frontend_library_folder_projection_sorts COMMAND seriona_frontend_library_folder_projection_tests projectionSortsPerLevelRules)
        add_test(NAME seriona_frontend_library_folder_projection_rebuild COMMAND seriona_frontend_library_folder_projection_tests treeChangeRebuildsProjection)
        add_test(NAME seriona_frontend_library_folder_projection_sync COMMAND seriona_frontend_library_folder_projection_tests playingAndFocusSyncEmitDataChanged)
        add_test(NAME seriona_frontend_library_folder_projection_stack COMMAND seriona_frontend_library_folder_projection_tests stackDepthAndProjectionLifecycle)
        add_test(NAME seriona_frontend_library_folder_projection_locate COMMAND seriona_frontend_library_folder_projection_tests locateNodeInFolderStackNavigatesToTargetLevel)

        add_executable(seriona_frontend_library_sort_tests
            tests/frontend/adapter/tst_library_sort.cpp
            src/app/library_model.cpp
            src/app/library_model.h
        src/app/library_folder_projection_model.cpp
        src/app/library_folder_projection_model.h
            src/app/library_tree_store.cpp
            src/app/library_tree_store.h
            src/app/backend_snapshot_mapper.cpp
            src/app/backend_snapshot_mapper.h
        )
        target_include_directories(seriona_frontend_library_sort_tests
            PRIVATE src/app
        )
        target_compile_definitions(seriona_frontend_library_sort_tests
            PRIVATE SERIONA_HAS_BACKEND=1
        )
        target_link_libraries(seriona_frontend_library_sort_tests
            PRIVATE Qt6::Quick Qt6::Test ${SERIONA_BACKEND_CONTROL_TARGET}
        )
        add_test(NAME seriona_frontend_library_sort_no_rules COMMAND seriona_frontend_library_sort_tests noRulesPreserveScannerOrderForRootFolderAndSearch)
        add_test(NAME seriona_frontend_library_sort_empty_rules COMMAND seriona_frontend_library_sort_tests emptyRulesRestoreScannerOrderAfterSorting)
        add_test(NAME seriona_frontend_library_sort_title COMMAND seriona_frontend_library_sort_tests titleAscendingAndDescendingSortCurrentFolderProjection)
        add_test(NAME seriona_frontend_library_sort_fields COMMAND seriona_frontend_library_sort_tests supportedSortDialogFieldsSortCurrentProjection)
        add_test(NAME seriona_frontend_library_sort_reapply COMMAND seriona_frontend_library_sort_tests currentRulesReapplyAcrossEnterFolderSearchSubmitAndClear)
        add_test(NAME seriona_frontend_library_sort_snapshot COMMAND seriona_frontend_library_sort_tests snapshotUpdateReappliesRulesWithoutMutatingScannerOrder)
        add_test(NAME seriona_frontend_library_sort_snapshot_reconcile_markers COMMAND seriona_frontend_library_sort_tests snapshotReconcileKeepsRootSearchSortAndPlaybackMarkers)
        add_test(NAME seriona_frontend_library_sort_snapshot_reconcile_fallback COMMAND seriona_frontend_library_sort_tests snapshotReconcileFallsBackToVisibleRowsWhenFolderDisappears)
        add_test(NAME seriona_frontend_library_sort_invalid COMMAND seriona_frontend_library_sort_tests invalidSortPayloadLeavesProjectionUnchanged)
        add_test(NAME seriona_frontend_library_sort_persist_command COMMAND seriona_frontend_library_sort_tests folderSortSendsApplyCommandWithRootAndFolderKey)
        add_test(NAME seriona_frontend_library_sort_root_isolation COMMAND seriona_frontend_library_sort_tests sameFolderNodeIdUnderDifferentRootsDoesNotReuseSavedRules)
        add_test(NAME seriona_frontend_library_sort_restore COMMAND seriona_frontend_library_sort_tests reenterFolderAndBackendStateReloadRestoreSavedRules)
        add_test(NAME seriona_frontend_library_sort_backend_notification COMMAND seriona_frontend_library_sort_tests backendFolderSortNotificationUpdatesActiveCurrentRules)
        add_test(NAME seriona_frontend_qml_sort_dialog_current_rules COMMAND seriona_frontend_library_sort_tests currentSortRulesExposeFolderRulesForQmlAndFallbacks)
        add_test(NAME seriona_frontend_library_sort_search_not_persisted COMMAND seriona_frontend_library_sort_tests searchProjectionSortDoesNotPersistOrOverwriteSavedFolderRules)
        add_test(NAME seriona_frontend_library_sort_invalid_context COMMAND seriona_frontend_library_sort_tests missingContextAndMalformedSortPayloadDoNotPersist)
        add_test(NAME seriona_frontend_library_search_subtree COMMAND seriona_frontend_library_sort_tests searchScopedToCurrentFolderSubtree)
        add_test(NAME seriona_frontend_library_search_no_folders COMMAND seriona_frontend_library_sort_tests searchExcludesFolders)
        add_test(NAME seriona_frontend_library_search_score_order COMMAND seriona_frontend_library_sort_tests searchRanksByWeightedScore)
        add_test(NAME seriona_frontend_library_search_clear_restores COMMAND seriona_frontend_library_sort_tests clearSearchRestoresUserSortRules)

        add_executable(seriona_frontend_library_dual_cursor_tests
            tests/frontend/adapter/tst_library_dual_cursor.cpp
            src/app/library_model.cpp
            src/app/library_model.h
        src/app/library_folder_projection_model.cpp
        src/app/library_folder_projection_model.h
            src/app/library_tree_store.cpp
            src/app/library_tree_store.h
            src/app/backend_snapshot_mapper.cpp
            src/app/backend_snapshot_mapper.h
        )
        target_include_directories(seriona_frontend_library_dual_cursor_tests
            PRIVATE src/app
        )
        target_compile_definitions(seriona_frontend_library_dual_cursor_tests
            PRIVATE SERIONA_HAS_BACKEND=1
        )
        target_link_libraries(seriona_frontend_library_dual_cursor_tests
            PRIVATE Qt6::Quick Qt6::Test ${SERIONA_BACKEND_CONTROL_TARGET}
        )
        add_test(NAME seriona_frontend_library_dual_cursor COMMAND seriona_frontend_library_dual_cursor_tests dualCursorKeepsFocusedAndPlayingSeparate)
        add_test(NAME seriona_frontend_library_dual_follow_playing COMMAND seriona_frontend_library_dual_cursor_tests followCurrentlyPlayingControlsBrowserCursor)
        add_test(NAME seriona_frontend_library_follow_playing COMMAND seriona_frontend_library_dual_cursor_tests followCurrentlyPlayingControlsBrowserCursor)
        add_test(NAME seriona_frontend_library_dual_cursor_refresh COMMAND seriona_frontend_library_dual_cursor_tests refreshKeepsAndFallsBackBrowserState)
        add_test(NAME seriona_frontend_library_dual_cursor_no_commands COMMAND seriona_frontend_library_dual_cursor_tests browsingActionsStayLocalToProjection)

        add_executable(seriona_frontend_sidebar_local_browsing_tests
            tests/frontend/adapter/tst_sidebar_local_browsing.cpp
            src/app/library_model.cpp
            src/app/library_model.h
        src/app/library_folder_projection_model.cpp
        src/app/library_folder_projection_model.h
            src/app/library_tree_store.cpp
            src/app/library_tree_store.h
            src/app/backend_snapshot_mapper.cpp
            src/app/backend_snapshot_mapper.h
        )
        target_include_directories(seriona_frontend_sidebar_local_browsing_tests
            PRIVATE src/app
        )
        target_compile_definitions(seriona_frontend_sidebar_local_browsing_tests
            PRIVATE SERIONA_HAS_BACKEND=1
        )
        target_link_libraries(seriona_frontend_sidebar_local_browsing_tests
            PRIVATE Qt6::Quick Qt6::Test ${SERIONA_BACKEND_CONTROL_TARGET}
        )
        add_test(NAME seriona_frontend_sidebar_search_local COMMAND seriona_frontend_sidebar_local_browsing_tests localSearchFiltersAndFocusesFirstMatch)
        add_test(NAME seriona_frontend_sidebar_search_empty COMMAND seriona_frontend_sidebar_local_browsing_tests emptySearchDoesNotMoveBrowserOrPlayback)
        add_test(NAME seriona_frontend_sidebar_tree_local COMMAND seriona_frontend_sidebar_local_browsing_tests enterFolderAndBackStayLocal)
        add_test(NAME seriona_frontend_sidebar_root_select_no_back COMMAND seriona_frontend_sidebar_local_browsing_tests selectingRootProjectionNodeDoesNotEnableBack)

        add_executable(seriona_frontend_ui_only_handler_policy_tests
            tests/frontend/adapter/tst_ui_only_handler_policy.cpp
        )
        target_compile_definitions(seriona_frontend_ui_only_handler_policy_tests
            PRIVATE SERIONA_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        )
        target_link_libraries(seriona_frontend_ui_only_handler_policy_tests
            PRIVATE Qt6::Test
        )
        add_test(NAME seriona_frontend_ui_only_no_backend_command COMMAND seriona_frontend_ui_only_handler_policy_tests uiOnlyHandlersDoNotUseBackendCommands)
        add_test(NAME seriona_frontend_qml_startup_property_contract COMMAND seriona_frontend_ui_only_handler_policy_tests qmlStartupDoesNotCallQtApplicationPropertyFunction)
        add_test(NAME seriona_frontend_qml_layout COMMAND seriona_frontend_ui_only_handler_policy_tests qmlLayoutSourceContractsStayStable)

        add_executable(seriona_frontend_library_select_track_tests
            tests/frontend/adapter/tst_library_select_track.cpp
            src/app/library_model.cpp
            src/app/library_model.h
        src/app/library_folder_projection_model.cpp
        src/app/library_folder_projection_model.h
            src/app/library_tree_store.cpp
            src/app/library_tree_store.h
            src/app/backend_snapshot_mapper.cpp
            src/app/backend_snapshot_mapper.h
        )
        target_include_directories(seriona_frontend_library_select_track_tests
            PRIVATE src/app
        )
        target_compile_definitions(seriona_frontend_library_select_track_tests
            PRIVATE SERIONA_HAS_BACKEND=1
        )
        target_link_libraries(seriona_frontend_library_select_track_tests
            PRIVATE Qt6::Quick Qt6::Test ${SERIONA_BACKEND_CONTROL_TARGET}
        )
add_test(NAME seriona_frontend_library_context_playback_folder COMMAND seriona_frontend_library_select_track_tests explicitTrackNodeActivationSubmitsStartPlaybackFromFolderContext)
add_test(NAME seriona_frontend_library_context_playback_root COMMAND seriona_frontend_library_select_track_tests indexedTrackActivationSubmitsStartPlaybackFromRootContext)
add_test(NAME seriona_frontend_library_context_playback_search_folder COMMAND seriona_frontend_library_select_track_tests searchTrackActivationUsesContainingFolderContext)
add_test(NAME seriona_frontend_library_context_playback_search_root COMMAND seriona_frontend_library_select_track_tests searchRootTrackActivationFallsBackToRootContext)
add_test(NAME seriona_frontend_library_context_playback_hidden_no_command COMMAND seriona_frontend_library_select_track_tests hiddenTrackActivationDoesNotUseStaleFolderContext)
        add_test(NAME seriona_frontend_library_select_locate_projection COMMAND seriona_frontend_library_select_track_tests locateCurrentSongMovesToContainingProjection)
        add_test(NAME seriona_frontend_library_select_locate_missing COMMAND seriona_frontend_library_select_track_tests locateMissingCurrentSongPreservesBrowserState)
        add_test(NAME seriona_frontend_library_select_browse_no_command COMMAND seriona_frontend_library_select_track_tests browsingAndLocateCurrentSongDoNotSubmitCommands)
        add_test(NAME seriona_frontend_library_browse_no_command COMMAND seriona_frontend_library_select_track_tests browsingAndLocateCurrentSongDoNotSubmitCommands)

        add_executable(seriona_frontend_library_scan_flow_tests
            tests/frontend/adapter/tst_library_scan_flow.cpp
            src/app/library_model.cpp
            src/app/library_model.h
        src/app/library_folder_projection_model.cpp
        src/app/library_folder_projection_model.h
            src/app/library_tree_store.cpp
            src/app/library_tree_store.h
            src/app/backend_snapshot_mapper.cpp
            src/app/backend_snapshot_mapper.h
        )
        target_include_directories(seriona_frontend_library_scan_flow_tests
            PRIVATE src/app
        )
        target_compile_definitions(seriona_frontend_library_scan_flow_tests
            PRIVATE SERIONA_HAS_BACKEND=1
        )
        target_link_libraries(seriona_frontend_library_scan_flow_tests
            PRIVATE Qt6::Quick Qt6::Test ${SERIONA_BACKEND_CONTROL_TARGET}
        )
add_test(NAME seriona_frontend_library_scan_request COMMAND seriona_frontend_library_scan_flow_tests scanLibraryRequestSubmitsBackendScanAndUpdatesTree)
add_test(NAME seriona_frontend_library_scan_cancel_invalid COMMAND seriona_frontend_library_scan_flow_tests cancelAndInvalidSelectionDoNotScanOrSaveRoot)
add_test(NAME seriona_frontend_library_scan_state_mapping COMMAND seriona_frontend_library_scan_flow_tests scanSnapshotsMapUiStates)
add_test(NAME seriona_frontend_library_scan_empty_state COMMAND seriona_frontend_library_scan_flow_tests emptySnapshotExposesEmptyLibraryState)
add_test(NAME seriona_frontend_library_scan_backend_unavailable COMMAND seriona_frontend_library_scan_flow_tests backendUnavailableDoesNotFabricateRows)
    endif()

        add_executable(seriona_frontend_app_facade_smoke_mode
            tests/frontend/adapter/tst_app_facade_smoke_mode.cpp
            src/app/app_facade.cpp
            src/app/app_settings_storage.cpp
            src/app/app_settings_storage.h
            src/app/track_stats_controller.cpp
            src/app/track_stats_controller.h
            src/app/app_facade.h
            src/app/playback_controller.cpp
            src/app/playback_controller.h
            src/app/artwork_palette_worker.cpp
            src/app/artwork_palette_worker.h
            src/app/navigation_controller.cpp
            src/app/navigation_controller.h
            src/app/settings_controller.cpp
            src/app/settings_controller.h
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
        src/app/library_folder_projection_model.cpp
        src/app/library_folder_projection_model.h
            src/app/library_tree_store.cpp
            src/app/library_tree_store.h
        )
    target_include_directories(seriona_frontend_app_facade_smoke_mode
        PRIVATE src/app
    )
    target_compile_definitions(seriona_frontend_app_facade_smoke_mode
        PRIVATE SERIONA_HAS_BACKEND=$<BOOL:${SERIONA_BACKEND_CONTROL_TARGET}>
    )
        target_link_libraries(seriona_frontend_app_facade_smoke_mode
            PRIVATE Qt6::Quick Qt6::Test Qt6::Concurrent
        )
        if(SERIONA_BACKEND_CONTROL_TARGET)
            target_link_libraries(seriona_frontend_app_facade_smoke_mode
                PRIVATE ${SERIONA_BACKEND_CONTROL_TARGET} ${SERIONA_BACKEND_AUDIO_TARGET} ${SERIONA_BACKEND_APP_TARGET} spdlog::spdlog
            )
        endif()
    add_test(NAME seriona_frontend_app_facade_smoke_mode COMMAND seriona_frontend_app_facade_smoke_mode)

    if(SERIONA_BACKEND_CONTROL_TARGET)
        add_executable(seriona_frontend_startup_restore_tests
            tests/frontend/adapter/tst_startup_restore.cpp
            src/app/app_facade.cpp
            src/app/app_settings_storage.cpp
            src/app/app_settings_storage.h
            src/app/track_stats_controller.cpp
            src/app/track_stats_controller.h
            src/app/app_facade.h
            src/app/playback_controller.cpp
            src/app/playback_controller.h
            src/app/artwork_palette_worker.cpp
            src/app/artwork_palette_worker.h
            src/app/navigation_controller.cpp
            src/app/navigation_controller.h
            src/app/settings_controller.cpp
            src/app/settings_controller.h
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
        src/app/library_folder_projection_model.cpp
        src/app/library_folder_projection_model.h
            src/app/library_tree_store.cpp
            src/app/library_tree_store.h
        )
        target_include_directories(seriona_frontend_startup_restore_tests
            PRIVATE src/app
        )
        target_compile_definitions(seriona_frontend_startup_restore_tests
            PRIVATE SERIONA_HAS_BACKEND=1
        )
        target_link_libraries(seriona_frontend_startup_restore_tests
            PRIVATE Qt6::Quick Qt6::Test Qt6::Concurrent ${SERIONA_BACKEND_CONTROL_TARGET} ${SERIONA_BACKEND_AUDIO_TARGET} ${SERIONA_BACKEND_APP_TARGET} spdlog::spdlog
        )

        add_test(NAME seriona_frontend_startup_persist_root COMMAND seriona_frontend_startup_restore_tests scanLibraryPersistsRootForStartupRestore)
        add_test(NAME seriona_frontend_startup_restore_root COMMAND seriona_frontend_startup_restore_tests restoreSavedRootScansAndEntersMainShell)
        add_test(NAME seriona_frontend_startup_missing_root COMMAND seriona_frontend_startup_restore_tests missingSavedRoot)

        add_executable(seriona_frontend_playback_snapshot_tests
            tests/frontend/adapter/tst_playback_snapshot_mapping.cpp
            src/app/app_facade.cpp
            src/app/app_settings_storage.cpp
            src/app/app_settings_storage.h
            src/app/track_stats_controller.cpp
            src/app/track_stats_controller.h
            src/app/app_facade.h
            src/app/playback_controller.cpp
            src/app/playback_controller.h
            src/app/artwork_palette_worker.cpp
            src/app/artwork_palette_worker.h
            src/app/navigation_controller.cpp
            src/app/navigation_controller.h
            src/app/settings_controller.cpp
            src/app/settings_controller.h
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
        src/app/library_folder_projection_model.cpp
        src/app/library_folder_projection_model.h
            src/app/library_tree_store.cpp
            src/app/library_tree_store.h
        )
        target_include_directories(seriona_frontend_playback_snapshot_tests
            PRIVATE src/app
        )
        target_compile_definitions(seriona_frontend_playback_snapshot_tests
            PRIVATE SERIONA_HAS_BACKEND=1
        )
        target_link_libraries(seriona_frontend_playback_snapshot_tests
            PRIVATE Qt6::Quick Qt6::Test Qt6::Concurrent ${SERIONA_BACKEND_CONTROL_TARGET} ${SERIONA_BACKEND_AUDIO_TARGET} ${SERIONA_BACKEND_APP_TARGET} spdlog::spdlog
        )

        add_test(NAME seriona_frontend_playback_snapshot_mapping COMMAND seriona_frontend_playback_snapshot_tests snapshotMapping)
        add_test(NAME seriona_frontend_playback_empty_snapshot COMMAND seriona_frontend_playback_snapshot_tests emptySnapshot)
        add_test(NAME seriona_frontend_playback_timeline_smoothing COMMAND seriona_frontend_playback_snapshot_tests timelineSmoothing)
        add_test(NAME seriona_frontend_playback_timeline_stopped_error COMMAND seriona_frontend_playback_snapshot_tests timelineStoppedAndErrorSnapToBackend)
        add_test(NAME seriona_frontend_playback_queue_mapping COMMAND seriona_frontend_playback_snapshot_tests queueEntriesMapping)

        add_executable(seriona_frontend_sidebar_queue_tests
            tests/frontend/adapter/tst_sidebar_queue_switch.cpp
            src/app/app_facade.cpp
            src/app/app_settings_storage.cpp
            src/app/app_settings_storage.h
            src/app/track_stats_controller.cpp
            src/app/track_stats_controller.h
            src/app/app_facade.h
            src/app/playback_controller.cpp
            src/app/playback_controller.h
            src/app/artwork_palette_worker.cpp
            src/app/artwork_palette_worker.h
            src/app/navigation_controller.cpp
            src/app/navigation_controller.h
            src/app/settings_controller.cpp
            src/app/settings_controller.h
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
        src/app/library_folder_projection_model.cpp
        src/app/library_folder_projection_model.h
            src/app/library_tree_store.cpp
            src/app/library_tree_store.h
        )
        target_include_directories(seriona_frontend_sidebar_queue_tests
            PRIVATE src/app
        )
        target_compile_definitions(seriona_frontend_sidebar_queue_tests
            PRIVATE SERIONA_HAS_BACKEND=1
        )
        target_link_libraries(seriona_frontend_sidebar_queue_tests
            PRIVATE Qt6::Quick Qt6::Test Qt6::Concurrent ${SERIONA_BACKEND_CONTROL_TARGET} ${SERIONA_BACKEND_AUDIO_TARGET} ${SERIONA_BACKEND_APP_TARGET} spdlog::spdlog
        )
        # 注：不引用 seriona_raw_res_0.qrc（SVG 资产）——主模块 seriona 与测试目标
        # 对 qrc_seriona_raw_res_0.cpp 注册重复 rcc 规则，Qt 6.8 下测试目标的规则
        # 不生效导致输出缺失；资源缺失不影响 QML 加载与测试逻辑。
        qt6_add_resources(SERIONA_SIDEBAR_QUEUE_TEST_QRC
            ${CMAKE_CURRENT_BINARY_DIR}/.qt/rcc/qmake_Seriona.qrc
            ${CMAKE_CURRENT_BINARY_DIR}/.qt/rcc/seriona_raw_qml_0.qrc
            ${CMAKE_CURRENT_BINARY_DIR}/.qt/rcc/seriona_raw_qml_0_extra_qmldirs.qrc
            # 模块目录映射：qrc:/qt/qml/Seriona -> build/qml-modules，引擎凭此
            # 找到 qmldir（构建 seriona 时生成，add_dependencies 保证顺序）
            ${CMAKE_CURRENT_BINARY_DIR}/qml-modules/seriona_qml_module_dir_map.qrc
        )
        # 复用 qt_add_qml_module 生成的静态类型注册器：Sidebar 引用的
        # AppFacade/PlaybackController/LibraryController 等 C++ 类型只有注册后
        # QML 引擎才能解析（qmldir 无 plugin 行，类型随 Seriona 静态注册）。
        target_sources(seriona_frontend_sidebar_queue_tests PRIVATE
            ${SERIONA_SIDEBAR_QUEUE_TEST_QRC}
            # seriona qmltyperegistrations 复制产物（见 qt_add_qml_module 后的说明）
            ${CMAKE_CURRENT_BINARY_DIR}/seriona_qmltyperegistrations_test.cpp
        )
        add_dependencies(seriona_frontend_sidebar_queue_tests seriona)
        add_test(NAME seriona_frontend_sidebar_queue_switch COMMAND seriona_frontend_sidebar_queue_tests)

        add_executable(seriona_frontend_folder_page_tests
            tests/frontend/adapter/tst_folder_page.cpp
            src/app/app_facade.cpp
            src/app/app_settings_storage.cpp
            src/app/app_settings_storage.h
            src/app/track_stats_controller.cpp
            src/app/track_stats_controller.h
            src/app/app_facade.h
            src/app/playback_controller.cpp
            src/app/playback_controller.h
            src/app/artwork_palette_worker.cpp
            src/app/artwork_palette_worker.h
            src/app/navigation_controller.cpp
            src/app/navigation_controller.h
            src/app/settings_controller.cpp
            src/app/settings_controller.h
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
            src/app/library_folder_projection_model.cpp
            src/app/library_folder_projection_model.h
            src/app/library_tree_store.cpp
            src/app/library_tree_store.h
        )
        target_include_directories(seriona_frontend_folder_page_tests
            PRIVATE src/app
        )
        target_compile_definitions(seriona_frontend_folder_page_tests
            PRIVATE SERIONA_HAS_BACKEND=1
        )
        target_link_libraries(seriona_frontend_folder_page_tests
            PRIVATE Qt6::Quick Qt6::Test Qt6::Concurrent ${SERIONA_BACKEND_CONTROL_TARGET} ${SERIONA_BACKEND_AUDIO_TARGET} ${SERIONA_BACKEND_APP_TARGET} spdlog::spdlog
        )
        qt6_add_resources(SERIONA_FOLDER_PAGE_TEST_QRC
            ${CMAKE_CURRENT_BINARY_DIR}/.qt/rcc/qmake_Seriona.qrc
            ${CMAKE_CURRENT_BINARY_DIR}/.qt/rcc/seriona_raw_qml_0.qrc
            ${CMAKE_CURRENT_BINARY_DIR}/.qt/rcc/seriona_raw_qml_0_extra_qmldirs.qrc
            # 模块目录映射：qrc:/qt/qml/Seriona -> build/qml-modules，引擎凭此
            # 找到 qmldir（构建 seriona 时生成，add_dependencies 保证顺序）
            ${CMAKE_CURRENT_BINARY_DIR}/qml-modules/seriona_qml_module_dir_map.qrc
        )
        target_sources(seriona_frontend_folder_page_tests PRIVATE
            ${SERIONA_FOLDER_PAGE_TEST_QRC}
            ${CMAKE_CURRENT_BINARY_DIR}/seriona_qmltyperegistrations_test.cpp
        )
        add_dependencies(seriona_frontend_folder_page_tests seriona)
        add_test(NAME seriona_frontend_folder_page_tests COMMAND seriona_frontend_folder_page_tests)

        add_executable(seriona_frontend_playback_command_tests
            tests/frontend/adapter/tst_playback_command_mapping.cpp
            src/app/app_facade.cpp
            src/app/app_settings_storage.cpp
            src/app/app_settings_storage.h
            src/app/track_stats_controller.cpp
            src/app/track_stats_controller.h
            src/app/app_facade.h
            src/app/playback_controller.cpp
            src/app/playback_controller.h
            src/app/artwork_palette_worker.cpp
            src/app/artwork_palette_worker.h
            src/app/navigation_controller.cpp
            src/app/navigation_controller.h
            src/app/settings_controller.cpp
            src/app/settings_controller.h
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
        src/app/library_folder_projection_model.cpp
        src/app/library_folder_projection_model.h
            src/app/library_tree_store.cpp
            src/app/library_tree_store.h
        )
        target_include_directories(seriona_frontend_playback_command_tests
            PRIVATE src/app
        )
        target_compile_definitions(seriona_frontend_playback_command_tests
            PRIVATE SERIONA_HAS_BACKEND=1
        )
        target_link_libraries(seriona_frontend_playback_command_tests
            PRIVATE Qt6::Quick Qt6::Test Qt6::Concurrent ${SERIONA_BACKEND_CONTROL_TARGET} ${SERIONA_BACKEND_AUDIO_TARGET} ${SERIONA_BACKEND_APP_TARGET} spdlog::spdlog
        )

        add_test(NAME seriona_frontend_playback_command_mapping COMMAND seriona_frontend_playback_command_tests visiblePlaybackControlsSubmitCommands)
        add_test(NAME seriona_frontend_playback_rejected_command COMMAND seriona_frontend_playback_command_tests rejectedCommandKeepsReadModelAndQueuesNotification)

        add_executable(seriona_frontend_notifications_tests
            tests/frontend/adapter/tst_frontend_notifications.cpp
            src/app/notification_controller.cpp
            src/app/notification_controller.h
            src/app/backend_command_adapter.cpp
            src/app/backend_command_adapter.h
        )
        target_include_directories(seriona_frontend_notifications_tests
            PRIVATE src/app
        )
        target_compile_definitions(seriona_frontend_notifications_tests
            PRIVATE SERIONA_HAS_BACKEND=1
        )
        target_link_libraries(seriona_frontend_notifications_tests
            PRIVATE Qt6::Quick Qt6::Test ${SERIONA_BACKEND_CONTROL_TARGET} spdlog::spdlog
        )

        add_test(NAME seriona_frontend_notifications_command_rejected COMMAND seriona_frontend_notifications_tests commandRejectedResultExposesCodeAndMessage)
        add_test(NAME seriona_frontend_notifications_playback_error COMMAND seriona_frontend_notifications_tests playbackErrorExposesBackendMessage)
        add_test(NAME seriona_frontend_notifications_library_scan_error COMMAND seriona_frontend_notifications_tests libraryScanErrorExposesBackendMessage)
        add_test(NAME seriona_frontend_notifications_library_snapshot_updated COMMAND seriona_frontend_notifications_tests librarySnapshotUpdatedUsesVisibleDefaultMessage)
        add_test(NAME seriona_frontend_notifications_bounded_queue COMMAND seriona_frontend_notifications_tests boundedQueueDropsOldestNotifications)
        add_test(NAME seriona_frontend_notifications_unsupported_feedback COMMAND seriona_frontend_notifications_tests unsupportedSettingsActionsShowLocalFeedback)
        add_test(NAME seriona_frontend_unsupported_settings_feedback COMMAND seriona_frontend_notifications_tests unsupportedSettingsActionsShowLocalFeedback)

        add_executable(seriona_frontend_waveform_worker_tests
            tests/frontend/adapter/tst_waveform_worker.cpp
            src/app/app_facade.cpp
            src/app/app_settings_storage.cpp
            src/app/app_settings_storage.h
            src/app/track_stats_controller.cpp
            src/app/track_stats_controller.h
            src/app/app_facade.h
            src/app/playback_controller.cpp
            src/app/playback_controller.h
            src/app/artwork_palette_worker.cpp
            src/app/artwork_palette_worker.h
            src/app/navigation_controller.cpp
            src/app/navigation_controller.h
            src/app/settings_controller.cpp
            src/app/settings_controller.h
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
        src/app/library_folder_projection_model.cpp
        src/app/library_folder_projection_model.h
            src/app/library_tree_store.cpp
            src/app/library_tree_store.h
        )
        target_include_directories(seriona_frontend_waveform_worker_tests
            PRIVATE src/app
        )
        target_compile_definitions(seriona_frontend_waveform_worker_tests
            PRIVATE SERIONA_HAS_BACKEND=1
        )
        target_link_libraries(seriona_frontend_waveform_worker_tests
            PRIVATE Qt6::Quick Qt6::Test Qt6::Concurrent ${SERIONA_BACKEND_CONTROL_TARGET} ${SERIONA_BACKEND_AUDIO_TARGET} ${SERIONA_BACKEND_APP_TARGET} spdlog::spdlog
        )

        add_test(NAME seriona_frontend_waveform_worker COMMAND seriona_frontend_waveform_worker_tests)
        add_test(NAME seriona_frontend_waveform_failure COMMAND seriona_frontend_waveform_worker_tests missingFileFailureReturnsEmptyWaveform)

        add_executable(seriona_frontend_current_track_lookup_tests
            tests/frontend/adapter/tst_current_track_lookup.cpp
            src/app/app_facade.cpp
            src/app/app_settings_storage.cpp
            src/app/app_settings_storage.h
            src/app/track_stats_controller.cpp
            src/app/track_stats_controller.h
            src/app/app_facade.h
            src/app/playback_controller.cpp
            src/app/playback_controller.h
            src/app/artwork_palette_worker.cpp
            src/app/artwork_palette_worker.h
            src/app/navigation_controller.cpp
            src/app/navigation_controller.h
            src/app/settings_controller.cpp
            src/app/settings_controller.h
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
        src/app/library_folder_projection_model.cpp
        src/app/library_folder_projection_model.h
            src/app/library_tree_store.cpp
            src/app/library_tree_store.h
        )
        target_include_directories(seriona_frontend_current_track_lookup_tests
            PRIVATE src/app
        )
        target_compile_definitions(seriona_frontend_current_track_lookup_tests
            PRIVATE SERIONA_HAS_BACKEND=1
        )
        target_link_libraries(seriona_frontend_current_track_lookup_tests
            PRIVATE Qt6::Quick Qt6::Test Qt6::Concurrent ${SERIONA_BACKEND_CONTROL_TARGET} ${SERIONA_BACKEND_AUDIO_TARGET} ${SERIONA_BACKEND_APP_TARGET} spdlog::spdlog
        )

        add_test(NAME seriona_frontend_current_track_lookup COMMAND seriona_frontend_current_track_lookup_tests currentTrackLookup)
        add_test(NAME seriona_frontend_current_track_cue_path COMMAND seriona_frontend_current_track_lookup_tests cuePathDoesNotDriveHighlight)

        add_executable(seriona_frontend_artwork_transition_tests
            tests/frontend/adapter/tst_artwork_transition.cpp
            src/app/playback_controller.cpp
            src/app/playback_controller.h
            src/app/settings_controller.cpp
            src/app/settings_controller.h
            src/app/app_settings_storage.cpp
            src/app/app_settings_storage.h
            src/app/artwork_palette_worker.cpp
            src/app/artwork_palette_worker.h
            src/app/backend_snapshot_mapper.cpp
            src/app/backend_snapshot_mapper.h
            src/app/backend_command_adapter.cpp
            src/app/backend_command_adapter.h
            src/app/notification_controller.cpp
            src/app/notification_controller.h
            src/app/lyrics_model.cpp
            src/app/lyrics_model.h
            src/app/library_model.cpp
            src/app/library_model.h
        src/app/library_folder_projection_model.cpp
        src/app/library_folder_projection_model.h
            src/app/library_tree_store.cpp
            src/app/library_tree_store.h
        )
        target_include_directories(seriona_frontend_artwork_transition_tests
            PRIVATE src/app
        )
        target_compile_definitions(seriona_frontend_artwork_transition_tests
            PRIVATE SERIONA_HAS_BACKEND=1
        )
        target_link_libraries(seriona_frontend_artwork_transition_tests
            PRIVATE Qt6::Quick Qt6::Test Qt6::Concurrent ${SERIONA_BACKEND_CONTROL_TARGET} ${SERIONA_BACKEND_AUDIO_TARGET} ${SERIONA_BACKEND_APP_TARGET} spdlog::spdlog
        )

        add_test(NAME seriona_frontend_artwork_transition COMMAND seriona_frontend_artwork_transition_tests artwork_transition)
        add_test(NAME seriona_frontend_artwork_fallback COMMAND seriona_frontend_artwork_transition_tests artwork_fallback)
        add_test(NAME seriona_frontend_artwork_palette_nonblocking COMMAND seriona_frontend_artwork_transition_tests artwork_palette_nonblocking)

        add_executable(seriona_frontend_lyrics_model_tests
            tests/frontend/adapter/tst_lyrics_model.cpp
            src/app/lyrics_model.cpp
            src/app/lyrics_model.h
        )
        target_include_directories(seriona_frontend_lyrics_model_tests
            PRIVATE src/app
        )
        target_compile_definitions(seriona_frontend_lyrics_model_tests
            PRIVATE SERIONA_HAS_BACKEND=1
        )
        target_link_libraries(seriona_frontend_lyrics_model_tests
            PRIVATE Qt6::Quick Qt6::Test ${SERIONA_BACKEND_CONTROL_TARGET}
        )

        add_test(NAME seriona_frontend_lyrics_trackid_lookup COMMAND seriona_frontend_lyrics_model_tests trackidLookup)
        add_test(NAME seriona_frontend_lyrics_missing_empty COMMAND seriona_frontend_lyrics_model_tests missingEmpty)
        add_test(NAME seriona_frontend_lyrics_delimiter_resolution COMMAND seriona_frontend_lyrics_model_tests delimiterResolution)

        add_executable(seriona_frontend_backend_bridge_tests
            tests/frontend/adapter/tst_backend_bridge.cpp
            src/app/backend_bridge.cpp
            src/app/backend_bridge.h
            src/app/settings_controller.cpp
            src/app/app_settings_storage.cpp
            src/app/app_settings_storage.h
            src/app/settings_controller.h
            src/app/backend_command_adapter.cpp
            src/app/backend_command_adapter.h
        )
        target_include_directories(seriona_frontend_backend_bridge_tests
            PRIVATE src/app
        )
        target_compile_definitions(seriona_frontend_backend_bridge_tests
            PRIVATE SERIONA_HAS_BACKEND=1
        )
        target_link_libraries(seriona_frontend_backend_bridge_tests
            PRIVATE Qt6::Quick Qt6::Test ${SERIONA_BACKEND_CONTROL_TARGET} ${SERIONA_BACKEND_APP_TARGET} spdlog::spdlog
        )

        add_test(NAME seriona_frontend_backend_bridge_threading COMMAND seriona_frontend_backend_bridge_tests threading)
        add_test(NAME seriona_frontend_backend_bridge_shutdown COMMAND seriona_frontend_backend_bridge_tests shutdown)
        add_test(NAME seriona_frontend_backend_bridge_shutdown_stop_sent COMMAND seriona_frontend_backend_bridge_tests shutdownStopSent)
        add_test(NAME seriona_frontend_shutdown_sequence COMMAND seriona_frontend_backend_bridge_tests shutdownSequence)
        add_test(NAME seriona_frontend_shutdown_start_failed COMMAND seriona_frontend_backend_bridge_tests shutdownStartFailed)
        add_test(NAME seriona_frontend_backend_bridge_sort_command_conversion COMMAND seriona_frontend_backend_bridge_tests applyFolderSortRulesBuildsTypedBackendCommand)
        add_test(NAME seriona_frontend_backend_bridge_sort_empty_rules COMMAND seriona_frontend_backend_bridge_tests applyFolderSortRulesAllowsEmptyRules)
        add_test(NAME seriona_frontend_backend_bridge_sort_invalid_payload COMMAND seriona_frontend_backend_bridge_tests applyFolderSortRulesRejectsInvalidPayloadWithoutDispatch)
        add_test(NAME seriona_frontend_backend_bridge_sort_missing_context COMMAND seriona_frontend_backend_bridge_tests applyFolderSortRulesRejectsMissingContextWithoutDispatch)
        add_test(NAME seriona_frontend_backend_bridge_configure_output COMMAND seriona_frontend_backend_bridge_tests submitConfigureOutputBuildsTypedBackendCommand)
        add_test(NAME seriona_frontend_backend_bridge_configure_output_invalid COMMAND seriona_frontend_backend_bridge_tests submitConfigureOutputRejectsInvalidPayloadWithoutDispatch)
        add_test(NAME seriona_frontend_backend_bridge_enumerate_devices COMMAND seriona_frontend_backend_bridge_tests enumeratePlaybackDevicesMapsDeviceIds)
        add_test(NAME seriona_frontend_backend_bridge_settings_push_on_start COMMAND seriona_frontend_backend_bridge_tests settingsPushOnStart)
        add_test(NAME seriona_frontend_backend_bridge_set_transition_config COMMAND seriona_frontend_backend_bridge_tests submitTransitionConfigBuildsTypedBackendCommand)
        add_test(NAME seriona_frontend_backend_bridge_set_transition_config_invalid COMMAND seriona_frontend_backend_bridge_tests submitTransitionConfigRejectsInvalidPayloadWithoutDispatch)
        add_test(NAME seriona_frontend_backend_bridge_transition_no_reload_while_playing COMMAND seriona_frontend_backend_bridge_tests transitionConfigWhilePlayingDoesNotReloadOrInterrupt)
        add_test(NAME seriona_frontend_backend_bridge_play_next_track COMMAND seriona_frontend_backend_bridge_tests playNextTrackBuildsQueueCommand)
        add_test(NAME seriona_frontend_backend_bridge_play_next_empty COMMAND seriona_frontend_backend_bridge_tests playNextTrackRejectsEmptyIdWithoutDispatch)
        add_test(NAME seriona_frontend_backend_bridge_remove_from_queue COMMAND seriona_frontend_backend_bridge_tests removeFromQueueBuildsIndexedCommand)

        add_executable(seriona_frontend_track_detail_tests
            tests/frontend/adapter/tst_track_detail.cpp
            src/app/app_facade.cpp
            src/app/app_settings_storage.cpp
            src/app/app_settings_storage.h
            src/app/track_stats_controller.cpp
            src/app/track_stats_controller.h
            src/app/app_facade.h
            src/app/playback_controller.cpp
            src/app/playback_controller.h
            src/app/artwork_palette_worker.cpp
            src/app/artwork_palette_worker.h
            src/app/navigation_controller.cpp
            src/app/navigation_controller.h
            src/app/settings_controller.cpp
            src/app/settings_controller.h
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
        src/app/library_folder_projection_model.cpp
        src/app/library_folder_projection_model.h
            src/app/library_tree_store.cpp
            src/app/library_tree_store.h
        )
        target_include_directories(seriona_frontend_track_detail_tests
            PRIVATE src/app
        )
        target_compile_definitions(seriona_frontend_track_detail_tests
            PRIVATE SERIONA_HAS_BACKEND=1
        )
        target_link_libraries(seriona_frontend_track_detail_tests
            PRIVATE Qt6::Quick Qt6::Test Qt6::Concurrent ${SERIONA_BACKEND_CONTROL_TARGET} ${SERIONA_BACKEND_AUDIO_TARGET} ${SERIONA_BACKEND_APP_TARGET} spdlog::spdlog
        )
        add_test(NAME seriona_frontend_track_detail COMMAND seriona_frontend_track_detail_tests trackDetailDataAssembledFromEntryAndStats)
        add_test(NAME seriona_frontend_track_detail_delete_chain COMMAND seriona_frontend_track_detail_tests deleteChainConfirmedOnceSubmitsSingleCommand)
        add_test(NAME seriona_frontend_track_detail_file_path COMMAND seriona_frontend_track_detail_tests filePathForNodeIdReturnsAbsolutePaths)
    endif()
endif()

if(SERIONA_INSTALLED_MODE)
    # Linux installed 模式安装规则：二进制 + .desktop 启动器 + hicolor 主题图标。
    # 便携版（默认）不安装任何内容，保持可移动目录语义。
    include(GNUInstallDirs)
    install(TARGETS seriona
        BUNDLE DESTINATION .
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    # .desktop 模板通过 configure_file 注入安装前缀的绝对 Exec 路径：
    # GLib 2.88+ 解析 desktop 文件时会验证 Exec 程序是否在 PATH 中（g_find_program_for_path），
    # 而 portal/systemd 服务环境 PATH 不含 ~/.local/bin，相对 Exec 会导致 GIO 加载失败，
    # Qt 的 host portal 注册报 "App info not found"。
    set(SERIONA_BINDIR "${CMAKE_INSTALL_FULL_BINDIR}")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/assets/org.kaizen857.Seriona.desktop.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/org.kaizen857.Seriona.desktop"
                   @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.kaizen857.Seriona.desktop"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
    foreach(icon_size 16 22 24 32 48 64 128 256)
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/hicolor/${icon_size}x${icon_size}/app.png"
                DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${icon_size}x${icon_size}/apps"
                RENAME "org.kaizen857.Seriona.png")
    endforeach()
    # AppStream 元数据：Flathub / GNOME Software / Discover 展示的前置资产
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/org.kaizen857.Seriona.metainfo.xml"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo")
endif()

# ---- CPack：Linux deb/rpm 打包（仅 installed 模式有意义）----
# CI 容器矩阵（debian:trixie / ubuntu:resolute / fedora / rockylinux）逐发行版原生构建：
#   deb 依赖由 dpkg-shlibdeps 自动生成（SHLIBDEPS ON，含完整 Qt Quick 栈）；
#   rpm 依赖声明 qt6-qtbase >= 6.8（RPM 符号依赖不带版本下限，需显式声明）。
# 老 LTS 发行版（Qt < 6.8）不产出 deb/rpm，由 AppImage/Flatpak 覆盖（见 docs/ 调研）。
if(UNIX AND NOT APPLE AND SERIONA_INSTALLED_MODE)
    set(CPACK_PACKAGE_NAME "seriona")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_VENDOR "kaizen857")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Seriona music player")
    set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/kaizen857/Seriona")
    set(CPACK_PACKAGE_LICENSE "GPL-3.0-or-later")
    set(CPACK_PACKAGE_FILE_NAME "seriona-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_PROCESSOR}")

    set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "kaizen857 <noreply@github.com>")
    # QML 模块（Qt5Compat.GraphicalEffects）是资源非库，SHLIBDEPS 不自动声明
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "qml6-module-qt5compat-graphicaleffects")

    set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtbase >= 6.8, qt6-qt5compat")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")

    include(CPack)
endif()
