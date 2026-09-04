#include "backend_bridge.h"

#include "settings_controller.h"

#include "seriona/audio/audio_contracts.h"
#include "seriona/control/folder_sort_settings_store.h"
#include "seriona/metadata/metadata_contracts.h"
#include "seriona/scanner/scanner_contracts.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QVariantList>
#include <QVariantMap>
#include <QtTest/QTest>

#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {

class FakeAudioPlaybackService final : public seriona::audio::AudioPlaybackService
{
public:
    void setEventSink(seriona::audio::BackendEventSink sink) override
    {
        std::scoped_lock lock(m_mutex);
        if (!sink) {
            ++m_eventSinkClearCalls;
        }
        m_eventSink = std::move(sink);
    }

    void configureOutput(const seriona::audio::AudioOutputConfig &config) override
    {
        std::scoped_lock lock(m_mutex);
        ++m_configureOutputCalls;
        m_lastOutputConfig = config;
    }

    void configureTransition(const seriona::audio::TransitionConfig &config) override
    {
        std::scoped_lock lock(m_mutex);
        ++m_configureTransitionCalls;
        m_lastTransitionConfig = config;
    }

    void loadTrack(const seriona::audio::TrackPlaybackRequest &) override
    {
        std::scoped_lock lock(m_mutex);
        ++m_loadTrackCalls;
    }
    void prepareNext(const seriona::audio::TrackPlaybackRequest &) override { }
    void play() override { }
    void pause() override { }
    void resume() override { }

    void stop() override
    {
        std::scoped_lock lock(m_mutex);
        ++m_stopCalls;
    }

    void seek(std::chrono::milliseconds) override { }
    void setVolume(float) override { }
    void setMuted(bool) override { }
    void selectOutputDevice(const std::string &) override { }

    seriona::audio::PlaybackClockSnapshot queryPlaybackClock() const override
    {
        return {};
    }

    seriona::audio::AudioOutputConfig lastOutputConfig() const
    {
        std::scoped_lock lock(m_mutex);
        return m_lastOutputConfig;
    }

    seriona::audio::TransitionConfig lastTransitionConfig() const
    {
        std::scoped_lock lock(m_mutex);
        return m_lastTransitionConfig;
    }

    int configureOutputCalls() const
    {
        std::scoped_lock lock(m_mutex);
        return m_configureOutputCalls;
    }

    int configureTransitionCalls() const
    {
        std::scoped_lock lock(m_mutex);
        return m_configureTransitionCalls;
    }

    int loadTrackCalls() const
    {
        std::scoped_lock lock(m_mutex);
        return m_loadTrackCalls;
    }

    std::vector<seriona::audio::AudioDeviceFormat> enumeratePlaybackDevices() const override
    {
        return {
            {"dev-1", "Device One", "pulse", 48000, seriona::audio::AudioSampleFormat::Int16, 2, 0,
             seriona::audio::AudioOutputMode::Mixed, false},
            {"dev-2", "Device Two", "alsa", 96000, seriona::audio::AudioSampleFormat::Float32, 2, 0,
             seriona::audio::AudioOutputMode::Mixed, false},
            {"", "Nameless Device", "pulse", 44100, seriona::audio::AudioSampleFormat::Unknown, 0, 0,
             seriona::audio::AudioOutputMode::Mixed, false},
            {"dev-3", "", "pulse", 48000, seriona::audio::AudioSampleFormat::Int16, 2, 0,
             seriona::audio::AudioOutputMode::Mixed, false},
        };
    }

    void emitEvent(seriona::audio::BackendEvent event)
    {
        seriona::audio::BackendEventSink sink;
        {
            std::scoped_lock lock(m_mutex);
            sink = m_eventSink;
        }
        if (sink) {
            sink(std::move(event));
        }
    }

    int stopCalls() const
    {
        std::scoped_lock lock(m_mutex);
        return m_stopCalls;
    }

    int eventSinkClearCalls() const
    {
        std::scoped_lock lock(m_mutex);
        return m_eventSinkClearCalls;
    }

private:
    mutable std::mutex m_mutex;
    seriona::audio::BackendEventSink m_eventSink;
    seriona::audio::AudioOutputConfig m_lastOutputConfig;
    seriona::audio::TransitionConfig m_lastTransitionConfig;
    int m_stopCalls = 0;
    int m_eventSinkClearCalls = 0;
    int m_configureOutputCalls = 0;
    int m_configureTransitionCalls = 0;
    int m_loadTrackCalls = 0;
};

class FakeFileScannerService final : public seriona::scanner::FileScannerService
{
public:
    void setEventSink(seriona::scanner::ScannerEventSink sink) override
    {
        if (!sink) {
            ++m_eventSinkClearCalls;
        }
        m_eventSink = std::move(sink);
    }
    void configure(const seriona::scanner::ScannerConfig &) override { }
    void scan(const std::vector<seriona::scanner::ScannerRoot> &, seriona::scanner::ScanMode mode) override
    {
        m_lastScanMode = mode;
        ++m_scanCalls;
    }
    void startWatching(const std::vector<seriona::scanner::ScannerRoot> &) override { }
    void stopWatching() override { }
    void stop() override { }

    bool removeLocation(const std::filesystem::path &path) override
    {
        ++m_removeLocationCalls;
        m_lastRemovedPath = path.lexically_normal().generic_string();
        return m_removeLocationResult;
    }

    seriona::scanner::PlaylistTreeSnapshot snapshot() const override
    {
        return m_snapshot;
    }

    // T14：可配置初始库快照（PlayNextTrack/RemoveFromQueue 的 reducer 库来源）
    void setSnapshot(seriona::scanner::PlaylistTreeSnapshot snapshot)
    {
        m_snapshot = std::move(snapshot);
    }

    // T14：发布一次 PlaylistSnapshotUpdated 事件（模拟扫描完成，喂给 reducer 库）
    void emitSnapshot(const seriona::scanner::PlaylistTreeSnapshot &snapshot)
    {
        seriona::scanner::ScannerEvent event;
        event.type = seriona::scanner::ScannerEventType::PlaylistSnapshotUpdated;
        event.monotonicVersion = 1;
        event.timestamp = std::chrono::steady_clock::now();
        event.payload = snapshot;
        if (m_eventSink) {
            m_eventSink(std::move(event));
        }
    }

    int eventSinkClearCalls() const
    {
        return m_eventSinkClearCalls;
    }

    int scanCalls() const
    {
        return m_scanCalls;
    }

    std::optional<seriona::scanner::ScanMode> lastScanMode() const
    {
        return m_lastScanMode;
    }

