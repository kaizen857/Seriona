#include "backend_bridge.h"

#if SERIONA_HAS_BACKEND
#include "backend_command_adapter.h"
#include "path_text.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMetaObject>
#include <QPair>
#include <QList>
#include <QPointer>
#include <QVariantMap>

#include "seriona/app/runtime_paths.h"
#include "seriona/app/application_logging.h"

#include <spdlog/common.h>
#include <spdlog/spdlog.h>

#include <exception>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#endif

namespace Seriona::App {

namespace {

#if SERIONA_HAS_BACKEND
constexpr qsizetype kMaxQueuedNotifications = 64;

std::string toBackendString(const QString &value)
{
    const QByteArray utf8 = value.toUtf8();
    return std::string(utf8.constData(), static_cast<std::size_t>(utf8.size()));
}

std::filesystem::path toBackendPath(const QString &path)
{
    return pathFromUtf8(toBackendString(path));
}

QString normalizedRootPath(const QString &rootPath)
{
    const QString trimmed = rootPath.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }
    return QDir::cleanPath(QFileInfo(trimmed).absoluteFilePath());
}

seriona::control::MediaControllerCommandResult invalidCommandResult(std::string message)
{
    seriona::control::MediaControllerCommandResult result;
    result.accepted = false;
    result.code = seriona::control::MediaControllerErrorCode::InvalidCommand;
    result.message = std::move(message);
    return result;
}

std::optional<seriona::control::FolderSortField> sortFieldFromPayload(const QString &field)
{
    const QString trimmed = field.trimmed();
    if (trimmed.compare(QStringLiteral("title"), Qt::CaseInsensitive) == 0) {
        return seriona::control::FolderSortField::Title;
    }
    if (trimmed.compare(QStringLiteral("artist"), Qt::CaseInsensitive) == 0) {
        return seriona::control::FolderSortField::Artist;
    }
    if (trimmed.compare(QStringLiteral("album"), Qt::CaseInsensitive) == 0) {
        return seriona::control::FolderSortField::Album;
    }
    if (trimmed.compare(QStringLiteral("filename"), Qt::CaseInsensitive) == 0) {
        return seriona::control::FolderSortField::Filename;
    }
    if (trimmed.compare(QStringLiteral("year"), Qt::CaseInsensitive) == 0) {
        return seriona::control::FolderSortField::Year;
    }
    if (trimmed.compare(QStringLiteral("duration"), Qt::CaseInsensitive) == 0) {
        return seriona::control::FolderSortField::Duration;
    }
    if (trimmed.compare(QStringLiteral("createdDate"), Qt::CaseInsensitive) == 0) {
        return seriona::control::FolderSortField::CreatedDate;
    }
    if (trimmed.compare(QStringLiteral("discNumber"), Qt::CaseInsensitive) == 0) {
        return seriona::control::FolderSortField::DiscNumber;
    }
    if (trimmed.compare(QStringLiteral("trackNumber"), Qt::CaseInsensitive) == 0) {
        return seriona::control::FolderSortField::TrackNumber;
    }
    return std::nullopt;
}

std::optional<seriona::control::FolderSortDirection> sortDirectionFromPayload(const QString &order)
{
    const QString trimmed = order.trimmed();
    if (trimmed.compare(QStringLiteral("asc"), Qt::CaseInsensitive) == 0) {
        return seriona::control::FolderSortDirection::Ascending;
    }
    if (trimmed.compare(QStringLiteral("desc"), Qt::CaseInsensitive) == 0) {
        return seriona::control::FolderSortDirection::Descending;
    }
    return std::nullopt;
}

std::optional<seriona::control::FolderSortRule> sortRuleFromPayload(const QVariant &ruleVar, std::string &error)
{
    if (!ruleVar.canConvert<QVariantMap>()) {
        error = "Malformed sort rule payload";
        return std::nullopt;
    }

    const QVariantMap ruleMap = ruleVar.toMap();
    const std::optional<seriona::control::FolderSortField> field = sortFieldFromPayload(
        ruleMap.value(QStringLiteral("field")).toString());
    if (!field.has_value()) {
        error = "Invalid sort field";
        return std::nullopt;
    }

    const std::optional<seriona::control::FolderSortDirection> direction = sortDirectionFromPayload(
        ruleMap.value(QStringLiteral("order")).toString());
    if (!direction.has_value()) {
        error = "Invalid sort direction";
        return std::nullopt;
    }

    seriona::control::FolderSortRule rule;
    rule.field = *field;
    rule.direction = *direction;
    rule.missingValuePolicy = seriona::control::FolderSortMissingValuePolicy::Last;
    return rule;
}

std::optional<std::vector<seriona::control::FolderSortRule>> sortRulesFromPayload(const QVariantList &rules, std::string &error)
{
    std::vector<seriona::control::FolderSortRule> parsedRules;
    parsedRules.reserve(static_cast<std::size_t>(rules.size()));
    for (const QVariant &ruleVar : rules) {
        std::optional<seriona::control::FolderSortRule> parsedRule = sortRuleFromPayload(ruleVar, error);
        if (!parsedRule.has_value()) {
            return std::nullopt;
        }
        parsedRules.push_back(*parsedRule);
    }
    return parsedRules;
}
#endif

}

