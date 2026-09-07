#include "app_facade.h"

#include "backend_bridge.h"

#if SERIONA_HAS_BACKEND
#include "waveform_provider.h"

#include "seriona/control/control_contracts.h"
#endif

#include <QCoreApplication>
#include <QUrl>
#include <QVariant>
#include <QVariantList>

#include <array>
#include <string>

namespace Seriona::App {

namespace {

constexpr auto kBackendBridgeAutostartProperty = "seriona.backendBridgeAutostartEnabled";

bool backendBridgeAutostartEnabled()
{
    const QCoreApplication *application = QCoreApplication::instance();
    if (!application) {
        return true;
    }

    const QVariant configured = application->property(kBackendBridgeAutostartProperty);
    return configured.isValid() ? configured.toBool() : true;
}

// ConfigureOutput 的拒绝通知：前端本地校验与后端 reducer 的拒绝消息均以
// "ConfigureOutput" 开头（backend_bridge.cpp 的 invalidCommandResult 与
// control_state_reducer.cpp 的 handleConfigureOutput），其他命令的 CommandRejected
// 不以此开头，可据此区分而不误触发回退。
bool isConfigureOutputRejection(const std::string &message)
{
    return QString::fromUtf8(message.data(), static_cast<qsizetype>(message.size()))
        .startsWith(QStringLiteral("ConfigureOutput"));
}

}

#if SERIONA_HAS_BACKEND
namespace {
// 后端快照 float 数组 → QML 消费面 QVariantList（double）：EQ 曲线 181 点 / 频谱
// 60 桶走统一转换；长度由 std::array 模板形参编译期携带，double 与 QML number 直配。
template <std::size_t N>
QVariantList floatArrayToVariantList(const std::array<float, N> &values)
{
    QVariantList list;
    list.reserve(static_cast<qsizetype>(N));
    for (const float value : values) {
        list.append(static_cast<double>(value));
    }
    return list;
}
}

void AppFacade::handlePlayerSnapshotChanged(
    const seriona::control::PlayerStateSnapshot &player,
    const seriona::control::LibraryStateSnapshot &library)
{
    m_playback.applyPlayerStateSnapshot(player, &library);
    m_lyrics.applyPlayerStateSnapshot(player, &library);
    m_library.applyPlayerStateSnapshot(player, false);
    m_waveformProvider->requestForSnapshots(player, library);
}

void AppFacade::handleLibrarySnapshotChanged(
    const seriona::control::PlayerStateSnapshot &player,
    const seriona::control::LibraryStateSnapshot &library)
{
    m_library.applyLibraryStateSnapshot(library);
    m_playback.applyPlayerStateSnapshot(player, &library);
    m_lyrics.applyPlayerStateSnapshot(player, &library);
    m_library.applyPlayerStateSnapshot(player, true);
    m_waveformProvider->requestForSnapshots(player, library);
}

void AppFacade::handleEqualizerStateChanged()
{
    const seriona::audio::EqualizerStateSnapshot &snapshot = m_backendBridge->equalizerStateSnapshot();
    // 快照 → 镜像属性：curvePoints/curveFrequencies 各 181 点（double）。快照 sampleRate
    // 无前端镜像面——settings.sampleRate 是输出目标采样率（有持久化/推送语义），不能
    // 承载快照回读值，不映射（后端 reducer 快照回填实际输出率属后端 B2.6 后续）。
    m_settings.mirrorEqualizerCurve(floatArrayToVariantList(snapshot.curvePointsDb),
                                    floatArrayToVariantList(snapshot.curveFrequenciesHz));
}

void AppFacade::handleSpectrumChanged()
{
    const seriona::audio::SpectrumSnapshot &snapshot = m_backendBridge->spectrumSnapshot();
    // 频谱推送面 R2 已接线（后端 SpectrumUpdated 事件 → 控制器驻留槽 → 订阅回调）；
    // 快照按契约 60 桶落地（generation=0 的默认空快照同样按契约镜像，UI 空态由
    // EqualizerWindow 呈现）。更新频率 = 后端发布频率，此处不节流不放大。
    m_settings.mirrorSpectrumBins(floatArrayToVariantList(snapshot.binsDb));
}
#endif

AppFacade::AppFacade(QObject *parent)
    : QObject(parent)
    , m_playback(this)
    , m_library(this)
    , m_lyrics(this)
    , m_notifications(this)
    , m_navigation(this)
    , m_trackStats(this)
    , m_backendBridge(std::make_unique<BackendBridge>(this))
#if SERIONA_HAS_BACKEND
    , m_waveformProvider(std::make_unique<WaveformProvider>(this))
#endif
{
    // 播放次数计数点（T16）：新曲目开始播放（轨道切换/PlaybackEnded 后续播）自增。
    m_playback.setTrackStartedHandler([this](const QString &trackId) {
        m_trackStats.recordPlayback(trackId);
    });
#if SERIONA_HAS_BACKEND
    m_playback.setCommandExecutor([this](const seriona::control::MediaControlCommand &command) {
        return m_backendBridge->submitCommand(command);
    });
    m_library.setCommandExecutor([this](const seriona::control::MediaControlCommand &command) {
        return m_backendBridge->submitCommand(command);
    });
    m_library.setFolderSortExecutor([this](const QString &rootPath, const QString &folderNodeId, const QVariantList &rules) {
        return m_backendBridge->applyFolderSortRules(rootPath, folderNodeId, rules);
    });
    m_library.setScanExecutor([this](const QString &rootPath, seriona::scanner::ScanMode mode) {
        return m_backendBridge->scanLibrary(rootPath, mode);
    });
    connect(m_waveformProvider.get(), &WaveformProvider::waveformReady, this, [this](const WaveformResult &result) {
        if (m_shuttingDown) {
            return;
        }
        m_playback.applyWaveform(result.heights, result.barWidth);
    });
    connect(m_backendBridge.get(), &BackendBridge::playerSnapshotChanged, this, [this] {
        const seriona::control::PlayerStateSnapshot &player = m_backendBridge->playerSnapshot();
        const seriona::control::LibraryStateSnapshot &library = m_backendBridge->librarySnapshot();
        handlePlayerSnapshotChanged(player, library);
    });
    connect(m_backendBridge.get(), &BackendBridge::librarySnapshotChanged, this, [this] {
        const seriona::control::PlayerStateSnapshot &player = m_backendBridge->playerSnapshot();
        const seriona::control::LibraryStateSnapshot &library = m_backendBridge->librarySnapshot();
        handleLibrarySnapshotChanged(player, library);
    });
    // F1.3：均衡器/频谱订阅 → SettingsController 镜像属性（curvePoints/curveFrequencies/
    // spectrumBins）。shutdown 竞态守卫同 waveform 先例（bridge 自身 shutdown 守卫外再兜底）。
    connect(m_backendBridge.get(), &BackendBridge::equalizerStateChanged, this, [this] {
        if (m_shuttingDown) {
            return;
        }
        handleEqualizerStateChanged();
    });
    connect(m_backendBridge.get(), &BackendBridge::spectrumChanged, this, [this] {
        if (m_shuttingDown) {
            return;
        }
        handleSpectrumChanged();
    });
    connect(m_backendBridge.get(), &BackendBridge::domainNotificationQueued, this, [this] {
        const auto &notifications = m_backendBridge->notifications();
        if (notifications.empty()) {
            return;
        }
        const seriona::control::ControlDomainNotification &notification = notifications.back();
        if (notification.kind == seriona::control::ControlDomainNotificationKind::FolderSortRulesApplied
            && notification.folderSortSetting.has_value()) {
            m_library.applyFolderSortSetting(*notification.folderSortSetting);
        }
        if (notification.kind == seriona::control::ControlDomainNotificationKind::CommandRejected
            && isConfigureOutputRejection(notification.message)) {
            m_settings.rollbackRejectedOutputConfig();
        }
        m_notifications.enqueueDomainNotification(notification);
    });
    m_settings.setApplyOutputConfigExecutor([this](int outputMode, int sampleRate, int sampleFormat, int bufferDurationMs, const QString &preferredDeviceId) {
        return m_backendBridge->submitConfigureOutput(outputMode, sampleRate, sampleFormat, bufferDurationMs, preferredDeviceId);
    });
    // 播放过渡组推送（T12）：SettingsController 内部完成 400ms 去抖（滑块）与立即推送
    // （档位/开关），此处仅透传到 BackendBridge 的 SetTransitionConfig 组包/校验。
    m_settings.setApplyTransitionConfigExecutor([this](int autoAdvanceFadeMode, bool fadeOnTransport, bool fadeOnSeek,
                                                       int gaplessPreloadMs, int crossfadeMs, int transportFadeMs,
                                                       int seekFadeMs, int manualAdvanceFadeMode, int manualShortCrossfadeMs) {
        return m_backendBridge->submitTransitionConfig(autoAdvanceFadeMode, fadeOnTransport, fadeOnSeek, gaplessPreloadMs,
                                                       crossfadeMs, transportFadeMs, seekFadeMs, manualAdvanceFadeMode,
                                                       manualShortCrossfadeMs);
    });
    // 均衡器组推送（F1.3）：SettingsController 内部完成 50ms 去抖（连续控件）与立即
    // 推送（离散控件/预设/复位），此处仅透传 BackendBridge 的 SetEqualizerConfig
    // 组包/校验（6 参全量，spectrum 拆分与真命令落点见 backend_bridge.h
    // submitEqualizerConfig 注释——EQ 走 SetEqualizerConfig，spectrum 位变化
    // 独立外发 SetSpectrumEnabled）。
    // apply() 耦合评估（F1.2 review 观察 ②）：output 离散 setter/去抖到期均经 apply()
    // → EQ executor 绑定后这些路径附带重推一次相同 EQ 载荷；后端 reducer 收等值命令
    // 仅 generation++，无听感影响；EQ 段放 apply() 系计划裁定（启动同步一次），与
    // transition 注入点同为独立通道透传同构——接受此耦合，不引入第三通道。
    m_settings.setApplyEqualizerConfigExecutor([this](bool enabled, int bandMode, double preGainDb,
                                                       const QVariantList &bandGains, bool limiterEnabled,
                                                       bool spectrumEnabled) {
        return m_backendBridge->submitEqualizerConfig(enabled, bandMode, preGainDb, bandGains,
                                                       limiterEnabled, spectrumEnabled);
    });
    m_settings.setEnumerateDevicesExecutor([this] {
        return m_backendBridge->enumeratePlaybackDeviceCapabilities();
    });
    m_settings.setLogLevelExecutor([this](int level) {
        return m_backendBridge->setLogLevel(level);
    });
    // 应用设置存储：接入后端时经 BackendBridge → MediaController 键值存储；
    // mock-only 不注入，各控制器回退内存存储（进程内有效）。
    const AppSettingsBackend settingsBackend{
        .read = [this](const QString &group, const QString &key, const QVariant &defaultValue) {
            return m_backendBridge->getAppSetting(group, key, defaultValue);
        },
        .write = [this](const QString &group, const QString &key, const QVariant &value) {
            m_backendBridge->setAppSetting(group, key, value);
        },
        .remove = [this](const QString &group, const QString &key) {
            m_backendBridge->removeAppSetting(group, key);
        },
    };
    m_settings.setSettingsStorageBackend(settingsBackend);
    m_navigation.setSettingsStorageBackend(settingsBackend);
    m_trackStats.setSettingsStorageBackend(settingsBackend);
    // 启动路径：bridge 就绪（startedChanged 且 started() 为真）后推送一次持久化配置；
    // shutdown 也会发 startedChanged（started()==false），必须跳过。
    connect(m_backendBridge.get(), &BackendBridge::startedChanged, this, [this] {
        if (!m_backendBridge->started()) {
            return;
        }
        m_settings.reloadFromSettings();
        m_lyrics.setLyricDelimiters(m_settings.lyricDelimiters());
        m_settings.apply();
        // 播放过渡组随输出组在启动/重连后 apply 一次（含持久化的 9 键，仿 m_settings.apply()）
        m_settings.applyTransitionConfig();
        // 持久化的日志等级在启动时同步到后端（initializeApplicationLogging 默认之后覆盖）
        m_settings.applyLogLevel();
    });
#endif

    // 歌词分隔符联动：设置变化立即同步到 LyricsModel（歌词解析立即生效）。
    connect(&m_settings, &SettingsController::lyricDelimitersChanged, this, [this] {
        m_lyrics.setLyricDelimiters(m_settings.lyricDelimiters());
    });

    if (backendBridgeAutostartEnabled()) {
        m_backendBridge->start();
    }

    if (QCoreApplication *application = QCoreApplication::instance()) {
        connect(application, &QCoreApplication::aboutToQuit, this, &AppFacade::shutdown, Qt::DirectConnection);
    }
}

AppFacade::~AppFacade()
{
    shutdown();
}

QString AppFacade::layerName() const
{
    return QStringLiteral("Seriona C++ Middle Layer");
}

bool AppFacade::foundationReady() const
{
    return true;
}

PlaybackController *AppFacade::playback()
{
    return &m_playback;
}

LibraryController *AppFacade::library()
{
    return &m_library;
}

LyricsModel *AppFacade::lyrics()
{
    return &m_lyrics;
}

NotificationController *AppFacade::notifications()
{
    return &m_notifications;
}

NavigationController *AppFacade::navigation()
{
    return &m_navigation;
}

SettingsController *AppFacade::settings()
{
    return &m_settings;
}

TrackStatsController *AppFacade::trackStats()
{
    return &m_trackStats;
}

bool AppFacade::deleteTarget(const QString &path, bool folder)
{
#if SERIONA_HAS_BACKEND
    if (m_shuttingDown) {
        return false;
    }
    const seriona::control::MediaControllerCommandResult result = m_backendBridge->deleteTarget(path, folder);
    if (result.accepted) {
        m_notifications.showInfo(folder ? tr("删除文件夹") : tr("删除歌曲"),
                                 folder ? tr("文件夹已从磁盘删除，不可恢复") : tr("歌曲已从磁盘删除，不可恢复"));
    }
    return result.accepted;
#else
    m_notifications.showUnsupportedAction(folder ? tr("删除文件夹") : tr("删除歌曲"));
    return false;
#endif
}

bool AppFacade::playNextTrack(const QString &trackId)
{
#if SERIONA_HAS_BACKEND
    if (m_shuttingDown) {
        return false;
    }
    const seriona::control::MediaControllerCommandResult result = m_backendBridge->playNextTrack(trackId);
    if (result.accepted) {
        m_notifications.showInfo(tr("已添加到下一首播放"), tr("该曲目将在当前歌曲结束后播放"));
    }
    return result.accepted;
#else
    m_notifications.showUnsupportedAction(tr("添加到下一首播放"));
    return false;
#endif
}

bool AppFacade::removeFromQueue(quint64 queueIndex)
{
#if SERIONA_HAS_BACKEND
    if (m_shuttingDown) {
        return false;
    }
    return m_backendBridge->removeFromQueue(queueIndex).accepted;
#else
    m_notifications.showUnsupportedAction(tr("从队列移除"));
    return false;
#endif
}

QString AppFacade::filePathForNodeId(const QString &nodeId)
{
    return m_library.model()->absoluteFilePathForNode(nodeId);
}

bool AppFacade::backendBridgeStartedForTests() const
{
    return m_backendBridge->started();
}

std::size_t AppFacade::backendNotificationCountForTests() const
{
#if SERIONA_HAS_BACKEND
    return m_backendBridge->notifications().size();
#else
    return 0U;
#endif
}

bool AppFacade::scanLibrary(const QUrl &rootUrl)
{
    return m_navigation.scanLibrary(m_library, rootUrl);
}

bool AppFacade::restorePlaylistFromStartup()
{
    return m_navigation.restorePlaylistFromStartup(m_library, [this](const QUrl &rootUrl) {
#if SERIONA_HAS_BACKEND
        return m_library.scanLibrary(rootUrl, seriona::scanner::ScanMode::Incremental);
#else
        return scanLibrary(rootUrl);
#endif
    });
}

#if SERIONA_HAS_BACKEND
void AppFacade::applyPlayerSnapshotForTests(
    const seriona::control::PlayerStateSnapshot &player,
    const seriona::control::LibraryStateSnapshot &library)
{
    handlePlayerSnapshotChanged(player, library);
}

void AppFacade::applyLibrarySnapshotForTests(
    const seriona::control::PlayerStateSnapshot &player,
    const seriona::control::LibraryStateSnapshot &library)
{
    handleLibrarySnapshotChanged(player, library);
}
#endif

void AppFacade::shutdown()
{
    if (m_shuttingDown) {
        return;
    }

    m_shuttingDown = true;
#if SERIONA_HAS_BACKEND
    if (m_waveformProvider) {
        m_waveformProvider->cancelPending();
    }
#endif
    if (m_backendBridge) {
        m_backendBridge->shutdown();
    }
}

}