    int removeLocationCalls() const
    {
        return m_removeLocationCalls;
    }

    const std::string &lastRemovedPath() const
    {
        return m_lastRemovedPath;
    }

    void setRemoveLocationResult(bool result)
    {
        m_removeLocationResult = result;
    }

private:
    seriona::scanner::ScannerEventSink m_eventSink;
    int m_eventSinkClearCalls = 0;
    int m_scanCalls = 0;
    std::optional<seriona::scanner::ScanMode> m_lastScanMode;
    int m_removeLocationCalls = 0;
    bool m_removeLocationResult = false;
    std::string m_lastRemovedPath;
    seriona::scanner::PlaylistTreeSnapshot m_snapshot;
};

class RecordingFolderSortSettingsStore final : public seriona::control::FolderSortSettingsStore
{
public:
    void upsert(seriona::control::FolderSortSetting setting) override
    {
        ++m_upsertCalls;
        m_lastSetting = std::move(setting);
    }

    std::optional<seriona::control::FolderSortSetting> load(const std::filesystem::path &, const std::string &) const override
    {
        return std::nullopt;
    }

    void remove(const std::filesystem::path &, const std::string &) override { }

    std::vector<seriona::control::FolderSortSetting> list(const std::filesystem::path &) const override
    {
        return {};
    }

    int upsertCalls() const
    {
        return m_upsertCalls;
    }

    const std::optional<seriona::control::FolderSortSetting> &lastSetting() const
    {
        return m_lastSetting;
    }

private:
    int m_upsertCalls = 0;
    std::optional<seriona::control::FolderSortSetting> m_lastSetting;
};

struct FakeMetadataState {
    bool throwOnStart = false;
    int startCalls = 0;
    int stopCalls = 0;
    int unsubscribeCalls = 0;
};

class FakeMetadataSharingService final : public seriona::metadata::MetadataSharingService
{
public:
    explicit FakeMetadataSharingService(std::shared_ptr<FakeMetadataState> state)
        : m_state(std::move(state))
    {
    }

    seriona::metadata::MetadataBackendKind backendKind() const override
    {
        return seriona::metadata::MetadataBackendKind::Noop;
    }

    seriona::metadata::MetadataBackendCapabilities capabilities() const override
    {
        return {};
    }

    seriona::control::SubscriptionHandle registerCommandCallback(seriona::control::MediaControlCommandSink callback) override
    {
        m_commandSink = std::move(callback);

        seriona::control::SubscriptionHandle handle;
        handle.subscriptionId = 1;
        handle.unsubscribe = [this, state = m_state] {
            ++state->unsubscribeCalls;
            m_commandSink = {};
        };
        return handle;
    }

    seriona::metadata::MetadataSyncResult start(const seriona::metadata::PlatformMediaState &) override
    {
        ++m_state->startCalls;
        if (m_state->throwOnStart) {
            throw std::runtime_error("metadata start failed");
        }
        return acceptedResult();
    }

    seriona::metadata::MetadataSyncResult update(const seriona::metadata::PlatformMediaState &) override
    {
        return acceptedResult();
    }

    seriona::metadata::MetadataSyncResult stop() override
    {
        ++m_state->stopCalls;
        return acceptedResult();
    }

private:
    static seriona::metadata::MetadataSyncResult acceptedResult()
    {
        seriona::metadata::MetadataSyncResult result;
        result.accepted = true;
        return result;
    }

    seriona::control::MediaControlCommandSink m_commandSink;
    std::shared_ptr<FakeMetadataState> m_state;
};

struct ControllerHarness {
    std::shared_ptr<FakeAudioPlaybackService> audio = std::make_shared<FakeAudioPlaybackService>();
    std::shared_ptr<FakeFileScannerService> scanner = std::make_shared<FakeFileScannerService>();
    std::shared_ptr<FakeMetadataState> metadata = std::make_shared<FakeMetadataState>();
    std::shared_ptr<RecordingFolderSortSettingsStore> folderSortStore = std::make_shared<RecordingFolderSortSettingsStore>();

    Seriona::App::BackendBridge::ControllerFactory factory(bool runInlineForTests)
    {
        struct FactoryState {
            seriona::control::MediaControllerDependencies dependencies;
            seriona::control::MediaControllerOptions options;
        };

        auto state = std::make_shared<FactoryState>();
        state->dependencies.audio = audio;
        state->dependencies.scanner = scanner;
        state->dependencies.metadata = std::make_unique<FakeMetadataSharingService>(metadata);
        state->dependencies.folderSortSettingsStore = folderSortStore;
        state->options.runInlineForTests = runInlineForTests;

        return [state] {
            return seriona::control::makeMediaController(std::move(state->dependencies), state->options);
        };
    }
};

seriona::audio::BackendEvent makePlaybackStateEvent(std::uint64_t version, seriona::audio::PlaybackState state)
{
    seriona::audio::PlaybackStateChanged payload;
    payload.state = state;

    seriona::audio::BackendEvent event;
    event.type = seriona::audio::BackendEventType::PlaybackStateChanged;
    event.sourceModule = seriona::audio::BackendSourceModule::AudioPlaybackService;
    event.monotonicVersion = version;
    event.timestamp = std::chrono::steady_clock::now();
    event.payload = payload;
    return event;
}

void waitForInitialPlayerSnapshot(Seriona::App::BackendBridge &bridge)
{
    QSignalSpy playerSpy(&bridge, &Seriona::App::BackendBridge::playerSnapshotChanged);
    bridge.start();
    QTRY_VERIFY(playerSpy.count() > 0);
}

QVariantMap sortRule(const QString &field, const QString &order)
{
    QVariantMap rule;
    rule.insert(QStringLiteral("field"), field);
    rule.insert(QStringLiteral("order"), order);
    return rule;
}

QVariantList sortRules(std::initializer_list<QVariantMap> rules)
{
    QVariantList result;
    for (const QVariantMap &rule : rules) {
        result.append(rule);
    }
    return result;
}