#if SERIONA_HAS_BACKEND
BackendBridge::BackendBridge(ControllerFactory controllerFactory, QObject *parent)
    : QObject(parent)
    , m_controllerFactory(std::move(controllerFactory))
{
}
#endif

BackendBridge::BackendBridge(QObject *parent)
    : QObject(parent)
#if SERIONA_HAS_BACKEND
    , m_controllerFactory(defaultControllerFactory())
#endif
{
}

BackendBridge::~BackendBridge()
{
    shutdown();
}

void BackendBridge::start()
{
    if (m_started) {
        return;
    }

    m_shuttingDown = false;
#if SERIONA_HAS_BACKEND
    try {
        if (!m_controller) {
            m_controller = m_controllerFactory();
        }
        if (!m_controller) {
            return;
        }

        registerSubscriptions();
        m_controller->start();
    } catch (...) {
        m_shuttingDown = true;
        unsubscribeAll();
        if (m_controller) {
            m_controller->shutdown();
            m_controller.reset();
        }
        return;
    }
#endif
    m_started = true;
    emit startedChanged();
}

void BackendBridge::shutdown()
{
#if SERIONA_HAS_BACKEND
    const bool hasController = static_cast<bool>(m_controller);
#else
    const bool hasController = false;
#endif
    if (!m_started && !hasController) {
        return;
    }

#if SERIONA_HAS_BACKEND
    if (m_started) {
        submitShutdownStop();
    }
#endif
    m_shuttingDown = true;
#if SERIONA_HAS_BACKEND
    unsubscribeAll();
    if (m_controller) {
        m_controller->shutdown();
        m_controller.reset();
    }
#endif
    if (m_started) {
        m_started = false;
        emit startedChanged();
    }
    emit shutdownCompleted();
}

bool BackendBridge::started() const
{
    return m_started;
}

bool BackendBridge::shuttingDown() const
{
    return m_shuttingDown;
}

void BackendBridge::drainForTests()
{
#if SERIONA_HAS_BACKEND
    if (m_controller) {
        m_controller->drainForTests();
    }
#endif
}

#if SERIONA_HAS_BACKEND
seriona::control::MediaControllerCommandResult BackendBridge::submitCommand(const seriona::control::MediaControlCommand &command)
{
    if (m_shuttingDown || !m_controller) {
        seriona::control::MediaControllerCommandResult result = controllerStoppedResult();
        enqueueCommandFailureNotification(result);
        return result;
    }

    seriona::control::MediaControllerCommandResult result = m_controller->submitCommand(command);
    enqueueCommandFailureNotification(result);
    return result;
}

seriona::control::MediaControllerCommandResult BackendBridge::scanLibrary(const QString &rootPath, seriona::scanner::ScanMode mode)
{
    if (m_shuttingDown || !m_controller) {
        seriona::control::MediaControllerCommandResult result = controllerStoppedResult();
        enqueueCommandFailureNotification(result);
        return result;
    }

    seriona::scanner::ScannerRoot root;
    root.path = pathFromUtf8(rootPath.toStdString());
    root.recursive = true;
    seriona::control::MediaControllerCommandResult result = m_controller->scanLibrary({std::move(root)}, mode);
    enqueueCommandFailureNotification(result);
    return result;
}

