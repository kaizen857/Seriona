// ============================================================
// T9 频响曲线合成 conformance 测试（真后端嵌入 harness，tst_backend_bridge 同款模式）
//
// 目的：本地乐观合成（src/app/equalizer_curve_synth.{h,cpp}，拖动期间前端显示源）
// 与后端 makeEqualizerStateSnapshot（control_state_reducer.cpp:306-363，镜像 curvePoints
// 的产生方）逐点一致——漂移锁：全 181 点逐点差 ≤ 0.05dB × 全部测试组。
// 若本地公式/ISO 表/preGain 项/0.95×fs/2 钳制/Band10 分母等与后端漂移，本测试红。
//
// Harness：真 makeMediaController（真 reducer）以 runInlineForTests 模式嵌入，
// 真 BackendBridge 命令链路：submitEqualizerConfig → SetEqualizerConfig → reducer
// 合成快照 → subscribeEqualizerState 镜像 → equalizerStateSnapshot() 读回。
// 音频/扫描/元数据面为最小 fake（本测试只驱动 EQ 命令，不驱动播放/扫描）。
// 零新公共 API：全部走既有 BackendBridge/submitEqualizerConfig + equalizerStateSnapshot。
// ============================================================
#include "backend_bridge.h"

#include "equalizer_curve_synth.h"

#include "seriona/audio/audio_contracts.h"
#include "seriona/control/control_contracts.h"
#include "seriona/control/folder_sort_settings_store.h"
#include "seriona/control/media_controller.h"
#include "seriona/metadata/metadata_contracts.h"
#include "seriona/scanner/scanner_contracts.h"

#include <QSignalSpy>
#include <QVariantList>
#include <QtTest/QTest>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

// —— 最小音频 fake（AudioPlaybackService 纯虚面；EQ 意图是默认 no-op——
// reducer 在命令期自行合成快照，不依赖音频服务回读）——
class FakeAudioPlaybackService final : public seriona::audio::AudioPlaybackService
{
public:
    void setEventSink(seriona::audio::BackendEventSink sink) override { m_sink = std::move(sink); }
    void configureOutput(const seriona::audio::AudioOutputConfig &) override { }
    void loadTrack(const seriona::audio::TrackPlaybackRequest &) override { }
    void prepareNext(const seriona::audio::TrackPlaybackRequest &) override { }
    void play() override { }
    void pause() override { }
    void resume() override { }
    void stop() override { }
    void seek(std::chrono::milliseconds) override { }
    void setVolume(float) override { }
    void setMuted(bool) override { }
    void selectOutputDevice(const std::string &) override { }
    seriona::audio::PlaybackClockSnapshot queryPlaybackClock() const override { return {}; }
    seriona::audio::BackendEventSink sink() const { return m_sink; }

private:
    seriona::audio::BackendEventSink m_sink;
};

// —— 最小扫描 fake ——
class FakeFileScannerService final : public seriona::scanner::FileScannerService
{
public:
    void setEventSink(seriona::scanner::ScannerEventSink) override { }
    void configure(const seriona::scanner::ScannerConfig &) override { }
    void scan(const std::vector<seriona::scanner::ScannerRoot> &, seriona::scanner::ScanMode) override { }
    void startWatching(const std::vector<seriona::scanner::ScannerRoot> &) override { }
    void stopWatching() override { }
    void stop() override { }
    seriona::scanner::PlaylistTreeSnapshot snapshot() const override { return {}; }
    bool removeLocation(const std::filesystem::path &) override { return false; }
    bool removeRoot(const std::filesystem::path &) override { return true; }
};

// —— 最小文件夹排序存储 fake ——
class FakeFolderSortSettingsStore final : public seriona::control::FolderSortSettingsStore
{
public:
    void upsert(seriona::control::FolderSortSetting) override { }
    std::optional<seriona::control::FolderSortSetting> load(const std::filesystem::path &,
                                                            const std::string &) const override
    {
        return std::nullopt;
    }
    void remove(const std::filesystem::path &, const std::string &) override { }
    std::vector<seriona::control::FolderSortSetting> list(const std::filesystem::path &) const override
    {
        return {};
    }
};

// —— 最小元数据 sharing fake（EQ 命令链不触碰；registerCommandCallback 返回空句柄）——
class FakeMetadataSharingService final : public seriona::metadata::MetadataSharingService
{
public:
    seriona::metadata::MetadataBackendKind backendKind() const override
    {
        return seriona::metadata::MetadataBackendKind::Noop;
    }
    seriona::metadata::MetadataBackendCapabilities capabilities() const override { return {}; }
    seriona::control::SubscriptionHandle registerCommandCallback(seriona::control::MediaControlCommandSink) override
    {
        return {};
    }
    seriona::metadata::MetadataSyncResult start(const seriona::metadata::PlatformMediaState &) override { return {}; }
    seriona::metadata::MetadataSyncResult update(const seriona::metadata::PlatformMediaState &) override { return {}; }
    seriona::metadata::MetadataSyncResult stop() override { return {}; }
};

