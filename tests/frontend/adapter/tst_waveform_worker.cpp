#include "app_facade.h"
#include "waveform_provider.h"

#include "seriona/control/control_contracts.h"

#include <QSemaphore>
#include <QSignalSpy>
#include <QtTest/QTest>

#include <chrono>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Seriona::App::WaveformBuildInput;
using Seriona::App::WaveformParameters;
using Seriona::App::WaveformPayload;
using Seriona::App::WaveformProvider;
using Seriona::App::WaveformRequest;
using Seriona::App::WaveformResult;

WaveformParameters testParameters()
{
    WaveformParameters parameters = Seriona::App::defaultWaveformParameters();
    parameters.barCount = 4;
    parameters.totalWidth = 40;
    parameters.maxHeight = 20;
    return parameters;
}

WaveformRequest testRequest(const QString &trackId)
{
    WaveformRequest request;
    request.trackId = trackId;
    request.waveformFilePath = QStringLiteral("/music/source.flac");
    request.offsetUS = 12000000;
    request.durationUS = 34000000;
    request.startTimeUS = 12000000;
    request.endTimeUS = 46000000;
    request.parameters = testParameters();
    return request;
}

WaveformResult resultAt(const QSignalSpy &spy, qsizetype index)
{
    return qvariant_cast<WaveformResult>(spy.at(index).at(0));
}

seriona::control::SplitLyricLine snapshotLine(std::chrono::milliseconds timestamp,
                                              const std::string &text,
                                              const std::string &original,
                                              const std::string &translation)
{
    seriona::control::SplitLyricLine line;
    line.timestamp = timestamp;
    line.text = text;
    line.original = original;
    line.translation = translation;
    line.split = !translation.empty();
    return line;
}

seriona::control::TrackLyricsSnapshot trackLyricsSnapshot(
    const std::string &trackId,
    const std::vector<seriona::control::SplitLyricLine> &lines)
{
    seriona::control::TrackLyricsSnapshot snapshot;
    snapshot.trackId = trackId;
    snapshot.targetLanguage = "zh";
    snapshot.lines = lines;
    return snapshot;
}

QString lyricsDisplayLine(const Seriona::App::LyricsModel &model, int row)
{
    return model.data(model.index(row, 0), Seriona::App::LyricsModel::DisplayLineRole).toString();
}

}

class WaveformWorkerTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void workerCallsBackendToolSignatureAndAppliesBarWidth();
    void cacheKeyUsesTrackIdentityParametersAndImmutableDefaultConfig();
    void cueMetadataChoosesSourceFilePathAndTimeWindow();
    void cueTrackDefersWaveformGenerationUntilLibraryMetadataArrives();
    void staleWorkerResultIsIgnored();
    void emptyTrackLyricsSnapshotClearsPreviousContent();
    void missingFileFailureReturnsEmptyWaveform();
};

void WaveformWorkerTest::initTestCase()
{
    qRegisterMetaType<WaveformResult>();
}