seriona::scanner::PlaylistTreeSnapshot makeQueueSnapshot()
{
    using seriona::scanner::PlaylistNode;
    using seriona::scanner::PlaylistNodeKind;
    using seriona::scanner::SongMetadata;

    SongMetadata song;
    song.trackId = "track-x-id";
    song.title = "Track X";
    song.filePath = std::filesystem::path("/music/folder-x/track-x.mp3");

    PlaylistNode rootNode;
    rootNode.nodeId = "root";
    rootNode.kind = PlaylistNodeKind::Root;
    rootNode.displayName = "Library";
    rootNode.childNodeIds = {"folder-x"};

    PlaylistNode folder;
    folder.nodeId = "folder-x";
    folder.parentNodeId = std::string{"root"};
    folder.kind = PlaylistNodeKind::Directory;
    folder.displayName = "Folder X";
    folder.childNodeIds = {"track-x"};

    PlaylistNode track;
    track.nodeId = "track-x";
    track.parentNodeId = std::string{"folder-x"};
    track.kind = PlaylistNodeKind::Track;
    track.displayName = "Track X";
    track.song = song;

    seriona::scanner::PlaylistTreeSnapshot snapshot;
    snapshot.version = 7;
    snapshot.rootNodeId = std::string{"root"};
    snapshot.nodes = {rootNode, folder, track};
    return snapshot;
}

void seedLibrarySnapshot(Seriona::App::BackendBridge &bridge, ControllerHarness &harness)
{
    QSignalSpy librarySpy(&bridge, &Seriona::App::BackendBridge::librarySnapshotChanged);
    harness.scanner->emitSnapshot(makeQueueSnapshot());
    QTRY_VERIFY(librarySpy.count() > 0);
}

}

class BackendBridgeTest : public QObject
{
    Q_OBJECT

private slots:
    void threading();
    void shutdown();
    void shutdownStopSent();
    void shutdownSequence();
    void shutdownStartFailed();
    void scanLibraryDefaultsToFullMode();
    void scanLibraryForwardsIncrementalMode();
    void applyFolderSortRulesBuildsTypedBackendCommand();
    void applyFolderSortRulesAllowsEmptyRules();
    void applyFolderSortRulesRejectsInvalidPayloadWithoutDispatch();
    void applyFolderSortRulesRejectsMissingContextWithoutDispatch();
    void submitConfigureOutputBuildsTypedBackendCommand();
    void submitConfigureOutputRejectsInvalidPayloadWithoutDispatch();
    void deleteTargetBuildsTypedDeleteTrackCommand();
    void deleteTargetBuildsTypedDeleteFolderCommand();
    void deleteTargetRejectsEmptyPathWithoutDispatch();
    void playNextTrackBuildsQueueCommand();
    void playNextTrackRejectsEmptyIdWithoutDispatch();
    void removeFromQueueBuildsIndexedCommand();
    void enumeratePlaybackDevicesMapsDeviceIds();
    void settingsPushOnStart();
    void submitTransitionConfigBuildsTypedBackendCommand();
    void submitTransitionConfigRejectsInvalidPayloadWithoutDispatch();
    void transitionConfigWhilePlayingDoesNotReloadOrInterrupt();
};

void BackendBridgeTest::threading()
{
    ControllerHarness harness;
    Seriona::App::BackendBridge bridge(harness.factory(false));
    waitForInitialPlayerSnapshot(bridge);

    QThread *signalThread = nullptr;
    QSignalSpy playerSpy(&bridge, &Seriona::App::BackendBridge::playerSnapshotChanged);
    connect(&bridge, &Seriona::App::BackendBridge::playerSnapshotChanged, &bridge, [&signalThread] {
        signalThread = QThread::currentThread();
    });

    std::thread producer([&harness] {
        harness.audio->emitEvent(makePlaybackStateEvent(1, seriona::audio::PlaybackState::Playing));
    });
    producer.join();

    QTRY_VERIFY(playerSpy.count() > 0);
    QCOMPARE(signalThread, QCoreApplication::instance()->thread());
    QCOMPARE(bridge.playerSnapshot().playback.state, seriona::control::PlaybackStatus::Playing);

    bridge.shutdown();
}

void BackendBridgeTest::shutdown()
{
    ControllerHarness harness;
    auto bridge = std::make_unique<Seriona::App::BackendBridge>(harness.factory(true));
    waitForInitialPlayerSnapshot(*bridge);

    QSignalSpy playerSpy(bridge.get(), &Seriona::App::BackendBridge::playerSnapshotChanged);
    harness.audio->emitEvent(makePlaybackStateEvent(1, seriona::audio::PlaybackState::Playing));
    bridge->drainForTests();

    bridge->shutdown();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QCOMPARE(playerSpy.count(), 0);

    bridge.reset();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QVERIFY(true);
}

void BackendBridgeTest::shutdownStopSent()
{
    ControllerHarness harness;
    Seriona::App::BackendBridge bridge(harness.factory(true));
    waitForInitialPlayerSnapshot(bridge);

    QCOMPARE(harness.audio->stopCalls(), 0);
    bridge.shutdown();
    QCOMPARE(harness.audio->stopCalls(), 1);
}

void BackendBridgeTest::shutdownSequence()
{
    ControllerHarness harness;
    Seriona::App::BackendBridge bridge(harness.factory(true));
    waitForInitialPlayerSnapshot(bridge);

    QSignalSpy shutdownSpy(&bridge, &Seriona::App::BackendBridge::shutdownCompleted);
    QSignalSpy playerSpy(&bridge, &Seriona::App::BackendBridge::playerSnapshotChanged);

    bridge.shutdown();

    QCOMPARE(harness.audio->stopCalls(), 1);
    QVERIFY(harness.audio->eventSinkClearCalls() >= 1);
    QVERIFY(harness.scanner->eventSinkClearCalls() >= 1);
    QCOMPARE(harness.metadata->unsubscribeCalls, 1);
    QCOMPARE(harness.metadata->stopCalls, 1);
    QCOMPARE(bridge.started(), false);
    QCOMPARE(bridge.shuttingDown(), true);
    QCOMPARE(shutdownSpy.count(), 1);

    harness.audio->emitEvent(makePlaybackStateEvent(2, seriona::audio::PlaybackState::Playing));
    bridge.drainForTests();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QCOMPARE(playerSpy.count(), 0);

    const int audioEventSinkClearCalls = harness.audio->eventSinkClearCalls();
    const int scannerEventSinkClearCalls = harness.scanner->eventSinkClearCalls();
    bridge.shutdown();
    QCOMPARE(harness.audio->stopCalls(), 1);
    QCOMPARE(harness.audio->eventSinkClearCalls(), audioEventSinkClearCalls);
    QCOMPARE(harness.scanner->eventSinkClearCalls(), scannerEventSinkClearCalls);
    QCOMPARE(harness.metadata->stopCalls, 1);
    QCOMPARE(shutdownSpy.count(), 1);
}