seriona::control::MediaControllerCommandResult BackendBridge::applyFolderSortRules(
    const QString &rootPath,
    const QString &folderNodeId,
    const QVariantList &rules)
{
    const QString normalizedRoot = normalizedRootPath(rootPath);
    if (normalizedRoot.isEmpty()) {
        seriona::control::MediaControllerCommandResult result = invalidCommandResult("Folder sort command requires a root path");
        enqueueCommandFailureNotification(result);
        return result;
    }

    const QString normalizedFolderNodeId = folderNodeId.trimmed();
    if (normalizedFolderNodeId.isEmpty()) {
        seriona::control::MediaControllerCommandResult result = invalidCommandResult("Folder sort command requires a folder node id");
        enqueueCommandFailureNotification(result);
        return result;
    }

    std::string error;
    std::optional<std::vector<seriona::control::FolderSortRule>> parsedRules = sortRulesFromPayload(rules, error);
    if (!parsedRules.has_value()) {
        seriona::control::MediaControllerCommandResult result = invalidCommandResult(std::move(error));
        enqueueCommandFailureNotification(result);
        return result;
    }

    seriona::control::FolderSortSetting setting;
    setting.rootPath = toBackendPath(normalizedRoot);
    setting.folderNodeId = toBackendString(normalizedFolderNodeId);
    setting.rules = std::move(*parsedRules);

    seriona::control::MediaControlCommand command;
    command.kind = seriona::control::MediaControlCommandKind::ApplyFolderSortRules;
    command.folderSortSetting = std::move(setting);
    return submitCommand(command);
}

seriona::control::MediaControllerCommandResult BackendBridge::submitConfigureOutput(
    int outputMode,
    int sampleRate,
    int sampleFormat,
    int bufferDurationMs,
    const QString &preferredDeviceId)
{
    if (outputMode != 0 && outputMode != 1) {
        seriona::control::MediaControllerCommandResult result = invalidCommandResult(
            "ConfigureOutput requires outputMode 0 (Direct) or 1 (Mixed)");
        enqueueCommandFailureNotification(result);
        return result;
    }
    if (sampleRate != 0 && (sampleRate < 8000 || sampleRate > 768000)) {
        seriona::control::MediaControllerCommandResult result = invalidCommandResult(
            "ConfigureOutput sample rate is out of range (8000-768000)");
        enqueueCommandFailureNotification(result);
        return result;
    }
    if (sampleFormat != 0 && sampleFormat != 1 && sampleFormat != 2 && sampleFormat != 3 && sampleFormat != 4) {
        seriona::control::MediaControllerCommandResult result = invalidCommandResult(
            "ConfigureOutput sample format must be 0 (device default), 1 (Int16), 2 (Int24), 3 (Int32), or 4 (Float32)");
        enqueueCommandFailureNotification(result);
        return result;
    }
    if (bufferDurationMs < 50 || bufferDurationMs > 1000) {
        seriona::control::MediaControllerCommandResult result = invalidCommandResult(
            "ConfigureOutput buffer duration is out of range (50-1000 ms)");
        enqueueCommandFailureNotification(result);
        return result;
    }

    seriona::audio::AudioOutputConfig config;
    config.outputMode = (outputMode == 0) ? seriona::audio::AudioOutputMode::Direct
                                          : seriona::audio::AudioOutputMode::Mixed;
    if (sampleRate > 0) {
        config.targetSampleRate = static_cast<std::uint32_t>(sampleRate);
    }
    if (sampleFormat > 0) {
        config.targetSampleFormat = static_cast<seriona::audio::AudioSampleFormat>(sampleFormat);
    }
    config.bufferDuration = std::chrono::milliseconds(bufferDurationMs);
    config.preferredDeviceId = toBackendString(preferredDeviceId);

    seriona::control::MediaControlCommand command;
    command.kind = seriona::control::MediaControlCommandKind::ConfigureOutput;
    command.outputConfig = std::move(config);
    return submitCommand(command);
}