void WaveformWorkerTest::workerCallsBackendToolSignatureAndAppliesBarWidth()
{
    std::vector<WaveformBuildInput> calls;
    WaveformProvider provider;
    provider.setGeneratorForTests([&calls](const WaveformBuildInput &input) {
        calls.push_back(input);
        return WaveformPayload{{12, 18, 9, 6}, 7};
    });
    QSignalSpy readySpy(&provider, &WaveformProvider::waveformReady);

    static_cast<void>(provider.requestWaveform(testRequest(QStringLiteral("track-signature"))));

    QTRY_COMPARE(readySpy.count(), 1);
    QCOMPARE(calls.size(), static_cast<std::size_t>(1));
    const WaveformBuildInput &input = calls.front();
    QCOMPARE(input.trackId, QStringLiteral("track-signature"));
    QCOMPARE(input.waveformFilePath, QStringLiteral("/music/source.flac"));
    QCOMPARE(input.barCount, 4);
    QCOMPARE(input.totalWidth, 40);
    QCOMPARE(input.maxHeight, 20);
    QCOMPARE(input.startTimeUS, 12000000);
    QCOMPARE(input.endTimeUS, 46000000);
    QCOMPARE(input.config.dbFloor, -55.0F);
    QCOMPARE(input.config.dbCeiling, 0.0F);
    QCOMPARE(input.config.enableSIMD, true);
    QCOMPARE(input.config.threadCount, 0);

    const WaveformResult result = resultAt(readySpy, 0);
    QCOMPARE(result.heights, QVariantList({12, 18, 9, 6}));
    QCOMPARE(result.barWidth, 7);

    Seriona::App::PlaybackController controller;
    QSignalSpy heightsSpy(&controller, &Seriona::App::PlaybackController::waveformHeightsChanged);
    QSignalSpy barWidthSpy(&controller, &Seriona::App::PlaybackController::waveformBarWidthChanged);
    controller.applyWaveform(result.heights, result.barWidth);

    QCOMPARE(controller.waveformHeights(), QVariantList({12, 18, 9, 6}));
    QCOMPARE(controller.waveformBarWidth(), 7);
    QCOMPARE(heightsSpy.count(), 1);
    QCOMPARE(barWidthSpy.count(), 1);
}

void WaveformWorkerTest::cacheKeyUsesTrackIdentityParametersAndImmutableDefaultConfig()
{
    WaveformRequest request = testRequest(QStringLiteral("track-cache"));
    WaveformRequest sameRequest = request;
    WaveformRequest changedTrack = request;
    changedTrack.trackId = QStringLiteral("track-cache-other");
    WaveformRequest changedConfig = request;
    changedConfig.parameters.config.dbFloor = -48.0F;

    QCOMPARE(request.cacheKey(), sameRequest.cacheKey());
    QVERIFY(request.cacheKey() != changedTrack.cacheKey());
    QVERIFY(request.cacheKey() != changedConfig.cacheKey());

    int callCount = 0;
    WaveformProvider provider;
    provider.setGeneratorForTests([&callCount](const WaveformBuildInput &) {
        ++callCount;
        return WaveformPayload{{3, 4, 5, 6}, 5};
    });
    QSignalSpy readySpy(&provider, &WaveformProvider::waveformReady);

    static_cast<void>(provider.requestWaveform(request));
    QTRY_COMPARE(readySpy.count(), 1);
    static_cast<void>(provider.requestWaveform(sameRequest));
    QTRY_COMPARE(readySpy.count(), 2);

    QCOMPARE(callCount, 1);
    QCOMPARE(resultAt(readySpy, 0).cacheHit, false);
    QCOMPARE(resultAt(readySpy, 1).cacheHit, true);
}