void BackendBridgeTest::shutdownStartFailed()
{
    ControllerHarness harness;
    harness.metadata->throwOnStart = true;
    Seriona::App::BackendBridge bridge(harness.factory(true));

    QSignalSpy playerSpy(&bridge, &Seriona::App::BackendBridge::playerSnapshotChanged);
    bridge.start();

    QCOMPARE(bridge.started(), false);
    QCOMPARE(bridge.shuttingDown(), true);
    QCOMPARE(harness.metadata->startCalls, 1);
    QCOMPARE(harness.metadata->unsubscribeCalls, 1);
    QCOMPARE(harness.metadata->stopCalls, 1);

    harness.audio->emitEvent(makePlaybackStateEvent(3, seriona::audio::PlaybackState::Playing));
    bridge.drainForTests();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QCOMPARE(playerSpy.count(), 0);

    bridge.shutdown();
    bridge.shutdown();
    QCOMPARE(harness.audio->stopCalls(), 0);
    QCOMPARE(harness.metadata->stopCalls, 1);
}

void BackendBridgeTest::scanLibraryDefaultsToFullMode()
{
    QTemporaryDir musicDir;
    QVERIFY(musicDir.isValid());

    ControllerHarness harness;
    Seriona::App::BackendBridge bridge(harness.factory(true));
    waitForInitialPlayerSnapshot(bridge);

    const seriona::control::MediaControllerCommandResult result = bridge.scanLibrary(musicDir.path());

    QVERIFY(result.accepted);
    QCOMPARE(harness.scanner->scanCalls(), 1);
    QVERIFY(harness.scanner->lastScanMode().has_value());
    QCOMPARE(*harness.scanner->lastScanMode(), seriona::scanner::ScanMode::Full);

    bridge.shutdown();
}

void BackendBridgeTest::scanLibraryForwardsIncrementalMode()
{
    QTemporaryDir musicDir;
    QVERIFY(musicDir.isValid());

    ControllerHarness harness;
    Seriona::App::BackendBridge bridge(harness.factory(true));
    waitForInitialPlayerSnapshot(bridge);

    const seriona::control::MediaControllerCommandResult result = bridge.scanLibrary(
        musicDir.path(),
        seriona::scanner::ScanMode::Incremental);

    QVERIFY(result.accepted);
    QCOMPARE(harness.scanner->scanCalls(), 1);
    QVERIFY(harness.scanner->lastScanMode().has_value());
    QCOMPARE(*harness.scanner->lastScanMode(), seriona::scanner::ScanMode::Incremental);

    bridge.shutdown();
}

void BackendBridgeTest::applyFolderSortRulesBuildsTypedBackendCommand()
{
    QTemporaryDir musicDir;
    QVERIFY(musicDir.isValid());
    const QString rootPath = QFileInfo(musicDir.path()).absoluteFilePath();

    ControllerHarness harness;
    Seriona::App::BackendBridge bridge(harness.factory(true));
    waitForInitialPlayerSnapshot(bridge);

    const seriona::control::MediaControllerCommandResult result = bridge.applyFolderSortRules(
        rootPath + QStringLiteral("/../") + QFileInfo(musicDir.path()).fileName(),
        QStringLiteral("  folder-jazz  "),
        sortRules({sortRule(QStringLiteral("title"), QStringLiteral("desc")),
                   sortRule(QStringLiteral("createdDate"), QStringLiteral("asc"))}));

    QVERIFY(result.accepted);
    QCOMPARE(result.code, seriona::control::MediaControllerErrorCode::None);
    QCOMPARE(harness.folderSortStore->upsertCalls(), 1);
    QVERIFY(harness.folderSortStore->lastSetting().has_value());
    const seriona::control::FolderSortSetting &setting = *harness.folderSortStore->lastSetting();
    QCOMPARE(QString::fromStdString(setting.rootPath.generic_string()), rootPath);
    QCOMPARE(QString::fromStdString(setting.folderNodeId), QStringLiteral("folder-jazz"));
    QCOMPARE(setting.rules.size(), std::size_t{2});
    QCOMPARE(setting.rules.at(0).field, seriona::control::FolderSortField::Title);
    QCOMPARE(setting.rules.at(0).direction, seriona::control::FolderSortDirection::Descending);
    QCOMPARE(setting.rules.at(0).missingValuePolicy, seriona::control::FolderSortMissingValuePolicy::Last);
    QCOMPARE(setting.rules.at(1).field, seriona::control::FolderSortField::CreatedDate);
    QCOMPARE(setting.rules.at(1).direction, seriona::control::FolderSortDirection::Ascending);
    QCOMPARE(setting.rules.at(1).missingValuePolicy, seriona::control::FolderSortMissingValuePolicy::Last);

    bridge.shutdown();
}

void BackendBridgeTest::applyFolderSortRulesAllowsEmptyRules()
{
    QTemporaryDir musicDir;
    QVERIFY(musicDir.isValid());
    const QString rootPath = QFileInfo(musicDir.path()).absoluteFilePath();

    ControllerHarness harness;
    Seriona::App::BackendBridge bridge(harness.factory(true));
    waitForInitialPlayerSnapshot(bridge);

    const seriona::control::MediaControllerCommandResult result = bridge.applyFolderSortRules(
        rootPath,
        QStringLiteral("folder-jazz"),
        QVariantList{});

    QCOMPARE(result.accepted, false);
    QCOMPARE(result.code, seriona::control::MediaControllerErrorCode::InvalidCommand);
    QVERIFY(QString::fromStdString(result.message).contains(QStringLiteral("sort rule"), Qt::CaseInsensitive));
    QCOMPARE(harness.folderSortStore->upsertCalls(), 0);
    QVERIFY(!harness.folderSortStore->lastSetting().has_value());

    bridge.shutdown();
}