seriona::control::MediaControllerCommandResult BackendBridge::submitTransitionConfig(
    int autoAdvanceFadeMode,
    bool fadeOnTransport,
    bool fadeOnSeek,
    int gaplessPreloadMs,
    int crossfadeMs,
    int transportFadeMs,
    int seekFadeMs,
    int manualAdvanceFadeMode,
    int manualShortCrossfadeMs)
{
    // 数值域与后端 reducer handleSetTransitionConfig 逐条一致（读到即镜像）：
    // 两枚枚举 int ∈ [0,2]；crossfadeMs [0,10000]；transportFadeMs/seekFadeMs/
    // manualShortCrossfadeMs [0,3000]；gaplessPreloadMs [0,5000]；负值一并拒绝。
    auto reject = [this](const char *message) {
        seriona::control::MediaControllerCommandResult result = invalidCommandResult(message);
        spdlog::warn("SetTransitionConfig rejected locally: {}", message);
        enqueueCommandFailureNotification(result);
        return result;
    };
    if (autoAdvanceFadeMode < 0 || autoAdvanceFadeMode > 2) {
        return reject("SetTransitionConfig auto advance fade mode is out of range (0-2)");
    }
    if (manualAdvanceFadeMode < 0 || manualAdvanceFadeMode > 2) {
        return reject("SetTransitionConfig manual advance fade mode is out of range (0-2)");
    }
    if (crossfadeMs < 0 || crossfadeMs > 10000) {
        return reject("SetTransitionConfig crossfade length is out of range (0-10000 ms)");
    }
    if (transportFadeMs < 0 || transportFadeMs > 3000) {
        return reject("SetTransitionConfig transport fade length is out of range (0-3000 ms)");
    }
    if (seekFadeMs < 0 || seekFadeMs > 3000) {
        return reject("SetTransitionConfig seek fade length is out of range (0-3000 ms)");
    }
    if (manualShortCrossfadeMs < 0 || manualShortCrossfadeMs > 3000) {
        return reject("SetTransitionConfig manual short crossfade length is out of range (0-3000 ms)");
    }
    if (gaplessPreloadMs < 0 || gaplessPreloadMs > 5000) {
        return reject("SetTransitionConfig gapless preload lead is out of range (0-5000 ms)");
    }

    // 组包顺序 = TransitionConfig 字段声明顺序 = 后端 reducer 解析顺序（跨端契约）。
    seriona::audio::TransitionConfig config;
    config.autoAdvanceFadeMode = static_cast<seriona::audio::AutoAdvanceFadeMode>(autoAdvanceFadeMode);
    config.fadeOnTransport = fadeOnTransport;
    config.fadeOnSeek = fadeOnSeek;
    config.gaplessPreloadMs = std::chrono::milliseconds(gaplessPreloadMs);
    config.crossfadeMs = std::chrono::milliseconds(crossfadeMs);
    config.transportFadeMs = std::chrono::milliseconds(transportFadeMs);
    config.seekFadeMs = std::chrono::milliseconds(seekFadeMs);
    config.manualAdvanceFadeMode = static_cast<seriona::audio::ManualAdvanceFadeMode>(manualAdvanceFadeMode);
    config.manualShortCrossfadeMs = std::chrono::milliseconds(manualShortCrossfadeMs);

    // 与 ConfigureOutput 同通道：submitCommand 单意图外发；后端据此仅转发配置，
    // 不产生任何重载/切歌尾意图（无 LoadTrack/设备重开副作用）。
    seriona::control::MediaControlCommand command;
    command.kind = seriona::control::MediaControlCommandKind::SetTransitionConfig;
    command.transitionConfig = std::move(config);
    return submitCommand(command);
}

seriona::control::MediaControllerCommandResult BackendBridge::deleteTarget(const QString &path, bool folder)
{
    const QString normalized = path.trimmed();
    if (normalized.isEmpty()) {
        seriona::control::MediaControllerCommandResult result = invalidCommandResult(
            "DeleteTrack/DeleteFolder requires a target path");
        enqueueCommandFailureNotification(result);
        return result;
    }

    seriona::control::MediaControlCommand command;
    command.kind = folder ? seriona::control::MediaControlCommandKind::DeleteFolder
                          : seriona::control::MediaControlCommandKind::DeleteTrack;
    command.targetPath = toBackendPath(normalized);
    return submitCommand(command);
}

seriona::control::MediaControllerCommandResult BackendBridge::playNextTrack(const QString &trackId)
{
    const QString normalized = trackId.trimmed();
    if (normalized.isEmpty()) {
        seriona::control::MediaControllerCommandResult result = invalidCommandResult(
            "PlayNextTrack requires a track id");
        enqueueCommandFailureNotification(result);
        return result;
    }

    seriona::control::MediaControlCommand command;
    command.kind = seriona::control::MediaControlCommandKind::PlayNextTrack;
    seriona::control::TrackIdentity identity;
    identity.trackId = toBackendString(normalized);
    command.track = std::move(identity);
    return submitCommand(command);
}

seriona::control::MediaControllerCommandResult BackendBridge::removeFromQueue(quint64 queueIndex)
{
    seriona::control::MediaControlCommand command;
    command.kind = seriona::control::MediaControlCommandKind::RemoveFromQueue;
    command.queueIndex = static_cast<std::size_t>(queueIndex);
    return submitCommand(command);
}