void WaveformWorkerTest::cueMetadataChoosesSourceFilePathAndTimeWindow()
{
    seriona::control::PlayerStateSnapshot player;
    player.currentTrack = seriona::control::TrackIdentity{};
    player.currentTrack->trackId = "cue-track";
    player.currentTrack->filePath = "/music/cue-sheet.cue";

    const seriona::control::LibraryStateSnapshot emptyLibrary;
    QVERIFY(!Seriona::App::makeWaveformRequest(player, emptyLibrary, testParameters()).has_value());

    seriona::scanner::SongMetadata cueSong;
    cueSong.trackId = "cue-track";
    cueSong.filePath = "/music/cue-sheet.cue";
    cueSong.sourceFilePath = "/music/source-album.flac";
    cueSong.offset = std::chrono::milliseconds{12000};
    cueSong.duration = std::chrono::milliseconds{34000};

    seriona::scanner::PlaylistNode cueNode;
    cueNode.nodeId = "node-cue-track";
    cueNode.song = cueSong;

    seriona::scanner::SongMetadata normalSong;
    normalSong.trackId = "normal-track";
    normalSong.filePath = "/music/normal.flac";

    seriona::scanner::PlaylistNode normalNode;
    normalNode.nodeId = "node-normal-track";
    normalNode.song = normalSong;

    seriona::control::LibraryStateSnapshot library;
    library.libraryTree = seriona::scanner::PlaylistTreeSnapshot{};
    library.libraryTree->nodes = {cueNode, normalNode};

    const std::optional<WaveformRequest> cueRequest = Seriona::App::makeWaveformRequest(player, library, testParameters());
    QVERIFY(cueRequest.has_value());
    QCOMPARE(cueRequest->trackId, QStringLiteral("cue-track"));
    QCOMPARE(cueRequest->waveformFilePath, QStringLiteral("/music/source-album.flac"));
    QCOMPARE(cueRequest->offsetUS, 12000000);
    QCOMPARE(cueRequest->durationUS, 34000000);
    QCOMPARE(cueRequest->startTimeUS, 12000000);
    QCOMPARE(cueRequest->endTimeUS, 46000000);

    player.currentTrack->trackId = "normal-track";
    const std::optional<WaveformRequest> normalRequest = Seriona::App::makeWaveformRequest(player, library, testParameters());
    QVERIFY(normalRequest.has_value());
    QCOMPARE(normalRequest->waveformFilePath, QStringLiteral("/music/normal.flac"));
    QCOMPARE(normalRequest->startTimeUS, 0);
    QCOMPARE(normalRequest->endTimeUS, 0);
}

void WaveformWorkerTest::cueTrackDefersWaveformGenerationUntilLibraryMetadataArrives()
{
    WaveformProvider provider;
    int generatorCalls = 0;
    provider.setGeneratorForTests([&generatorCalls](const WaveformBuildInput &input) -> WaveformPayload {
        ++generatorCalls;
        if (input.trackId != QStringLiteral("cue-track")) {
            throw std::runtime_error("unexpected track id");
        }
        if (input.waveformFilePath != QStringLiteral("/music/source-album.flac")) {
            throw std::runtime_error("unexpected waveform path");
        }
        if (input.startTimeUS != 12000000 || input.endTimeUS != 46000000) {
            throw std::runtime_error("unexpected waveform time window");
        }
        return WaveformPayload{{5, 4, 3, 2}, 6};
    });
    QSignalSpy readySpy(&provider, &WaveformProvider::waveformReady);

    seriona::control::PlayerStateSnapshot player;
    player.currentTrack = seriona::control::TrackIdentity{};
    player.currentTrack->trackId = "cue-track";
    player.currentTrack->filePath = "/music/cue-sheet.cue";

    const seriona::control::LibraryStateSnapshot emptyLibrary;
    static_cast<void>(provider.requestForSnapshots(player, emptyLibrary));

    QTRY_COMPARE(readySpy.count(), 1);
    QCOMPARE(generatorCalls, 0);
    QCOMPARE(resultAt(readySpy, 0).heights, QVariantList{});

    seriona::scanner::SongMetadata cueSong;
    cueSong.trackId = "cue-track";
    cueSong.filePath = "/music/cue-sheet.cue";
    cueSong.sourceFilePath = "/music/source-album.flac";
    cueSong.offset = std::chrono::milliseconds{12000};
    cueSong.duration = std::chrono::milliseconds{34000};

    seriona::scanner::PlaylistNode cueNode;
    cueNode.nodeId = "node-cue-track";
    cueNode.song = cueSong;

    seriona::control::LibraryStateSnapshot library;
    library.libraryTree = seriona::scanner::PlaylistTreeSnapshot{};
    library.libraryTree->nodes = {cueNode};

    static_cast<void>(provider.requestForSnapshots(player, library));

    QTRY_COMPARE(readySpy.count(), 2);
    QCOMPARE(generatorCalls, 1);
    QCOMPARE(resultAt(readySpy, 1).heights, QVariantList({5, 4, 3, 2}));
    QCOMPARE(resultAt(readySpy, 1).barWidth, 6);
}