void BackendBridgeTest::applyFolderSortRulesRejectsInvalidPayloadWithoutDispatch()
{
    QTemporaryDir musicDir;
    QVERIFY(musicDir.isValid());
    const QString rootPath = QFileInfo(musicDir.path()).absoluteFilePath();

    ControllerHarness harness;
    Seriona::App::BackendBridge bridge(harness.factory(true));
    waitForInitialPlayerSnapshot(bridge);

    const seriona::control::MediaControllerCommandResult invalidField = bridge.applyFolderSortRules(
        rootPath,
        QStringLiteral("folder-jazz"),
        sortRules({sortRule(QStringLiteral("unknownField"), QStringLiteral("asc"))}));
    QCOMPARE(invalidField.accepted, false);
    QCOMPARE(invalidField.code, seriona::control::MediaControllerErrorCode::InvalidCommand);
    QVERIFY(QString::fromStdString(invalidField.message).contains(QStringLiteral("field"), Qt::CaseInsensitive));

    const seriona::control::MediaControllerCommandResult invalidDirection = bridge.applyFolderSortRules(
        rootPath,
        QStringLiteral("folder-jazz"),
        sortRules({sortRule(QStringLiteral("title"), QStringLiteral("sideways"))}));
    QCOMPARE(invalidDirection.accepted, false);
    QCOMPARE(invalidDirection.code, seriona::control::MediaControllerErrorCode::InvalidCommand);
    QVERIFY(QString::fromStdString(invalidDirection.message).contains(QStringLiteral("direction"), Qt::CaseInsensitive));

    QVariantList malformed;
    malformed.append(QStringLiteral("not-a-map"));
    const seriona::control::MediaControllerCommandResult malformedPayload = bridge.applyFolderSortRules(
        rootPath,
        QStringLiteral("folder-jazz"),
        malformed);
    QCOMPARE(malformedPayload.accepted, false);
    QCOMPARE(malformedPayload.code, seriona::control::MediaControllerErrorCode::InvalidCommand);
    QVERIFY(QString::fromStdString(malformedPayload.message).contains(QStringLiteral("payload"), Qt::CaseInsensitive));
    QCOMPARE(harness.folderSortStore->upsertCalls(), 0);

    bridge.shutdown();
}

void BackendBridgeTest::applyFolderSortRulesRejectsMissingContextWithoutDispatch()
{
    QTemporaryDir musicDir;
    QVERIFY(musicDir.isValid());
    const QString rootPath = QFileInfo(musicDir.path()).absoluteFilePath();

    ControllerHarness harness;
    Seriona::App::BackendBridge bridge(harness.factory(true));
    waitForInitialPlayerSnapshot(bridge);

    const seriona::control::MediaControllerCommandResult missingRoot = bridge.applyFolderSortRules(
        QString(),
        QStringLiteral("folder-jazz"),
        sortRules({sortRule(QStringLiteral("title"), QStringLiteral("asc"))}));
    QCOMPARE(missingRoot.accepted, false);
    QCOMPARE(missingRoot.code, seriona::control::MediaControllerErrorCode::InvalidCommand);
    QVERIFY(QString::fromStdString(missingRoot.message).contains(QStringLiteral("root"), Qt::CaseInsensitive));

    const seriona::control::MediaControllerCommandResult missingFolder = bridge.applyFolderSortRules(
        rootPath,
        QStringLiteral("   "),
        sortRules({sortRule(QStringLiteral("title"), QStringLiteral("asc"))}));
    QCOMPARE(missingFolder.accepted, false);
    QCOMPARE(missingFolder.code, seriona::control::MediaControllerErrorCode::InvalidCommand);
    QVERIFY(QString::fromStdString(missingFolder.message).contains(QStringLiteral("folder"), Qt::CaseInsensitive));
    QCOMPARE(harness.folderSortStore->upsertCalls(), 0);

    bridge.shutdown();
}

void BackendBridgeTest::submitConfigureOutputBuildsTypedBackendCommand()
{
    ControllerHarness harness;
    Seriona::App::BackendBridge bridge(harness.factory(true));
    waitForInitialPlayerSnapshot(bridge);

    const seriona::control::MediaControllerCommandResult mixed = bridge.submitConfigureOutput(
        1, 96000, 2, 500, QStringLiteral("dev-1"));
    QVERIFY(mixed.accepted);
    QCOMPARE(mixed.code, seriona::control::MediaControllerErrorCode::None);
    QCOMPARE(harness.audio->configureOutputCalls(), 1);
    const seriona::audio::AudioOutputConfig mixedConfig = harness.audio->lastOutputConfig();
    QCOMPARE(mixedConfig.outputMode, seriona::audio::AudioOutputMode::Mixed);
    QVERIFY(mixedConfig.targetSampleRate.has_value());
    QCOMPARE(*mixedConfig.targetSampleRate, std::uint32_t{96000});
    QVERIFY(mixedConfig.targetSampleFormat.has_value());
    QCOMPARE(*mixedConfig.targetSampleFormat, seriona::audio::AudioSampleFormat::Int24);
    QCOMPARE(mixedConfig.bufferDuration, std::chrono::milliseconds(500));
    QCOMPARE(QString::fromStdString(mixedConfig.preferredDeviceId), QStringLiteral("dev-1"));

    // 0 采样率/位深 = 跟随设备：不携带 targetSampleRate/targetSampleFormat；空设备 id 表示默认设备
    const seriona::control::MediaControllerCommandResult direct = bridge.submitConfigureOutput(
        0, 0, 0, 300, QString());
    QVERIFY(direct.accepted);
    QCOMPARE(direct.code, seriona::control::MediaControllerErrorCode::None);
    QCOMPARE(harness.audio->configureOutputCalls(), 2);
    const seriona::audio::AudioOutputConfig directConfig = harness.audio->lastOutputConfig();
    QCOMPARE(directConfig.outputMode, seriona::audio::AudioOutputMode::Direct);
    QVERIFY(!directConfig.targetSampleRate.has_value());
    QVERIFY(!directConfig.targetSampleFormat.has_value());
    QCOMPARE(directConfig.bufferDuration, std::chrono::milliseconds(300));
    QVERIFY(directConfig.preferredDeviceId.empty());

    // 位深 1/3/4 映射 Int16/Int32/Float32
    const seriona::control::MediaControllerCommandResult int16 = bridge.submitConfigureOutput(
        0, 0, 1, 300, QString());
    QVERIFY(int16.accepted);
    QCOMPARE(*harness.audio->lastOutputConfig().targetSampleFormat, seriona::audio::AudioSampleFormat::Int16);
    const seriona::control::MediaControllerCommandResult int32 = bridge.submitConfigureOutput(
        0, 0, 3, 300, QString());
    QVERIFY(int32.accepted);
    QCOMPARE(*harness.audio->lastOutputConfig().targetSampleFormat, seriona::audio::AudioSampleFormat::Int32);
    const seriona::control::MediaControllerCommandResult float32 = bridge.submitConfigureOutput(
        0, 0, 4, 300, QString());
    QVERIFY(float32.accepted);
    QCOMPARE(*harness.audio->lastOutputConfig().targetSampleFormat, seriona::audio::AudioSampleFormat::Float32);

    bridge.shutdown();
}