QList<QPair<QString, QString>> BackendBridge::enumeratePlaybackDevices()
{
    const QList<PlaybackDeviceCapabilities> devices = enumeratePlaybackDeviceCapabilities();
    QList<QPair<QString, QString>> devicePairs;
    devicePairs.reserve(devices.size());
    for (const PlaybackDeviceCapabilities &device : devices) {
        devicePairs.append({device.deviceId, device.deviceName});
    }
    return devicePairs;
}

QList<PlaybackDeviceCapabilities> BackendBridge::enumeratePlaybackDeviceCapabilities()
{
    if (m_shuttingDown || !m_controller) {
        return {};
    }

    const std::vector<seriona::audio::AudioDeviceFormat> devices = m_controller->enumeratePlaybackDevices();    QList<PlaybackDeviceCapabilities> capabilities;
    capabilities.reserve(static_cast<qsizetype>(devices.size()));
    for (const seriona::audio::AudioDeviceFormat &device : devices) {
        PlaybackDeviceCapabilities caps;
        caps.deviceId = QString::fromStdString(device.deviceId);
        if (caps.deviceId.isEmpty()) {
            continue;
        }
        caps.deviceName = QString::fromStdString(device.deviceName);
        if (caps.deviceName.isEmpty()) {
            caps.deviceName = caps.deviceId;
        }
        caps.sampleFormats.reserve(static_cast<qsizetype>(device.supportedSampleFormats.size()));
        for (const seriona::audio::AudioSampleFormat format : device.supportedSampleFormats) {
            caps.sampleFormats.append(static_cast<int>(format));
        }
        caps.sampleRates.reserve(static_cast<qsizetype>(device.supportedSampleRates.size()));
        for (const std::uint32_t rate : device.supportedSampleRates) {
            caps.sampleRates.append(static_cast<int>(rate));
        }
        capabilities.append(std::move(caps));
    }
    return capabilities;
}

void BackendBridge::setLogLevel(int level)
{
    // 值域防御在前端 settings_controller 完成（仅 [trace, critical]）；此处仍做
    // 转换前检查，避免越界 int 直接静态转换破坏 spdlog 的 should_log 比较。
    const auto asEnum = static_cast<spdlog::level::level_enum>(level);
    if (asEnum < spdlog::level::trace || asEnum > spdlog::level::off) {
        return;
    }
    seriona::app::setLogLevel(asEnum);
}

std::optional<QVariant> BackendBridge::getAppSetting(const QString &group, const QString &key, const QVariant &defaultValue)
{
    if (m_shuttingDown || !m_controller) {
        return std::nullopt;
    }
    const std::optional<std::string> stored = m_controller->getAppSetting(toBackendString(group), toBackendString(key));
    if (!stored.has_value() || stored->empty()) {
        return std::nullopt;
    }
    const QByteArray utf8(stored->data(), static_cast<qsizetype>(stored->size()));
    const QJsonDocument document = QJsonDocument::fromJson(utf8);
    if (document.isObject()) {
        return document.object().value(QStringLiteral("v")).toVariant();
    }
    return std::nullopt;
}

bool BackendBridge::setAppSetting(const QString &group, const QString &key, const QVariant &value)
{
    if (m_shuttingDown || !m_controller) {
        return false;
    }
    const QJsonObject envelope{{QStringLiteral("v"), QJsonValue::fromVariant(value)}};
    const QByteArray utf8 = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
    const seriona::control::MediaControllerCommandResult result =
        m_controller->setAppSetting(toBackendString(group),
                                    toBackendString(key),
                                    std::string(utf8.constData(), static_cast<std::size_t>(utf8.size())));
    return result.accepted;
}

bool BackendBridge::removeAppSetting(const QString &group, const QString &key)
{
    if (m_shuttingDown || !m_controller) {
        return false;
    }
    const seriona::control::MediaControllerCommandResult result =
        m_controller->removeAppSetting(toBackendString(group), toBackendString(key));
    return result.accepted;
}

const seriona::control::PlayerStateSnapshot &BackendBridge::playerSnapshot() const
{
    return m_playerSnapshot;
}

const seriona::control::LibraryStateSnapshot &BackendBridge::librarySnapshot() const
{
    return m_librarySnapshot;
}

const std::deque<seriona::control::ControlDomainNotification> &BackendBridge::notifications() const
{
    return m_notifications;
}