struct ControllerHarness {
    std::shared_ptr<FakeAudioPlaybackService> audio = std::make_shared<FakeAudioPlaybackService>();
    std::shared_ptr<FakeFileScannerService> scanner = std::make_shared<FakeFileScannerService>();
    std::shared_ptr<FakeFolderSortSettingsStore> folderSortStore =
        std::make_shared<FakeFolderSortSettingsStore>();

    Seriona::App::BackendBridge::ControllerFactory factory()
    {
        struct FactoryState {
            seriona::control::MediaControllerDependencies dependencies;
            seriona::control::MediaControllerOptions options;
        };
        auto state = std::make_shared<FactoryState>();
        state->dependencies.audio = audio;
        state->dependencies.scanner = scanner;
        state->dependencies.metadata = std::make_unique<FakeMetadataSharingService>();
        state->dependencies.folderSortSettingsStore = folderSortStore;
        state->options.runInlineForTests = true; // 命令同步落 reducer（桥侧队列投递不变）
        return [state] {
            return seriona::control::makeMediaController(std::move(state->dependencies), state->options);
        };
    }
};

// —— 测试组定义：{名字, bandMode, preGainDb, 10/31 段增益} ——
struct SynthTestCase {
    const char *name;
    int bandMode;
    double preGainDb;
    std::array<double, 31> gains{}; // 前 10/31 项有效
    int gainCount;
};

// 10 段全 +15（极值）、10 段混合（±15 极值 + 0.1 网格 + 非零 preGain）、
// 31 段全 −15（极值）、31 段混合、10 段平坦 + 非零 preGain
const std::vector<SynthTestCase> &testCases()
{
    static const std::vector<SynthTestCase> cases = [] {
        std::vector<SynthTestCase> list;

        SynthTestCase tenMax{};
        tenMax.name = "10-max-plus15";
        tenMax.bandMode = 10;
        tenMax.preGainDb = 0.0;
        tenMax.gains.fill(15.0);
        tenMax.gainCount = 10;
        list.push_back(tenMax);

        SynthTestCase tenMix{};
        tenMix.name = "10-mixed-extremes-grid";
        tenMix.bandMode = 10;
        tenMix.preGainDb = 2.5; // 非零 preGain：漏项必超 0.05dB 阈值
        tenMix.gains = { -15.0, -6.5, 0.0, 3.5, 12.0, -12.0, 6.0, 0.5, -0.5, 15.0, 0.0, 0.0,
                         0.0,    0.0,  0.0, 0.0,  0.0,   0.0,   0.0,  0.0, 0.0,  0.0, 0.0,
                         0.0,    0.0,  0.0, 0.0,  0.0,   0.0,   0.0,  0.0 };
        tenMix.gainCount = 10;
        list.push_back(tenMix);

        SynthTestCase thirtyMin{};
        thirtyMin.name = "31-min-minus15";
        thirtyMin.bandMode = 31;
        thirtyMin.preGainDb = 0.0;
        thirtyMin.gains.fill(-15.0);
        thirtyMin.gainCount = 31;
        list.push_back(thirtyMin);

        SynthTestCase thirtyMix{};
        thirtyMix.name = "31-mixed-extremes-grid";
        thirtyMix.bandMode = 31;
        thirtyMix.preGainDb = -3.0;
        thirtyMix.gains = { 15.0, -15.0, 12.5, -10.2, 8.0, -7.4, 5.0, -3.1, 0.0, 2.0,
                            -2.0, 4.4,   -4.4, 6.0,   -6.0, 1.5, -1.5, 15.0, -15.0, 0.0,
                            3.3,  0.7,   -0.7, 11.0,  -11.0, 9.9, -9.9, 5.5, -5.5, 10.0,
                            -10.0 };
        thirtyMix.gainCount = 31;
        list.push_back(thirtyMix);

        SynthTestCase tenFlatPre{};
        tenFlatPre.name = "10-flat-preGain6";
        tenFlatPre.bandMode = 10;
        tenFlatPre.preGainDb = 6.0;
        tenFlatPre.gains.fill(0.0);
        tenFlatPre.gainCount = 10;
        list.push_back(tenFlatPre);
        return list;
    }();
    return cases;
}

} // namespace