void BackendBridgeTest::submitConfigureOutputRejectsInvalidPayloadWithoutDispatch()
{
    ControllerHarness harness;
    Seriona::App::BackendBridge bridge(harness.factory(true));
    waitForInitialPlayerSnapshot(bridge);

    const seriona::control::MediaControllerCommandResult invalidMode = bridge.submitConfigureOutput(
        7, 48000, 0, 300, QString());
    QCOMPARE(invalidMode.accepted, false);
    QCOMPARE(invalidMode.code, seriona::control::MediaControllerErrorCode::InvalidCommand);

    const seriona::control::MediaControllerCommandResult invalidRate = bridge.submitConfigureOutput(
        0, 100, 0, 300, QString());
    QCOMPARE(invalidRate.accepted, false);
    QCOMPARE(invalidRate.code, seriona::control::MediaControllerErrorCode::InvalidCommand);

    const seriona::control::MediaControllerCommandResult invalidFormat = bridge.submitConfigureOutput(
        0, 48000, 6, 300, QString());
    QCOMPARE(invalidFormat.accepted, false);
    QCOMPARE(invalidFormat.code, seriona::control::MediaControllerErrorCode::InvalidCommand);

    const seriona::control::MediaControllerCommandResult invalidFormatHigh = bridge.submitConfigureOutput(
        0, 48000, 99, 300, QString());
    QCOMPARE(invalidFormatHigh.accepted, false);
    QCOMPARE(invalidFormatHigh.code, seriona::control::MediaControllerErrorCode::InvalidCommand);

    const seriona::control::MediaControllerCommandResult invalidDuration = bridge.submitConfigureOutput(
        0, 48000, 0, 2000, QString());
    QCOMPARE(invalidDuration.accepted, false);
    QCOMPARE(invalidDuration.code, seriona::control::MediaControllerErrorCode::InvalidCommand);

    QCOMPARE(harness.audio->configureOutputCalls(), 0);

    bridge.shutdown();
}

void BackendBridgeTest::deleteTargetBuildsTypedDeleteTrackCommand()
{
    QTemporaryDir musicDir;
    QVERIFY(musicDir.isValid());
    const QString targetPath = QFileInfo(musicDir.path()).absoluteFilePath() + QStringLiteral("/song.mp3");

    ControllerHarness harness;
    harness.scanner->setRemoveLocationResult(true);
    Seriona::App::BackendBridge bridge(harness.factory(true));
    waitForInitialPlayerSnapshot(bridge);

    // 确认弹窗确认后调用一次 deleteTarget → 恰好发出一条删除命令（removeLocation 调用计数为 1）
    const seriona::control::MediaControllerCommandResult result = bridge.deleteTarget(targetPath, false);

    QVERIFY(result.accepted);
    QCOMPARE(result.code, seriona::control::MediaControllerErrorCode::None);
    QCOMPARE(harness.scanner->removeLocationCalls(), 1);
    QCOMPARE(QString::fromStdString(harness.scanner->lastRemovedPath()), targetPath);

    bridge.shutdown();
}

void BackendBridgeTest::deleteTargetBuildsTypedDeleteFolderCommand()
{
    QTemporaryDir musicDir;
    QVERIFY(musicDir.isValid());
    const QString targetPath = QFileInfo(musicDir.path()).absoluteFilePath();

    ControllerHarness harness;
    harness.scanner->setRemoveLocationResult(true);
    Seriona::App::BackendBridge bridge(harness.factory(true));
    waitForInitialPlayerSnapshot(bridge);

    const seriona::control::MediaControllerCommandResult result = bridge.deleteTarget(targetPath, true);

    QVERIFY(result.accepted);
    QCOMPARE(result.code, seriona::control::MediaControllerErrorCode::None);
    QCOMPARE(harness.scanner->removeLocationCalls(), 1);
    QCOMPARE(QString::fromStdString(harness.scanner->lastRemovedPath()), targetPath);

    bridge.shutdown();
}

void BackendBridgeTest::deleteTargetRejectsEmptyPathWithoutDispatch()
{
    ControllerHarness harness;
    Seriona::App::BackendBridge bridge(harness.factory(true));
    waitForInitialPlayerSnapshot(bridge);

    // 空白路径直接拒绝，不发出任何删除命令
    const seriona::control::MediaControllerCommandResult result = bridge.deleteTarget(QStringLiteral("   \t"), false);

    QVERIFY(!result.accepted);
    QCOMPARE(result.code, seriona::control::MediaControllerErrorCode::InvalidCommand);
    QCOMPARE(harness.scanner->removeLocationCalls(), 0);

    bridge.shutdown();
}

void BackendBridgeTest::playNextTrackBuildsQueueCommand()
{
    ControllerHarness harness;
    harness.scanner->setSnapshot(makeQueueSnapshot());
    Seriona::App::BackendBridge bridge(harness.factory(false));
    waitForInitialPlayerSnapshot(bridge);
    seedLibrarySnapshot(bridge, harness);

    const seriona::control::MediaControllerCommandResult result = bridge.playNextTrack(QStringLiteral("track-x-id"));

    QVERIFY(result.accepted);
    QCOMPARE(result.code, seriona::control::MediaControllerErrorCode::None);

    // 跨端定死字段断言：快照 queueEntries: [{trackId, nodeId}]
    QTRY_VERIFY(bridge.playerSnapshot().queueEntries.size() == 1);
    QCOMPARE(QString::fromStdString(bridge.playerSnapshot().queueEntries.at(0).trackId), QStringLiteral("track-x-id"));
    QCOMPARE(QString::fromStdString(bridge.playerSnapshot().queueEntries.at(0).nodeId), QStringLiteral("folder-x"));

    bridge.shutdown();
}

void BackendBridgeTest::playNextTrackRejectsEmptyIdWithoutDispatch()
{
    ControllerHarness harness;
    harness.scanner->setSnapshot(makeQueueSnapshot());
    Seriona::App::BackendBridge bridge(harness.factory(false));
    waitForInitialPlayerSnapshot(bridge);
    seedLibrarySnapshot(bridge, harness);

    const seriona::control::MediaControllerCommandResult result = bridge.playNextTrack(QStringLiteral("   "));

    QVERIFY(!result.accepted);
    QCOMPARE(result.code, seriona::control::MediaControllerErrorCode::InvalidCommand);
    QTRY_COMPARE(bridge.playerSnapshot().queueEntries.size(), std::size_t{0});

    bridge.shutdown();
}