BackendBridge::ControllerFactory BackendBridge::defaultControllerFactory()
{
    return [] {
#if SERIONA_HAS_BACKEND
        const auto exePath = QCoreApplication::applicationFilePath().toStdString();
        const auto runtimePaths = seriona::app::resolveRuntimePaths(pathFromUtf8(exePath));
        runtimePaths.ensureDirectoriesExist();
        const auto &[dataRoot, logFile, mediaStorePath, artworkDir] = runtimePaths;
        static_cast<void>(dataRoot);
        static_cast<void>(logFile);
        return seriona::control::makeProductionMediaController(
            seriona::control::MediaControllerOptions{},
            mediaStorePath,
            artworkDir);
#else
        return nullptr;
#endif
    };
}

seriona::control::MediaControllerCommandResult BackendBridge::controllerStoppedResult()
{
    seriona::control::MediaControllerCommandResult result;
    result.accepted = false;
    result.code = seriona::control::MediaControllerErrorCode::ControllerStopped;
    result.message = "Media controller is stopped";
    return result;
}

void BackendBridge::unsubscribe(seriona::control::SubscriptionHandle &handle)
{
    if (handle.unsubscribe) {
        handle.unsubscribe();
        handle = {};
    }
}

void BackendBridge::registerSubscriptions()
{
    const QPointer<BackendBridge> receiver(this);
    m_playerSubscription = m_controller->subscribePlayerState([receiver](seriona::control::PlayerStateSnapshot snapshot) mutable {
        if (receiver.isNull()) {
            return;
        }

        QMetaObject::invokeMethod(receiver.data(), [receiver, snapshot = std::move(snapshot)]() mutable {
            if (receiver.isNull() || receiver->m_shuttingDown) {
                return;
            }
            receiver->applyPlayerSnapshot(std::move(snapshot));
        }, Qt::QueuedConnection);
    });
    m_librarySubscription = m_controller->subscribeLibraryState([receiver](seriona::control::LibraryStateSnapshot snapshot) mutable {
        if (receiver.isNull()) {
            return;
        }

        QMetaObject::invokeMethod(receiver.data(), [receiver, snapshot = std::move(snapshot)]() mutable {
            if (receiver.isNull() || receiver->m_shuttingDown) {
                return;
            }
            receiver->applyLibrarySnapshot(std::move(snapshot));
        }, Qt::QueuedConnection);
    });
    m_notificationSubscription = m_controller->subscribeDomainNotifications([receiver](seriona::control::ControlDomainNotification notification) mutable {
        if (receiver.isNull()) {
            return;
        }

        QMetaObject::invokeMethod(receiver.data(), [receiver, notification = std::move(notification)]() mutable {
            if (receiver.isNull() || receiver->m_shuttingDown) {
                return;
            }
            receiver->enqueueNotification(std::move(notification));
        }, Qt::QueuedConnection);
    });
}

void BackendBridge::unsubscribeAll()
{
    unsubscribe(m_playerSubscription);
    unsubscribe(m_librarySubscription);
    unsubscribe(m_notificationSubscription);
}

void BackendBridge::submitShutdownStop()
{
    if (!m_controller) {
        return;
    }

    seriona::control::MediaControlCommand stopCommand;
    stopCommand.kind = seriona::control::MediaControlCommandKind::Stop;
    static_cast<void>(m_controller->submitCommand(stopCommand));
}

void BackendBridge::applyPlayerSnapshot(seriona::control::PlayerStateSnapshot snapshot)
{
    m_playerSnapshot = std::move(snapshot);
    emit playerSnapshotChanged();
}

void BackendBridge::applyLibrarySnapshot(seriona::control::LibraryStateSnapshot snapshot)
{
    m_librarySnapshot = std::move(snapshot);
    emit librarySnapshotChanged();
}

void BackendBridge::enqueueCommandFailureNotification(const seriona::control::MediaControllerCommandResult &result)
{
    std::optional<seriona::control::ControlDomainNotification> notification = notificationFromRejectedCommandResult(result);
    if (!notification.has_value()) {
        return;
    }
    enqueueNotification(std::move(*notification));
}

void BackendBridge::enqueueNotification(seriona::control::ControlDomainNotification notification)
{
    m_notifications.push_back(std::move(notification));
    while (m_notifications.size() > kMaxQueuedNotifications) {
        m_notifications.pop_front();
    }
    emit domainNotificationQueued();
}
#endif

}