class EqualizerCurveConformanceTest : public QObject
{
    Q_OBJECT

private slots:
    void synthMatchesBackendReducerOverAllCases();
    void envelopeStaysWithinHandlesAndPassesThroughKnots();
};

void EqualizerCurveConformanceTest::synthMatchesBackendReducerOverAllCases()
{
    double worstOverall = 0.0;
    bool allPassed = true;

    for (const SynthTestCase &test : testCases()) {
        ControllerHarness harness;
        Seriona::App::BackendBridge bridge(harness.factory());
        QSignalSpy eqSpy(&bridge, &Seriona::App::BackendBridge::equalizerStateChanged);
        bridge.start();
        QTRY_VERIFY_WITH_TIMEOUT(eqSpy.count() > 0, 15000); // 订阅初始投递（gen0 空快照）

        // 组包（enabled/bandMode/preGainDb/gains/limiter/spectrum）→ 真 reducer
        QVariantList gains;
        for (int i = 0; i < test.gainCount; ++i)
            gains.append(test.gains[static_cast<std::size_t>(i)]);
        const seriona::control::MediaControllerCommandResult result = bridge.submitEqualizerConfig(
            /*enabled=*/true, test.bandMode, test.preGainDb, gains, /*limiterEnabled=*/false,
            /*spectrumEnabled=*/false);
        if (!result.accepted) {
            std::printf("[conformance] %-24s SUBMIT REJECTED: %s\n", test.name,
                        result.message.c_str());
            allPassed = false;
            continue;
        }

        // 镜像 curvePoints（QueuedConnection 搬运 → 轮询等镜像）
        QTRY_VERIFY_WITH_TIMEOUT(eqSpy.count() >= 2, 15000);
        const seriona::audio::EqualizerStateSnapshot &snapshot = bridge.equalizerStateSnapshot();

        // 本地合成（同一组输入；fs=0 = 后端 reducer 恒走分支）
        std::array<double, 31> localGains{};
        for (int i = 0; i < test.gainCount; ++i)
            localGains[static_cast<std::size_t>(i)] = gains.at(i).toDouble();
        const Seriona::App::EqualizerCurveSynthResult local = Seriona::App::synthesizeEqualizerCurve(
            test.bandMode, test.preGainDb,
            std::span<const double>(localGains.data(), static_cast<std::size_t>(test.gainCount)),
            0U);

        // 逐点差 ≤ 0.05dB × 全部 181 点（AC 漂移锁）
        double worstDev = 0.0;
        double sumDev = 0.0;
        int worstPoint = -1;
        constexpr int kPoints = 181;
        for (int i = 0; i < kPoints; ++i) {
            const double backendDb = static_cast<double>(snapshot.curvePointsDb[static_cast<std::size_t>(i)]);
            const double dev = std::fabs(local.responseDb[static_cast<std::size_t>(i)] - backendDb);
            if (dev > worstDev) {
                worstDev = dev;
                worstPoint = i;
            }
            sumDev += dev;
        }
        // 轴一致性（同源公式锁；本断言非 AC 门禁，仅记录）
        double worstAxisDev = 0.0;
        for (int i = 0; i < kPoints; ++i) {
            const double backendHz =
                static_cast<double>(snapshot.curveFrequenciesHz[static_cast<std::size_t>(i)]);
            worstAxisDev = std::max(worstAxisDev,
                                    std::fabs(local.frequenciesHz[static_cast<std::size_t>(i)] - backendHz));
        }
        const bool passed = worstDev <= 0.05;
        allPassed = allPassed && passed;
        worstOverall = std::max(worstOverall, worstDev);
        // 全 0 偏差时无 worst 点（worstPoint 保持 -1），跳过频率查询避免 size_t(-1) 越界
        const double worstHz =
            worstPoint >= 0 ? local.frequenciesHz[static_cast<std::size_t>(worstPoint)] : 0.0;
        std::printf("[conformance] %-24s mode=%2d preGain=%+4.1f  maxDev=%.6fdB"
                    " (pt %3d, %7.2fHz)  meanDev=%.6fdB  axisMaxDev=%.4eHz  %s\n",
                    test.name, test.bandMode, test.preGainDb, worstDev, worstPoint, worstHz,
                    sumDev / kPoints, worstAxisDev, passed ? "PASS" : "FAIL");
    }
    std::printf("[conformance] overall: %s (worst deviation %.6f dB)\n",
                allPassed ? "ALL PASS" : "FAILED", worstOverall);
    QVERIFY2(allPassed, "本地乐观合成与后端 reducer 逐点差 > 0.05dB（公式/常量漂移）");
}