void BackendBridgeTest::removeFromQueueBuildsIndexedCommand()
{
    ControllerHarness harness;
    harness.scanner->setSnapshot(makeQueueSnapshot());
    Seriona::App::BackendBridge bridge(harness.factory(false));
    waitForInitialPlayerSnapshot(bridge);
    seedLibrarySnapshot(bridge, harness);

    QVERIFY(bridge.playNextTrack(QStringLiteral("track-x-id")).accepted);
    QVERIFY(bridge.playNextTrack(QStringLiteral("track-x-id")).accepted);
    QTRY_COMPARE(bridge.playerSnapshot().queueEntries.size(), std::size_t{2});

    const seriona::control::MediaControllerCommandResult result = bridge.removeFromQueue(0);

    QVERIFY(result.accepted);
    QCOMPARE(result.code, seriona::control::MediaControllerErrorCode::None);
    QTRY_COMPARE(bridge.playerSnapshot().queueEntries.size(), std::size_t{1});
    QCOMPARE(QString::fromStdString(bridge.playerSnapshot().queueEntries.at(0).trackId), QStringLiteral("track-x-id"));

    bridge.shutdown();
}

void BackendBridgeTest::enumeratePlaybackDevicesMapsDeviceIds()
{
    ControllerHarness harness;
    Seriona::App::BackendBridge bridge(harness.factory(true));
    waitForInitialPlayerSnapshot(bridge);

    // 空 deviceId 的设备不参与映射（无法被选择）；deviceName 为空时以 deviceId 兜底
    const QList<QPair<QString, QString>> devices = bridge.enumeratePlaybackDevices();
    const QList<QPair<QString, QString>> expected = {
        {QStringLiteral("dev-1"), QStringLiteral("Device One")},
        {QStringLiteral("dev-2"), QStringLiteral("Device Two")},
        {QStringLiteral("dev-3"), QStringLiteral("dev-3")},
    };
    QCOMPARE(devices, expected);

    bridge.shutdown();
}

void BackendBridgeTest::settingsPushOnStart()
{
    ControllerHarness harness;
    Seriona::App::BackendBridge bridge(harness.factory(true));
    Seriona::App::SettingsController settings;
    int pushCount = 0;
    settings.setApplyOutputConfigExecutor(
        [&pushCount](int, int, int, int, const QString &) {
            ++pushCount;
        });
    int transitionPushCount = 0;
    settings.setApplyTransitionConfigExecutor(
        [&transitionPushCount](int, bool, bool, int, int, int, int, int, int) {
            ++transitionPushCount;
        });

    // AppFacade 启动挂钩契约：startedChanged 且 started() 为真时推送一次持久化配置
    // （输出组 apply + 过渡组 applyTransitionConfig，顺序与 app_facade.cpp 一致）
    connect(&bridge, &Seriona::App::BackendBridge::startedChanged, &bridge, [&] {
        if (!bridge.started()) {
            return;
        }
        settings.reloadFromSettings();
        settings.apply();
        settings.applyTransitionConfig();
    });

    bridge.start();
    QCOMPARE(bridge.started(), true);
    QCOMPARE(pushCount, 1);
    QCOMPARE(transitionPushCount, 1);

    // shutdown 的 startedChanged（started()==false）不得再次推送
    bridge.shutdown();
    QCOMPARE(pushCount, 1);
    QCOMPARE(transitionPushCount, 1);
}

void BackendBridgeTest::submitTransitionConfigBuildsTypedBackendCommand()
{
    ControllerHarness harness;
    Seriona::App::BackendBridge bridge(harness.factory(true));
    waitForInitialPlayerSnapshot(bridge);

    // 9 参按 TransitionConfig 契约顺序组包 → SetTransitionConfig 命令到达音频服务
    const seriona::control::MediaControllerCommandResult result = bridge.submitTransitionConfig(
        2, true, true, 1200, 2500, 600, 700, 1, 800);
    QVERIFY(result.accepted);
    QCOMPARE(result.code, seriona::control::MediaControllerErrorCode::None);
    QCOMPARE(harness.audio->configureTransitionCalls(), 1);
    const seriona::audio::TransitionConfig received = harness.audio->lastTransitionConfig();
    QCOMPARE(received.autoAdvanceFadeMode, seriona::audio::AutoAdvanceFadeMode::All);
    QCOMPARE(received.fadeOnTransport, true);
    QCOMPARE(received.fadeOnSeek, true);
    QCOMPARE(received.gaplessPreloadMs, std::chrono::milliseconds(1200));
    QCOMPARE(received.crossfadeMs, std::chrono::milliseconds(2500));
    QCOMPARE(received.transportFadeMs, std::chrono::milliseconds(600));
    QCOMPARE(received.seekFadeMs, std::chrono::milliseconds(700));
    QCOMPARE(received.manualAdvanceFadeMode, seriona::audio::ManualAdvanceFadeMode::ShortDip);
    QCOMPARE(received.manualShortCrossfadeMs, std::chrono::milliseconds(800));

    // 全默认参数（0 时长语义合法：即时完成，不做下界钳制）
    const seriona::control::MediaControllerCommandResult allOff = bridge.submitTransitionConfig(
        0, false, false, 0, 0, 0, 0, 0, 0);
    QVERIFY(allOff.accepted);
    QCOMPARE(harness.audio->configureTransitionCalls(), 2);
    const seriona::audio::TransitionConfig defaultOff = harness.audio->lastTransitionConfig();
    QCOMPARE(defaultOff.autoAdvanceFadeMode, seriona::audio::AutoAdvanceFadeMode::Off);
    QCOMPARE(defaultOff.crossfadeMs, std::chrono::milliseconds(0));
    QCOMPARE(defaultOff.manualAdvanceFadeMode, seriona::audio::ManualAdvanceFadeMode::Off);

    // 边界最大值合法（crossfade 10000 / 短淡变与预加载各自上限）
    const seriona::control::MediaControllerCommandResult maxed = bridge.submitTransitionConfig(
        1, false, false, 5000, 10000, 3000, 3000, 2, 3000);
    QVERIFY(maxed.accepted);
    const seriona::audio::TransitionConfig maxConfig = harness.audio->lastTransitionConfig();
    QCOMPARE(maxConfig.gaplessPreloadMs, std::chrono::milliseconds(5000));
    QCOMPARE(maxConfig.crossfadeMs, std::chrono::milliseconds(10000));
    QCOMPARE(maxConfig.transportFadeMs, std::chrono::milliseconds(3000));
    QCOMPARE(maxConfig.seekFadeMs, std::chrono::milliseconds(3000));
    QCOMPARE(maxConfig.manualShortCrossfadeMs, std::chrono::milliseconds(3000));

    // 过渡命令不触发 ConfigureOutput/LoadTrack/stop（与 ConfigureOutput 语义隔离）
    QCOMPARE(harness.audio->configureOutputCalls(), 0);
    QCOMPARE(harness.audio->loadTrackCalls(), 0);
    QCOMPARE(harness.audio->stopCalls(), 0);

    bridge.shutdown();
}

