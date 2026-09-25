#include "backend_bridge.h"

#include "lyric_split_boundary.h"
#include "settings_controller.h"

#include "seriona/audio/audio_contracts.h"
#include "seriona/control/folder_sort_settings_store.h"
#include "seriona/control/lyric_split_store.h"
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
#include <limits>
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

    void setEqualizer(const seriona::audio::EqualizerConfig &config) override
    {
        std::scoped_lock lock(m_mutex);
        ++m_setEqualizerCalls;
        m_lastEqualizerConfig = config;
    }

    void setSpectrumEnabled(bool enabled) override
    {
        std::scoped_lock lock(m_mutex);
        ++m_setSpectrumEnabledCalls;
        m_lastSpectrumEnabled = enabled;
    }

    bool spectrumEnabled() const override
    {
        std::scoped_lock lock(m_mutex);
        return m_lastSpectrumEnabled;
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

    seriona::audio::EqualizerConfig lastEqualizerConfig() const
    {
        std::scoped_lock lock(m_mutex);
        return m_lastEqualizerConfig;
    }

    int setEqualizerCalls() const
    {
        std::scoped_lock lock(m_mutex);
        return m_setEqualizerCalls;
    }

    int setSpectrumEnabledCalls() const
    {
        std::scoped_lock lock(m_mutex);
        return m_setSpectrumEnabledCalls;
    }

    bool lastSpectrumEnabled() const
    {
        std::scoped_lock lock(m_mutex);
        return m_lastSpectrumEnabled;
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
    seriona::audio::EqualizerConfig m_lastEqualizerConfig;
    bool m_lastSpectrumEnabled = false;
    int m_stopCalls = 0;
    int m_eventSinkClearCalls = 0;
    int m_configureOutputCalls = 0;
    int m_configureTransitionCalls = 0;
    int m_setEqualizerCalls = 0;
    int m_setSpectrumEnabledCalls = 0;
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

    bool removeRoot(const std::filesystem::path &) override { return true; }

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
    bool throwOnRegister = false;
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
        if (m_state->throwOnRegister) {
            throw std::runtime_error("metadata command registration failed");
        }

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

// 歌词切分 store 记录器：捕获后端实际落库的 manual 行/删除调用，供断言命令载荷
// （rawText/original/translation/convention）——含「约定取自当前快照」这一核心判据。
class RecordingLyricSplitStore final : public seriona::control::LyricSplitStore
{
public:
    struct RemoveCall {
        std::string rawText;
        std::string targetLanguage;
        seriona::control::LyricSplitConvention convention{seriona::control::LyricSplitConvention::None};
    };

    void putAuto(seriona::control::LyricSplitEntry) override {}

    void upsertManual(seriona::control::LyricSplitEntry entry) override
    {
        upsertCalls.push_back(std::move(entry));
    }

    std::optional<seriona::control::LyricSplitEntry> load(std::string_view,
                                                          std::string_view,
                                                          seriona::control::LyricSplitConvention) const override
    {
        return std::nullopt;
    }

    void removeManual(std::string_view rawText,
                      std::string_view targetLanguage,
                      seriona::control::LyricSplitConvention convention) override
    {
        removeCalls.push_back(RemoveCall{std::string(rawText), std::string(targetLanguage), convention});
    }

    void clearManual() override {}

    std::vector<seriona::control::LyricSplitEntry> listManual() const override
    {
        return {};
    }

    std::string algoVersion() const override
    {
        return "recording-lyric-split-store";
    }

    std::vector<seriona::control::LyricSplitEntry> upsertCalls;
    std::vector<RemoveCall> removeCalls;
};

struct ControllerHarness {
    std::shared_ptr<FakeAudioPlaybackService> audio = std::make_shared<FakeAudioPlaybackService>();
    std::shared_ptr<FakeFileScannerService> scanner = std::make_shared<FakeFileScannerService>();
    std::shared_ptr<FakeMetadataState> metadata = std::make_shared<FakeMetadataState>();
    std::shared_ptr<RecordingFolderSortSettingsStore> folderSortStore = std::make_shared<RecordingFolderSortSettingsStore>();
    // 歌词切分 store：默认空（装配期注入 Noop）；行级纠错用例注入 Recording 实现以捕获
    // 后端实际收到的 manual 行（含约定），从而断言命令载荷。
    std::shared_ptr<RecordingLyricSplitStore> lyricSplitStore;

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
        state->dependencies.lyricSplitStore = lyricSplitStore;
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

// 排空事件队列直到无待处理事件（慢机上跨线程事件可能在固定 50ms 排空后才到达；
// 先排空再断言，避免把迟到的合法投递误判为"泄漏/回调仍在跑"的假失败）。
// QEventLoop::processEvents 处理到队列空并返回"是否处理过事件"；每轮给跨线程
// 迟到投递 20ms 真实时间窗，多轮兜底，空队列即提前返回。
void drainEventsUntilIdle()
{
    QEventLoop loop;
    for (int i = 0; i < 10; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (!loop.processEvents(QEventLoop::AllEvents)) {
            return;
        }
        QTest::qWait(20);
    }
}

// 真实内嵌后端启动 + 跨线程首快照投递：慢机上可远超 QTRY 默认 5s，显式放宽预算。
void waitForInitialPlayerSnapshot(Seriona::App::BackendBridge &bridge)
{
    QSignalSpy playerSpy(&bridge, &Seriona::App::BackendBridge::playerSnapshotChanged);
    bridge.start();
    QTRY_VERIFY_WITH_TIMEOUT(playerSpy.count() > 0, 15000);
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
    QTRY_VERIFY_WITH_TIMEOUT(librarySpy.count() > 0, 10000);
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
    void startSurvivesMetadataFailure();
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
    void setLyricsTargetLanguageReachesBackendAndRejectsUnsupported();
    void lyricSplitCorrectionCommandsCarrySnapshotConvention();
    void lyricSplitBoundaryChainWritesConvertedPartsAndRepublishes();
    void enumeratePlaybackDevicesMapsDeviceIds();
    void settingsPushOnStart();
    void submitTransitionConfigBuildsTypedBackendCommand();
    void submitTransitionConfigRejectsInvalidPayloadWithoutDispatch();
    void transitionConfigWhilePlayingDoesNotReloadOrInterrupt();
    void submitEqualizerConfigBuildsTypedBackendCommand();
    void submitEqualizerConfigRejectsInvalidPayloadWithoutDispatch();
    void submitEqualizerConfigForwardsSpectrumEnabledCommand();
    void spectrumToggleDiscreteImmediateWithEqDebounceNotDuplicated();
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

    // 跨线程事件投递到主线程：显式放宽预算（默认 QTRY 5s 在慢机上等效很短）
    QTRY_VERIFY_WITH_TIMEOUT(playerSpy.count() > 0, 10000);
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
    drainEventsUntilIdle();
    QCOMPARE(playerSpy.count(), 0);

    bridge.reset();
    drainEventsUntilIdle();
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
    drainEventsUntilIdle();
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
    // 命令回调注册异常不在后端 metadata->start() 兜底 try/catch 内：启动硬失败。
    harness.metadata->throwOnRegister = true;
    Seriona::App::BackendBridge bridge(harness.factory(true));

    QSignalSpy playerSpy(&bridge, &Seriona::App::BackendBridge::playerSnapshotChanged);
    bridge.start();

    QCOMPARE(bridge.started(), false);
    QCOMPARE(bridge.shuttingDown(), true);
    QCOMPARE(harness.metadata->startCalls, 0);
    QCOMPARE(harness.metadata->unsubscribeCalls, 0);
    QCOMPARE(harness.metadata->stopCalls, 1);

    harness.audio->emitEvent(makePlaybackStateEvent(3, seriona::audio::PlaybackState::Playing));
    bridge.drainForTests();
    drainEventsUntilIdle();
    QCOMPARE(playerSpy.count(), 0);

    bridge.shutdown();
    bridge.shutdown();
    QCOMPARE(harness.audio->stopCalls(), 0);
    QCOMPARE(harness.metadata->stopCalls, 1);
}

void BackendBridgeTest::startSurvivesMetadataFailure()
{
    ControllerHarness harness;
    // metadata->start() 异常被后端兜底仅告警：平台控制面降级，不阻断启动。
    harness.metadata->throwOnStart = true;
    Seriona::App::BackendBridge bridge(harness.factory(true));
    waitForInitialPlayerSnapshot(bridge);

    QCOMPARE(bridge.started(), true);
    QCOMPARE(bridge.shuttingDown(), false);
    QCOMPARE(harness.metadata->startCalls, 1);

    bridge.shutdown();
    QCOMPARE(harness.metadata->unsubscribeCalls, 1);
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

void BackendBridgeTest::setLyricsTargetLanguageReachesBackendAndRejectsUnsupported()
{
    ControllerHarness harness;
    Seriona::App::BackendBridge bridge(harness.factory(true));
    waitForInitialPlayerSnapshot(bridge);

    // 白名单四 token 均送到后端 reducer 并获接受（accepted/code 只可能来自后端处理）
    const QStringList whitelist{QStringLiteral("zh"), QStringLiteral("ja"), QStringLiteral("ko"), QStringLiteral("en")};
    for (const QString &language : whitelist) {
        const seriona::control::MediaControllerCommandResult result = bridge.setLyricsTargetLanguage(language);
        QVERIFY(result.accepted);
        QCOMPARE(result.code, seriona::control::MediaControllerErrorCode::None);
    }

    // 白名单外：前端不校验，命令到达后端后被后端拒绝——证明连接的是后端白名单
    const seriona::control::MediaControllerCommandResult unsupported =
        bridge.setLyricsTargetLanguage(QStringLiteral("fr"));
    QCOMPARE(unsupported.accepted, false);
    QCOMPARE(unsupported.code, seriona::control::MediaControllerErrorCode::InvalidCommand);

    // 语言命令不触碰音频输出/加载/停止（与 ConfigureOutput 语义隔离）
    QCOMPARE(harness.audio->configureOutputCalls(), 0);
    QCOMPARE(harness.audio->loadTrackCalls(), 0);
    QCOMPARE(harness.audio->stopCalls(), 0);

    bridge.shutdown();
}

void BackendBridgeTest::lyricSplitCorrectionCommandsCarrySnapshotConvention()
{
    ControllerHarness harness;
    harness.lyricSplitStore = std::make_shared<RecordingLyricSplitStore>();
    Seriona::App::BackendBridge bridge(harness.factory(true));
    waitForInitialPlayerSnapshot(bridge);
    drainEventsUntilIdle();

    // 当前快照的约定必须非 None：S: /  = StrongSlashSpaced（token "S: / "）。
    // 每次提交前就地注入，保证命令读到的就是这份快照（排空/重发布不会引入竞态）。
    seriona::control::TrackLyricsSnapshot injected;
    injected.trackId = "track-lyric";
    injected.targetLanguage = "zh";
    injected.convention = seriona::control::LyricSplitConvention::StrongSlashSpaced;
    const auto injectConvention = [&bridge, &injected] {
        bridge.applyTrackLyricsSnapshotForTests(injected);
        QCOMPARE(bridge.trackLyricsSnapshot().convention, seriona::control::LyricSplitConvention::StrongSlashSpaced);
    };

    QSignalSpy lyricsSpy(&bridge, &Seriona::App::BackendBridge::trackLyricsChanged);

    // 修正原文/译文：命令被发出 → 后端接受 → store 收到 manual 行，约定取自当前快照。
    injectConvention();
    const seriona::control::MediaControllerCommandResult upsert =
        bridge.upsertLyricSplitCorrection(QStringLiteral("raw / line"), QStringLiteral("raw"), QStringLiteral("句"));
    QVERIFY(upsert.accepted);
    QCOMPARE(harness.lyricSplitStore->upsertCalls.size(), std::size_t{1});
    const auto &upsertEntry = harness.lyricSplitStore->upsertCalls.front();
    QCOMPARE(QString::fromStdString(upsertEntry.rawText), QStringLiteral("raw / line"));
    QCOMPARE(QString::fromStdString(upsertEntry.original), QStringLiteral("raw"));
    QCOMPARE(QString::fromStdString(upsertEntry.translation), QStringLiteral("句"));
    QCOMPARE(QString::fromStdString(seriona::control::conventionToken(upsertEntry.convention)), QStringLiteral("S: / "));
    // 时机③：命令成功后后端重发布一次完整快照。
    QTRY_VERIFY_WITH_TIMEOUT(lyricsSpy.count() > 0, 10000);

    // 此行无译文：保留当前原文，译文为显式空串（不折叠成缺字段）。
    injectConvention();
    const seriona::control::MediaControllerCommandResult emptyTranslation =
        bridge.upsertLyricSplitCorrection(QStringLiteral("raw / line"), QStringLiteral("raw"), QString());
    QVERIFY(emptyTranslation.accepted);
    QCOMPARE(harness.lyricSplitStore->upsertCalls.size(), std::size_t{2});
    QCOMPARE(QString::fromStdString(harness.lyricSplitStore->upsertCalls.back().translation), QString());
    QCOMPARE(QString::fromStdString(seriona::control::conventionToken(harness.lyricSplitStore->upsertCalls.back().convention)),
             QStringLiteral("S: / "));

    // 恢复本行自动识别：Remove 命令同样携带当前快照约定。
    injectConvention();
    const seriona::control::MediaControllerCommandResult remove =
        bridge.removeLyricSplitCorrection(QStringLiteral("raw / line"));
    QVERIFY(remove.accepted);
    QCOMPARE(harness.lyricSplitStore->removeCalls.size(), std::size_t{1});
    QCOMPARE(QString::fromStdString(harness.lyricSplitStore->removeCalls.front().rawText), QStringLiteral("raw / line"));
    QCOMPARE(QString::fromStdString(seriona::control::conventionToken(harness.lyricSplitStore->removeCalls.front().convention)),
             QStringLiteral("S: / "));

    // 空 rawText 本地拒绝，不外发（store 不再收到新调用）。
    injectConvention();
    const seriona::control::MediaControllerCommandResult rejected =
        bridge.upsertLyricSplitCorrection(QString(), QStringLiteral("raw"), QStringLiteral("句"));
    QCOMPARE(rejected.accepted, false);
    QCOMPARE(harness.lyricSplitStore->upsertCalls.size(), std::size_t{2});
    QCOMPARE(harness.lyricSplitStore->removeCalls.size(), std::size_t{1});

    bridge.shutdown();
}

// todo 33：整首纠错窗口拖动分界的链路——窗口口径的初始分界 → 前端换算出的
// (original, translation) → 经命令外发且被后端接受 → store 收到换算后的两段、
// 约定取自当前快照 → 快照按时机③重发布。
// 初始分界由 findLyricSplitBoundaryCut 反解（不是手写 indexOf），并断言它复现当前展示对。
void BackendBridgeTest::lyricSplitBoundaryChainWritesConvertedPartsAndRepublishes()
{
    ControllerHarness harness;
    harness.lyricSplitStore = std::make_shared<RecordingLyricSplitStore>();
    Seriona::App::BackendBridge bridge(harness.factory(true));
    waitForInitialPlayerSnapshot(bridge);
    drainEventsUntilIdle();

    seriona::control::TrackLyricsSnapshot injected;
    injected.trackId = "track-boundary";
    injected.targetLanguage = "zh";
    injected.convention = seriona::control::LyricSplitConvention::StrongSlashSpaced;
    bridge.applyTrackLyricsSnapshotForTests(injected);
    QCOMPARE(bridge.trackLyricsSnapshot().convention, seriona::control::LyricSplitConvention::StrongSlashSpaced);

    QSignalSpy lyricsSpy(&bridge, &Seriona::App::BackendBridge::trackLyricsChanged);

    const QString rawLine = QStringLiteral("揺るぎない Spirit / 坚定不移的Spirit");
    const QString currentOriginal = QStringLiteral("揺るぎない Spirit");
    const QString currentTranslation = QStringLiteral("坚定不移的Spirit");

    // 窗口口径的初始分界：反解出的分界必须复现当前展示对，否则「未拖动即保存」会写错值。
    const Seriona::App::LyricSplitBoundaryCut initialCut =
        Seriona::App::findLyricSplitBoundaryCut(rawLine, currentOriginal, currentTranslation);
    QVERIFY(initialCut.valid);
    const Seriona::App::LyricSplitBoundaryParts parts =
        Seriona::App::splitLyricLineAtBoundary(rawLine, initialCut);
    QCOMPARE(parts.original, currentOriginal);
    QCOMPARE(parts.translation, currentTranslation);
    QVERIFY(parts.valid);
    QCOMPARE(Seriona::App::boundaryDiffersFromCurrent(parts, currentOriginal, currentTranslation), false);

    // 无操作：用户「不拖动、直接保存」时，上层门函数不发命令。
    // 门谓词由生产头文件提供（与 AppFacade::commitLyricSplitBoundary 同源），
    // 不是恒真式：这里用当前展示对作为「当前值」调用真实门谓词，断言它拒绝提交。
    // （S19 A3：此前的写法 boundaryDiffersFromCurrent(parts, parts.original, parts.translation)
    //  恒为 false，if 体是死代码，断言不可能失败，等于假覆盖。）
    QCOMPARE(Seriona::App::lyricSplitBoundaryShouldCommit(parts, currentOriginal, currentTranslation), false);

    // 门谓词必须能判负（否则又是恒真）：同一 parts 换成不同「当前值」即应放行。
    QCOMPARE(Seriona::App::lyricSplitBoundaryShouldCommit(parts, currentOriginal, QStringLiteral("旧译")), true);
    // 原文为空（含全空白）时门谓词同样拒绝（与行级弹窗 A6 同口径）。
    const Seriona::App::LyricSplitBoundaryParts blankOriginal =
        Seriona::App::splitLyricLineAtBoundary(rawLine, 0);
    QCOMPARE(blankOriginal.valid, false);
    QCOMPARE(Seriona::App::lyricSplitBoundaryShouldCommit(blankOriginal, currentOriginal, currentTranslation), false);

    QCOMPARE(harness.lyricSplitStore->upsertCalls.size(), std::size_t{0});

    // 真实变更：模拟一次拖动（单点分界）→ 换算出的两段与预览一致 → 命令外发 → 后端接受
    // → store 收到这两段。拖动所见与所写同源（不做任何再切分）。
    const int draggedIndex = initialCut.rightStart;
    const Seriona::App::LyricSplitBoundaryParts edited =
        Seriona::App::splitLyricLineAtBoundary(rawLine, draggedIndex);
    QVERIFY(edited.valid);
    QCOMPARE(edited.original, QStringLiteral("揺るぎない Spirit /"));
    QCOMPARE(edited.translation, currentTranslation);
    QCOMPARE(Seriona::App::boundaryDiffersFromCurrent(edited, currentOriginal, currentTranslation), true);

    const seriona::control::MediaControllerCommandResult result =
        bridge.upsertLyricSplitCorrection(rawLine, edited.original, edited.translation);
    QVERIFY(result.accepted);
    QCOMPARE(harness.lyricSplitStore->upsertCalls.size(), std::size_t{1});
    const auto &entry = harness.lyricSplitStore->upsertCalls.back();
    QCOMPARE(QString::fromStdString(entry.rawText), rawLine);
    QCOMPARE(QString::fromStdString(entry.original), edited.original);
    QCOMPARE(QString::fromStdString(entry.translation), edited.translation);
    QCOMPARE(QString::fromStdString(seriona::control::conventionToken(entry.convention)), QStringLiteral("S: / "));
    QTRY_VERIFY_WITH_TIMEOUT(lyricsSpy.count() > 0, 10000);

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
    QTRY_COMPARE_WITH_TIMEOUT(harness.audio->configureTransitionCalls(), 2, 8000);
    QCOMPARE(harness.audio->lastTransitionConfig().crossfadeMs, std::chrono::milliseconds(2500));

    // 无整轨重载副作用：无 LoadTrack / 无设备重开（stop）/ 无 ConfigureOutput
    QCOMPARE(harness.audio->loadTrackCalls(), 0);
    QCOMPARE(harness.audio->stopCalls(), 0);
    QCOMPARE(harness.audio->configureOutputCalls(), 0);

    // 播放状态不被打断（仍 Playing，无 Loading/Stopped 事件泄漏到快照）
    QTRY_COMPARE(bridge.playerSnapshot().playback.state, seriona::control::PlaybackStatus::Playing);

    bridge.shutdown();
}

void BackendBridgeTest::submitEqualizerConfigBuildsTypedBackendCommand()
{
    ControllerHarness harness;
    Seriona::App::BackendBridge bridge(harness.factory(true));
    waitForInitialPlayerSnapshot(bridge);

    // 31 档全量：6 参按 EqualizerConfig 字段声明序组包 → SetEqualizerConfig 到达音频服务
    QVariantList gains31;
    gains31.reserve(31);
    for (int i = 0; i < 31; ++i) {
        gains31.append((i - 15) * 0.5);
    }
    const seriona::control::MediaControllerCommandResult full = bridge.submitEqualizerConfig(
        true, 31, -3.5, gains31, true, false);
    QVERIFY(full.accepted);
    QCOMPARE(full.code, seriona::control::MediaControllerErrorCode::None);
    QCOMPARE(harness.audio->setEqualizerCalls(), 1);
    const seriona::audio::EqualizerConfig fullConfig = harness.audio->lastEqualizerConfig();
    QCOMPARE(fullConfig.enabled, true);
    QCOMPARE(fullConfig.mode, seriona::audio::EqualizerBandMode::Band31);
    QCOMPARE(fullConfig.preGainDb, -3.5f);
    for (int i = 0; i < 31; ++i) {
        QCOMPARE(fullConfig.bandGainsDb[static_cast<std::size_t>(i)], static_cast<float>((i - 15) * 0.5));
    }
    QCOMPARE(fullConfig.limiterEnabled, true);

    // 10 档：mode=Band10；bandGainsDb 恒 31 定长，仅前 10 项拷贝生效，余 21 项零填充
    QVariantList gains10;
    gains10.reserve(10);
    for (int i = 0; i < 10; ++i) {
        gains10.append(2.5 + i * 0.5);
    }
    const seriona::control::MediaControllerCommandResult ten = bridge.submitEqualizerConfig(
        false, 10, 2.5, gains10, false, true);
    QVERIFY(ten.accepted);
    QCOMPARE(ten.code, seriona::control::MediaControllerErrorCode::None);
    QCOMPARE(harness.audio->setEqualizerCalls(), 2);
    const seriona::audio::EqualizerConfig tenConfig = harness.audio->lastEqualizerConfig();
    QCOMPARE(tenConfig.enabled, false);
    QCOMPARE(tenConfig.mode, seriona::audio::EqualizerBandMode::Band10);
    QCOMPARE(tenConfig.preGainDb, 2.5f);
    QCOMPARE(tenConfig.limiterEnabled, false);
    for (int i = 0; i < 10; ++i) {
        QCOMPARE(tenConfig.bandGainsDb[static_cast<std::size_t>(i)], static_cast<float>(2.5 + i * 0.5));
    }
    for (int i = 10; i < 31; ++i) {
        QCOMPARE(tenConfig.bandGainsDb[static_cast<std::size_t>(i)], 0.0f);
    }

    // R3：spectrum 位首次（false）同步一次、再变化 true 补发一次——payload bool 直抵
    // 音频服务（经内嵌后端 SetSpectrumEnabled 命令链），EQ 组包不受拆分影响
    QCOMPARE(harness.audio->setSpectrumEnabledCalls(), 2);
    QCOMPARE(harness.audio->lastSpectrumEnabled(), true);

    // 语义隔离：EQ 命令不触发 ConfigureOutput/SetTransitionConfig/LoadTrack/stop
    QCOMPARE(harness.audio->configureOutputCalls(), 0);
    QCOMPARE(harness.audio->configureTransitionCalls(), 0);
    QCOMPARE(harness.audio->loadTrackCalls(), 0);
    QCOMPARE(harness.audio->stopCalls(), 0);

    bridge.shutdown();
}

void BackendBridgeTest::submitEqualizerConfigRejectsInvalidPayloadWithoutDispatch()
{
    ControllerHarness harness;
    Seriona::App::BackendBridge bridge(harness.factory(true));
    waitForInitialPlayerSnapshot(bridge);

    QSignalSpy notifySpy(&bridge, &Seriona::App::BackendBridge::domainNotificationQueued);
    const std::size_t baselineNotifications = bridge.notifications().size();

    QVariantList valid10;
    valid10.reserve(10);
    for (int i = 0; i < 10; ++i) {
        valid10.append(0.5);
    }

    // bandMode 越界（仅 10/31）
    const seriona::control::MediaControllerCommandResult badMode = bridge.submitEqualizerConfig(
        false, 7, 0.0, valid10, false, false);
    QVERIFY(!badMode.accepted);
    QCOMPARE(badMode.code, seriona::control::MediaControllerErrorCode::InvalidCommand);

    // preGain 越界（±15）与 NaN
    QVariantList valid31;
    valid31.reserve(31);
    for (int i = 0; i < 31; ++i) {
        valid31.append(0.0);
    }
    const seriona::control::MediaControllerCommandResult highGain = bridge.submitEqualizerConfig(
        false, 31, 16.0, valid31, false, false);
    QVERIFY(!highGain.accepted);
    QCOMPARE(highGain.code, seriona::control::MediaControllerErrorCode::InvalidCommand);
    const seriona::control::MediaControllerCommandResult lowGain = bridge.submitEqualizerConfig(
        false, 31, -16.0, valid31, false, false);
    QVERIFY(!lowGain.accepted);
    const seriona::control::MediaControllerCommandResult nanGain = bridge.submitEqualizerConfig(
        false, 31, std::numeric_limits<double>::quiet_NaN(), valid31, false, false);
    QVERIFY(!nanGain.accepted);
    QCOMPARE(nanGain.code, seriona::control::MediaControllerErrorCode::InvalidCommand);

    // 增益长度与档位错配（10 档 9 项 / 31 档 10 项）
    const QVariantList short10 = valid10.mid(0, 9);
    const seriona::control::MediaControllerCommandResult shortGains = bridge.submitEqualizerConfig(
        false, 10, 0.0, short10, false, false);
    QVERIFY(!shortGains.accepted);
    QCOMPARE(shortGains.code, seriona::control::MediaControllerErrorCode::InvalidCommand);
    const seriona::control::MediaControllerCommandResult wrongModeLength = bridge.submitEqualizerConfig(
        false, 31, 0.0, valid10, false, false);
    QVERIFY(!wrongModeLength.accepted);
    QCOMPARE(wrongModeLength.code, seriona::control::MediaControllerErrorCode::InvalidCommand);

    // 逐项越界（15.5 / -15.5 / NaN）
    QVariantList over = valid10;
    over[3] = 15.5;
    const seriona::control::MediaControllerCommandResult itemOver = bridge.submitEqualizerConfig(
        false, 10, 0.0, over, false, false);
    QVERIFY(!itemOver.accepted);
    QCOMPARE(itemOver.code, seriona::control::MediaControllerErrorCode::InvalidCommand);
    QVariantList under = valid10;
    under[0] = -15.5;
    const seriona::control::MediaControllerCommandResult itemUnder = bridge.submitEqualizerConfig(
        false, 10, 0.0, under, false, false);
    QVERIFY(!itemUnder.accepted);
    QVariantList nanItem = valid10;
    nanItem[1] = std::numeric_limits<double>::quiet_NaN();
    const seriona::control::MediaControllerCommandResult itemNan = bridge.submitEqualizerConfig(
        false, 10, 0.0, nanItem, false, false);
    QVERIFY(!itemNan.accepted);

    // 全部本地拒绝：零外发 + 每次拒绝入队一条 CommandRejected 通知
    // （R3：频谱命令与 EQ 同函数同弃——本地 reject 早退于 spectrum 段之前，
    // 非法载荷不产生 SetSpectrumEnabled 外发）
    QCOMPARE(harness.audio->setEqualizerCalls(), 0);
    QCOMPARE(harness.audio->setSpectrumEnabledCalls(), 0);
    QCOMPARE(harness.audio->configureTransitionCalls(), 0);
    QCOMPARE(notifySpy.count(), 9);
    QCOMPARE(bridge.notifications().size(), baselineNotifications + 9);
    QCOMPARE(bridge.notifications().back().kind, seriona::control::ControlDomainNotificationKind::CommandRejected);
    QCOMPARE(bridge.notifications().back().errorCode, seriona::control::MediaControllerErrorCode::InvalidCommand);

    bridge.shutdown();
}

void BackendBridgeTest::submitEqualizerConfigForwardsSpectrumEnabledCommand()
{
    ControllerHarness harness;
    Seriona::App::BackendBridge bridge(harness.factory(true));
    waitForInitialPlayerSnapshot(bridge);

    QVariantList gains31;
    gains31.reserve(31);
    for (int i = 0; i < 31; ++i) {
        gains31.append((i - 15) * 0.5);
    }

    // 首次提交（spectrum=true）：EQ 命令照发 + SetSpectrumEnabled(true) 独立外发
    // （R3 真命令化：经内嵌后端 reducer 直转 → 音频服务 setSpectrumEnabled 记账）
    const seriona::control::MediaControllerCommandResult first = bridge.submitEqualizerConfig(
        true, 31, -3.5, gains31, true, true);
    QVERIFY(first.accepted);
    QCOMPARE(first.code, seriona::control::MediaControllerErrorCode::None);
    QCOMPARE(harness.audio->setEqualizerCalls(), 1);
    QCOMPARE(harness.audio->setSpectrumEnabledCalls(), 1);
    QCOMPARE(harness.audio->lastSpectrumEnabled(), true);

    // spectrum 位未变（如 EQ 去抖到期重复提交全量现值）：EQ 照发、频谱不重发
    const seriona::control::MediaControllerCommandResult repeat = bridge.submitEqualizerConfig(
        true, 31, -2.0, gains31, true, true);
    QVERIFY(repeat.accepted);
    QCOMPARE(harness.audio->setEqualizerCalls(), 2);
    QCOMPARE(harness.audio->setSpectrumEnabledCalls(), 1);
    QCOMPARE(harness.audio->lastSpectrumEnabled(), true);

    // 位变化 true→false：SetSpectrumEnabled(false) 再发一次，EQ 同步照发
    const seriona::control::MediaControllerCommandResult off = bridge.submitEqualizerConfig(
        true, 31, -2.0, gains31, true, false);
    QVERIFY(off.accepted);
    QCOMPARE(harness.audio->setEqualizerCalls(), 3);
    QCOMPARE(harness.audio->setSpectrumEnabledCalls(), 2);
    QCOMPARE(harness.audio->lastSpectrumEnabled(), false);
    QCOMPARE(harness.audio->lastEqualizerConfig().preGainDb, -2.0f);

    // 语义隔离：EQ/频谱命令均不触发输出重载/过渡/整轨加载/停止
    QCOMPARE(harness.audio->configureOutputCalls(), 0);
    QCOMPARE(harness.audio->configureTransitionCalls(), 0);
    QCOMPARE(harness.audio->loadTrackCalls(), 0);
    QCOMPARE(harness.audio->stopCalls(), 0);

    bridge.shutdown();
}

void BackendBridgeTest::spectrumToggleDiscreteImmediateWithEqDebounceNotDuplicated()
{
    ControllerHarness harness;
    Seriona::App::BackendBridge bridge(harness.factory(true));
    waitForInitialPlayerSnapshot(bridge);

    // 仿真 AppFacade 注入：SettingsController 全量 EQ executor 透传桥层
    Seriona::App::SettingsController settings;
    settings.setApplyEqualizerConfigExecutor([&](bool enabled, int bandMode, double preGainDb,
                                                 const QVariantList &bandGains, bool limiterEnabled,
                                                 bool spectrumEnabled) {
        return bridge.submitEqualizerConfig(enabled, bandMode, preGainDb, bandGains, limiterEnabled,
                                            spectrumEnabled);
    });

    QVariantList gains31;
    gains31.reserve(31);
    for (int i = 0; i < 31; ++i) {
        gains31.append((i - 15) * 0.5);
    }
    // 基线默认：enabled=false/bandMode=10/频谱 false。enabled 打开 = 立即项 → 首个
    // 全量推送（EQ 1 次 + 频谱首次同步 false 1 次——缓存 nullopt → 首次即发）。
    settings.setEnabled(true);
    QCOMPARE(harness.audio->setEqualizerCalls(), 1);
    QCOMPARE(harness.audio->setSpectrumEnabledCalls(), 1);
    QCOMPARE(harness.audio->lastSpectrumEnabled(), false);
    // 档位 10→31 = 立即项 → EQ 再推，spectrum 位未变 → 频谱零新增
    settings.setBandMode(31);
    QCOMPARE(harness.audio->setEqualizerCalls(), 2);
    QCOMPARE(harness.audio->setSpectrumEnabledCalls(), 1);

    // 频谱开关点击（离散立即项，不经 50ms 去抖）→ SetSpectrumEnabled(true) 单发，
    // EQ 全量载荷同步即时推送
    settings.setBandGains31(gains31);
    settings.setSpectrumEnabled(true);
    QCOMPARE(harness.audio->setEqualizerCalls(), 3);
    QCOMPARE(harness.audio->setSpectrumEnabledCalls(), 2);
    QCOMPARE(harness.audio->lastSpectrumEnabled(), true);

    // 增益拖动（50ms 去抖项）到期 → EQ 全量现值重推：spectrum 位未变 → 频谱零重复
    QTRY_COMPARE_WITH_TIMEOUT(harness.audio->setEqualizerCalls(), 4, 2000);
    QCOMPARE(harness.audio->setSpectrumEnabledCalls(), 2);

    // 开关再点击关闭 → SetSpectrumEnabled(false) 立即单发
    settings.setSpectrumEnabled(false);
    QCOMPARE(harness.audio->setEqualizerCalls(), 5);
    QCOMPARE(harness.audio->setSpectrumEnabledCalls(), 3);
    QCOMPARE(harness.audio->lastSpectrumEnabled(), false);

    // 语义隔离：无输出重载/过渡/整轨加载/停止副作用
    QCOMPARE(harness.audio->configureOutputCalls(), 0);
    QCOMPARE(harness.audio->configureTransitionCalls(), 0);
    QCOMPARE(harness.audio->loadTrackCalls(), 0);
    QCOMPARE(harness.audio->stopCalls(), 0);

    bridge.shutdown();
}

QTEST_GUILESS_MAIN(BackendBridgeTest)

#include "tst_backend_bridge.moc"