// R5 图形包络断言：synthesizeGraphicEnvelopeCurve（PCHIP 过手柄点包络）——
// 锁三个几何性质（用户可感知的「手柄贴曲线/曲线不过冲」契约）：
//   1. 不过冲：181 点响应全部落在手柄增益值域 [min, max]（PCHIP 段内单调保证，
//      若回归为 Catmull-Rom 过冲样条/物理叠加曲线，此断言红）；
//   2. 过手柄：在手柄中心频点处对相邻采样折线插值，值 ≈ 档增益（容差 0.5dB——
//      31 段 ±15 交替最坏情形折线拱高 ≤0.35dB，0.5dB 阈值具区分度）；
//   3. 轴单调：频率轴严格升序（绘制 x 依赖单调轴，折线无回绕）。
// 输入 = 后端同组测试用例（bandMode/gains；preGain 不进包络，本函数不使用）。
void EqualizerCurveConformanceTest::envelopeStaysWithinHandlesAndPassesThroughKnots()
{
    bool allPassed = true;
    double worstRangeViolation = 0.0;
    double worstKnotDeviation = 0.0;
    const auto xOf = [](double hz) { return std::log10(hz / 20.0) / 3.0; };
    for (const SynthTestCase &test : testCases()) {
        double caseRangeViolation = 0.0;
        double caseKnotDeviation = 0.0;
        std::array<double, 31> gains{};
        for (int i = 0; i < test.gainCount; ++i)
            gains[static_cast<std::size_t>(i)] = test.gains[static_cast<std::size_t>(i)];
        const Seriona::App::EqualizerCurveSynthResult env = Seriona::App::synthesizeGraphicEnvelopeCurve(
            test.bandMode, std::span<const double>(gains.data(), static_cast<std::size_t>(test.gainCount)));

        const double minGain = *std::min_element(gains.begin(), gains.begin() + test.gainCount);
        const double maxGain = *std::max_element(gains.begin(), gains.begin() + test.gainCount);
        for (int i = 0; i < 181; ++i) {
            const double db = env.responseDb[static_cast<std::size_t>(i)];
            const double rangeViolation = std::max(minGain - db, db - maxGain);
            caseRangeViolation = std::max(caseRangeViolation, rangeViolation);
            if (rangeViolation > 1e-9)
                allPassed = false;
        }
        double lastX = -1.0;
        for (int i = 0; i < 181; ++i) {
            const double x = xOf(env.frequenciesHz[static_cast<std::size_t>(i)]);
            if (x <= lastX)
                allPassed = false;
            lastX = x;
        }
        for (int b = 0; b < test.gainCount; ++b) {
            const double knotX = xOf(Seriona::App::eqIsoBandCenterHz(test.bandMode, b));
            const double expected = test.gains[static_cast<std::size_t>(b)];
            // 折线插值：找 knotX 两侧采样（频率轴单调升序；首尾手柄落在轴端点内）
            std::size_t hi = 1;
            while (hi + 1 < env.frequenciesHz.size()
                   && xOf(env.frequenciesHz[hi]) < knotX)
                ++hi;
            const double x0 = xOf(env.frequenciesHz[hi - 1]);
            const double x1 = xOf(env.frequenciesHz[hi]);
            const double t = x1 > x0 ? (knotX - x0) / (x1 - x0) : 0.0;
            const double interpolated = env.responseDb[hi - 1]
                + t * (env.responseDb[hi] - env.responseDb[hi - 1]);
            const double dev = std::fabs(interpolated - expected);
            caseKnotDeviation = std::max(caseKnotDeviation, dev);
            if (dev > 0.5)
                allPassed = false;
        }
        worstRangeViolation = std::max(worstRangeViolation, caseRangeViolation);
        worstKnotDeviation = std::max(worstKnotDeviation, caseKnotDeviation);
        std::printf("[envelope] %-24s mode=%2d rangeViolation=%.6fdB"
                    " worstKnotDev=%.4fdB  %s\n",
                    test.name, test.bandMode, caseRangeViolation, caseKnotDeviation,
                    allPassed ? "PASS" : "FAIL");
    }
    std::printf("[envelope] overall: %s (rangeViolation %.6f dB / knotDev %.4f dB)\n",
                allPassed ? "ALL PASS" : "FAILED", worstRangeViolation, worstKnotDeviation);
    QVERIFY2(allPassed,
             "包络越出手柄值域（不过冲失败）或手柄处折线偏差 > 0.5dB（过手柄失败）");
}

QTEST_GUILESS_MAIN(EqualizerCurveConformanceTest)
#include "tst_equalizer_curve_conformance.moc"