void BackendBridgeTest::submitTransitionConfigRejectsInvalidPayloadWithoutDispatch()
{
    ControllerHarness harness;
    Seriona::App::BackendBridge bridge(harness.factory(true));
    waitForInitialPlayerSnapshot(bridge);

    // 枚举越界（前端本地拒绝，与后端 reducer 域一致；命令不外发）
    const seriona::control::MediaControllerCommandResult badAuto = bridge.submitTransitionConfig(
        3, false, false, 0, 3000, 300, 300, 0, 500);
    QCOMPARE(badAuto.accepted, false);
    QCOMPARE(badAuto.code, seriona::control::MediaControllerErrorCode::InvalidCommand);
    const seriona::control::MediaControllerCommandResult badManual = bridge.submitTransitionConfig(
        0, false, false, 0, 3000, 300, 300, -1, 500);
    QCOMPARE(badManual.accepted, false);
    QCOMPARE(badManual.code, seriona::control::MediaControllerErrorCode::InvalidCommand);

    // crossfadeMs 越界（0-10000）
    const seriona::control::MediaControllerCommandResult badCrossfade = bridge.submitTransitionConfig(
        0, false, false, 0, 10001, 300, 300, 0, 500);
    QCOMPARE(badCrossfade.accepted, false);
    QCOMPARE(badCrossfade.code, seriona::control::MediaControllerErrorCode::InvalidCommand);
    const seriona::control::MediaControllerCommandResult negativeCrossfade = bridge.submitTransitionConfig(
        0, false, false, 0, -1, 300, 300, 0, 500);
    QCOMPARE(negativeCrossfade.accepted, false);
    QCOMPARE(negativeCrossfade.code, seriona::control::MediaControllerErrorCode::InvalidCommand);

    // transport/seek/manualShort 越界（0-3000）
    const seriona::control::MediaControllerCommandResult badTransport = bridge.submitTransitionConfig(
        0, false, false, 0, 3000, 3001, 300, 0, 500);
    QCOMPARE(badTransport.accepted, false);
    const seriona::control::MediaControllerCommandResult badSeek = bridge.submitTransitionConfig(
        0, false, false, 0, 3000, 300, -5, 0, 500);
    QCOMPARE(badSeek.accepted, false);
    const seriona::control::MediaControllerCommandResult badManualShort = bridge.submitTransitionConfig(
        0, false, false, 0, 3000, 300, 300, 0, 3001);
    QCOMPARE(badManualShort.accepted, false);

    // 预加载越界（0-5000）
    const seriona::control::MediaControllerCommandResult badPreload = bridge.submitTransitionConfig(
        0, false, false, 5001, 3000, 300, 300, 0, 500);
    QCOMPARE(badPreload.accepted, false);
    QCOMPARE(badPreload.code, seriona::control::MediaControllerErrorCode::InvalidCommand);

    // 全部非法值零外发
    QCOMPARE(harness.audio->configureTransitionCalls(), 0);
    QCOMPARE(harness.audio->loadTrackCalls(), 0);

    bridge.shutdown();
}

void BackendBridgeTest::transitionConfigWhilePlayingDoesNotReloadOrInterrupt()
{
    // 联调冒烟（真实内嵌后端 reducer + fake 音频服务）：
    // 播放中修改过渡设置 → 后端收到 SetTransitionConfig 且无 LoadTrack/设备重开副作用，
    // 播放状态不被打断
    ControllerHarness harness;
    Seriona::App::BackendBridge bridge(harness.factory(true));
    waitForInitialPlayerSnapshot(bridge);

    harness.audio->emitEvent(makePlaybackStateEvent(1, seriona::audio::PlaybackState::Playing));
    bridge.drainForTests();
    QTRY_COMPARE(bridge.playerSnapshot().playback.state, seriona::control::PlaybackStatus::Playing);
    QCOMPARE(harness.audio->loadTrackCalls(), 0);
    QCOMPARE(harness.audio->stopCalls(), 0);

    // 经 SettingsController 全链路：滑块 crossfadeMs（400ms 去抖）+ 档位立即推送
    Seriona::App::SettingsController settings;
    settings.setApplyTransitionConfigExecutor(
        [&bridge](int autoAdvanceFadeMode, bool fadeOnTransport, bool fadeOnSeek, int gaplessPreloadMs,
                  int crossfadeMs, int transportFadeMs, int seekFadeMs, int manualAdvanceFadeMode,
                  int manualShortCrossfadeMs) {
            bridge.submitTransitionConfig(autoAdvanceFadeMode, fadeOnTransport, fadeOnSeek, gaplessPreloadMs,
                                          crossfadeMs, transportFadeMs, seekFadeMs, manualAdvanceFadeMode,
                                          manualShortCrossfadeMs);
        });

    // 播放中改档位（立即）→ 后端立即收到新配置
    settings.setAutoAdvanceFadeMode(1);
    QCOMPARE(harness.audio->configureTransitionCalls(), 1);
    QCOMPARE(harness.audio->lastTransitionConfig().autoAdvanceFadeMode, seriona::audio::AutoAdvanceFadeMode::ExceptGaplessGroup);

    // 播放中改 crossfadeMs（去抖）→ 400ms 后单次到达、末值正确
    settings.setCrossfadeMs(1500);
    settings.setCrossfadeMs(2500);
    QCOMPARE(harness.audio->configureTransitionCalls(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(harness.audio->configureTransitionCalls(), 2, 2000);
    QCOMPARE(harness.audio->lastTransitionConfig().crossfadeMs, std::chrono::milliseconds(2500));

    // 无整轨重载副作用：无 LoadTrack / 无设备重开（stop）/ 无 ConfigureOutput
    QCOMPARE(harness.audio->loadTrackCalls(), 0);
    QCOMPARE(harness.audio->stopCalls(), 0);
    QCOMPARE(harness.audio->configureOutputCalls(), 0);

    // 播放状态不被打断（仍 Playing，无 Loading/Stopped 事件泄漏到快照）
    QTRY_COMPARE(bridge.playerSnapshot().playback.state, seriona::control::PlaybackStatus::Playing);

    bridge.shutdown();
}

QTEST_GUILESS_MAIN(BackendBridgeTest)

#include "tst_backend_bridge.moc"