void WaveformWorkerTest::staleWorkerResultIsIgnored()
{
    QSemaphore slowEntered;
    QSemaphore releaseSlow;
    WaveformProvider provider;
    provider.setGeneratorForTests([&slowEntered, &releaseSlow](const WaveformBuildInput &input) {
        if (input.trackId == QStringLiteral("slow-track")) {
            slowEntered.release();
            releaseSlow.acquire();
            return WaveformPayload{{1, 1, 1, 1}, 1};
        }
        return WaveformPayload{{9, 8, 7, 6}, 4};
    });
    QSignalSpy readySpy(&provider, &WaveformProvider::waveformReady);

    static_cast<void>(provider.requestWaveform(testRequest(QStringLiteral("slow-track"))));
    QVERIFY(slowEntered.tryAcquire(1, 8000));
    static_cast<void>(provider.requestWaveform(testRequest(QStringLiteral("fast-track"))));

    QTRY_COMPARE(readySpy.count(), 1);
    const WaveformResult fastResult = resultAt(readySpy, 0);
    QCOMPARE(fastResult.heights, QVariantList({9, 8, 7, 6}));
    QCOMPARE(fastResult.barWidth, 4);

    Seriona::App::PlaybackController controller;
    controller.applyWaveform(fastResult.heights, fastResult.barWidth);
    QCOMPARE(controller.waveformHeights(), QVariantList({9, 8, 7, 6}));
    QCOMPARE(controller.waveformBarWidth(), 4);

    releaseSlow.release();
    // 负向窗：旧结果若泄漏应在该窗口内到达（慢机上放宽到 500ms）
    QTest::qWait(500);
    QCOMPARE(readySpy.count(), 1);
    QCOMPARE(controller.waveformHeights(), QVariantList({9, 8, 7, 6}));
    QCOMPARE(controller.waveformBarWidth(), 4);
}

void WaveformWorkerTest::emptyTrackLyricsSnapshotClearsPreviousContent()
{
    Seriona::App::LyricsModel model;
    model.setShowTranslation(false);

    model.applyTrackLyricsSnapshot(trackLyricsSnapshot("current-track", {
        snapshotLine(std::chrono::milliseconds{0}, "Current lyric / 当前歌词", "Current lyric", "当前歌词")}));

    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(lyricsDisplayLine(model, 0), QStringLiteral("Current lyric"));

    // 切到另一首的带曲目空行回退快照 ⇒ 清空
    model.applyTrackLyricsSnapshot(trackLyricsSnapshot("previous-track", {}));
    QCOMPARE(model.rowCount(), 0);

    // 同一曲目再次有词后可渲染；随后该曲目的空行回退快照同样清空（不残留旧行）
    model.applyTrackLyricsSnapshot(trackLyricsSnapshot("current-track", {
        snapshotLine(std::chrono::milliseconds{0}, "Current lyric / 当前歌词", "Current lyric", "当前歌词")}));
    QCOMPARE(model.rowCount(), 1);
    model.applyTrackLyricsSnapshot(trackLyricsSnapshot("current-track", {}));
    QCOMPARE(model.rowCount(), 0);
}

void WaveformWorkerTest::missingFileFailureReturnsEmptyWaveform()
{
    WaveformProvider provider;
    provider.setGeneratorForTests([](const WaveformBuildInput &) -> WaveformPayload {
        throw std::runtime_error("fixture file is missing");
    });
    QSignalSpy failureSpy(&provider, &WaveformProvider::waveformFailed);

    static_cast<void>(provider.requestWaveform(testRequest(QStringLiteral("missing-file-track"))));

    QTRY_COMPARE(failureSpy.count(), 1);
    const WaveformResult result = resultAt(failureSpy, 0);
    QCOMPARE(result.heights, QVariantList{});
    QCOMPARE(result.barWidth, 0);
    QVERIFY(result.errorMessage.contains(QStringLiteral("fixture file is missing")));
}

QTEST_GUILESS_MAIN(WaveformWorkerTest)

#include "tst_waveform_worker.moc"
