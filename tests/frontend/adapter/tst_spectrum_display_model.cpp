// 任务 7 TDD：频谱显示平滑模型单测（先于实现编写——RED 桩阶段即就位）。
// 纯 C++ 模型（无 QtQuick/QSG），QTEST_GUILESS_MAIN 即证明无 GUI 依赖。
// 数值断言全部按规格实测：τ 以 e^−1 比例锁定、墙钟 dt 大步长安全、钳制/地板逐档验证。
#include "spectrum_display_model.h"

#include <cmath>
#include <limits>
#include <vector>

#include <QtTest/QTest>

namespace {

constexpr double kTol = 1e-9;

bool near(double a, double b, double tol = kTol)
{
    return std::abs(a - b) <= tol;
}

bool allNear(const std::vector<double> &values, double expected, double tol = kTol)
{
    for (double v : values) {
        if (!near(v, expected, tol))
            return false;
    }
    return true;
}

bool allFinite(const std::vector<double> &values)
{
    for (double v : values) {
        if (!std::isfinite(v))
            return false;
    }
    return true;
}

} // namespace

class SpectrumDisplayModelTest : public QObject
{
    Q_OBJECT

private slots:
    void tauConstantsWithinSpecBands();
    void attackIsInstantOnRise();
    void barDecayReachesEInvAtOneTau();
    void decayPathIndependentUnderDtSplitting();
    void constantInputConvergesToTargetAndHolds();
    void largeDtSnapsWithoutOvershootOrNaN();
    void nonPositiveDtIsNoop();
    void peakMaxHoldInstantAndIndependentTau();
    void peakFallMeasuredEInvAtTauPeak();
    void aboveZeroDbClampsTop();
    void belowMinusSixtyMapsToZero();
    void silenceFloorRendersAllZeroAndFinite();
    void resetClearsAllChannels();
    void freqLogAxisMappingAnchors();
    void bandCenterConvention();
    void dbFractionMappingDisplayAndGainDomains();
};

// τ 常量的设计规格带（调研 §6.2）：柱 120-150ms、峰值 800ms-1s。
// 防「凑数 e^−1」：实测以本常量推进，若实现暗用别的 τ 则 e^−1 断言必红。
void SpectrumDisplayModelTest::tauConstantsWithinSpecBands()
{
    QVERIFY2(Seriona::App::SpectrumDisplayModel::kBarFallTauSeconds >= 0.120
                 && Seriona::App::SpectrumDisplayModel::kBarFallTauSeconds <= 0.150,
             "bar τ must sit in the 120-150ms spec band");
    QVERIFY2(Seriona::App::SpectrumDisplayModel::kPeakFallTauSeconds >= 0.800
                 && Seriona::App::SpectrumDisplayModel::kPeakFallTauSeconds <= 1.000,
             "peak τ must sit in the 800ms-1s spec band");
    QCOMPARE(Seriona::App::SpectrumDisplayModel::kBinCount, 120);
}

void SpectrumDisplayModelTest::attackIsInstantOnRise()
{
    Seriona::App::SpectrumDisplayModel model;
    std::vector<double> bins(Seriona::App::SpectrumDisplayModel::kBinCount, -20.0);

    // 目标升 → 无需 advanceBy 即到位（attack 即时，新数据 ≤1 帧可见）
    model.setBinsDb(bins);
    const double expected = Seriona::App::SpectrumDisplayModel::spectrumDbToFraction(-20.0);
    for (int i = 0; i < model.binCount(); ++i) {
        QVERIFY2(near(model.displayValue(i), expected, 1e-12), "display must jump to target immediately on rise");
        QVERIFY2(near(model.peakValue(i), expected, 1e-12), "peak must follow the initial attack too");
    }
    // 更高目标同样即时（0dBFS 顶）
    std::fill(bins.begin(), bins.end(), -1.0);
    model.setBinsDb(bins);
    const double expectedTop = Seriona::App::SpectrumDisplayModel::spectrumDbToFraction(-1.0);
    QVERIFY(near(model.displayValue(0), expectedTop, 1e-12));
}

void SpectrumDisplayModelTest::barDecayReachesEInvAtOneTau()
{
    using Model = Seriona::App::SpectrumDisplayModel;
    Model model;
    std::vector<double> bins(Model::kBinCount, -20.0); // target = (−20+60)/60 = 2/3
    model.setBinsDb(bins);
    QVERIFY(near(model.displayValue(0), 2.0 / 3.0, 1e-12));

    // 恒定目标降到 1/3：t=τ 处 (display−target)/(initial−target) 必须 ≈ e^−1
    std::fill(bins.begin(), bins.end(), -40.0); // target = 1/3
    model.setBinsDb(bins);
    const double target = 1.0 / 3.0;
    const double initial = 2.0 / 3.0;
    model.advanceBy(Model::kBarFallTauSeconds);

    const double ratio = (model.displayValue(0) - target) / (initial - target);
    QVERIFY2(std::abs(ratio - std::exp(-1.0)) < 1e-9,
             qPrintable(QStringLiteral("bar decay after exactly τ must measure e^-1, got ratio %1").arg(ratio, 0, 'g', 12)));
}

void SpectrumDisplayModelTest::decayPathIndependentUnderDtSplitting()
{
    using Model = Seriona::App::SpectrumDisplayModel;
    // 指数衰减与拆步无关（exp 乘积律）：一次 τ 与 20 步 τ/20 结果一致
    // —— 锁定「墙钟 dt」语义：dt 是累计时长而非步数/帧数。
    auto runSplit = [](int steps) {
        Model model;
        std::vector<double> bins(Model::kBinCount, -20.0);
        model.setBinsDb(bins);
        std::fill(bins.begin(), bins.end(), -40.0);
        model.setBinsDb(bins);
        const double dt = Model::kBarFallTauSeconds / static_cast<double>(steps);
        for (int s = 0; s < steps; ++s)
            model.advanceBy(dt);
        return model.displayValue(0);
    };

    const double singleStep = runSplit(1);
    const double splitSteps = runSplit(20);
    QVERIFY2(near(singleStep, splitSteps, 1e-12),
             qPrintable(QStringLiteral("one τ-step and 20 τ/20-steps must agree, got %1 vs %2")
                            .arg(singleStep, 0, 'g', 12)
                            .arg(splitSteps, 0, 'g', 12)));
    // 且拆步路径同样满足 e^−1
    const double ratio = (splitSteps - 1.0 / 3.0) / (2.0 / 3.0 - 1.0 / 3.0);
    QVERIFY(std::abs(ratio - std::exp(-1.0)) < 1e-9);
}

void SpectrumDisplayModelTest::constantInputConvergesToTargetAndHolds()
{
    using Model = Seriona::App::SpectrumDisplayModel;
    Model model;
    std::vector<double> bins(Model::kBinCount, -30.0); // 0.5（即时攻击到位）
    model.setBinsDb(bins);
    std::fill(bins.begin(), bins.end(), -40.0); // 恒定目标 1/3，低于当前显示 → 走回落
    model.setBinsDb(bins);

    // 60 个 τ_peak 大步（共 54s 墙钟）后两通道均收敛到当前目标
    for (int i = 0; i < 60; ++i)
        model.advanceBy(Model::kPeakFallTauSeconds);
    QVERIFY(allNear(model.displays(), 1.0 / 3.0, 1e-12));
    QVERIFY(allNear(model.peaks(), 1.0 / 3.0, 1e-12));

    // 恒定输入稳态 == 目标：继续推进不漂移、不越过目标
    const double before = model.displayValue(0);
    model.advanceBy(Model::kBarFallTauSeconds * 100.0);
    QVERIFY(near(model.displayValue(0), before, 1e-15));
    QVERIFY(near(model.displayValue(0), 1.0 / 3.0, 1e-12));
    QVERIFY(model.displayValue(0) <= 1.0 / 3.0 + 1e-15);
}

void SpectrumDisplayModelTest::largeDtSnapsWithoutOvershootOrNaN()
{
    using Model = Seriona::App::SpectrumDisplayModel;
    Model model;
    std::vector<double> bins(Model::kBinCount, -10.0); // 5/6
    model.setBinsDb(bins);
    std::fill(bins.begin(), bins.end(), -50.0); // 1/6
    model.setBinsDb(bins);

    // 单调回落路径：每一步都在 [target, 上一步] 内（无过冲/下冲）
    double previous = model.displayValue(0);
    for (int i = 0; i < 10; ++i) {
        model.advanceBy(0.01);
        const double v = model.displayValue(0);
        QVERIFY(v <= previous + 1e-12);
        QVERIFY(v >= 1.0 / 6.0 - 1e-12);
        previous = v;
    }
    // 大步长（分钟级/整日挂起恢复）：直落目标，无爆值/NaN
    model.advanceBy(86400.0);
    QVERIFY(near(model.displayValue(0), 1.0 / 6.0, 1e-12));
    QVERIFY(near(model.peakValue(0), 1.0 / 6.0, 1e-12));
    QVERIFY(allFinite(model.displays()));
    QVERIFY(allFinite(model.peaks()));

    // 极端大步长依然安全
    model.advanceBy(1e300);
    QVERIFY(allFinite(model.displays()));
    QVERIFY(allFinite(model.peaks()));
}

void SpectrumDisplayModelTest::nonPositiveDtIsNoop()
{
    using Model = Seriona::App::SpectrumDisplayModel;
    Model model;
    std::vector<double> bins(Model::kBinCount, -20.0);
    model.setBinsDb(bins);
    std::fill(bins.begin(), bins.end(), -40.0);
    model.setBinsDb(bins);

    const double before = model.displayValue(0);
    model.advanceBy(0.0); // dt=0：无时间流逝，状态不变
    QVERIFY(near(model.displayValue(0), before, 1e-15));
    model.advanceBy(-1.0); // 负 dt（时钟回拨防护）：无操作
    QVERIFY(near(model.displayValue(0), before, 1e-15));
    model.advanceBy(std::numeric_limits<double>::quiet_NaN()); // 非有限 dt：无操作
    QVERIFY(near(model.displayValue(0), before, 1e-15));
}

void SpectrumDisplayModelTest::peakMaxHoldInstantAndIndependentTau()
{
    using Model = Seriona::App::SpectrumDisplayModel;
    Model model;
    std::vector<double> bins(Model::kBinCount, -20.0); // 2/3
    model.setBinsDb(bins);

    // 目标恒定时峰值不下落（max 保持稳态）
    for (int i = 0; i < 30; ++i)
        model.advanceBy(Model::kBarFallTauSeconds);
    QVERIFY(near(model.peakValue(0), 2.0 / 3.0, 1e-12));

    // 目标回落：峰值仍保持原 max（setBinsDb 不触发回落）
    std::fill(bins.begin(), bins.end(), -40.0);
    model.setBinsDb(bins);
    QVERIFY(near(model.peakValue(0), 2.0 / 3.0, 1e-12));

    // 目标再升越过旧 max → max 即时更新（无需 advance）
    std::fill(bins.begin(), bins.end(), -5.0); // 55/60 = 0.9167
    model.setBinsDb(bins);
    QVERIFY(near(model.peakValue(0), 55.0 / 60.0, 1e-12));

    // 峰值通道回落比柱通道慢得多：同一 0.9s 墙钟内柱已基本沉底、峰值仍在半途（τ 独立性）
    std::fill(bins.begin(), bins.end(), -50.0); // target 1/6 ≈ 0.1667
    model.setBinsDb(bins);
    model.advanceBy(Model::kPeakFallTauSeconds); // 0.9 s = τ_peak = 6.67τ_bar
    const double settled = model.displayValue(0);
    const double midFallPeak = model.peakValue(0);
    QVERIFY2(settled - 1.0 / 6.0 < 1e-3 && settled >= 1.0 / 6.0 - 1e-12,
             "bar must have nearly settled onto the low target within one τ_peak");
    // 峰值以 τ_peak 回落：0.75 跨度剩 e^-1 → ≈ 0.4425（若误用柱 τ 则 ≈ 1/6，必红）
    QVERIFY2(midFallPeak > 0.43 && midFallPeak < 0.46,
             qPrintable(QStringLiteral("peak must decay with its own slower τ, got %1").arg(midFallPeak, 0, 'g', 12)));
    QVERIFY2(midFallPeak - settled > 0.25, "peak must still sit far above the settled bar");
}

void SpectrumDisplayModelTest::peakFallMeasuredEInvAtTauPeak()
{
    using Model = Seriona::App::SpectrumDisplayModel;
    Model model;
    std::vector<double> bins(Model::kBinCount, -20.0); // 2/3
    model.setBinsDb(bins);
    QVERIFY(near(model.peakValue(0), 2.0 / 3.0, 1e-12));

    // 恒定目标低于峰值：t=τ_peak 处 (peak−target)/(initial−target) ≈ e^−1
    std::fill(bins.begin(), bins.end(), -40.0); // 1/3
    model.setBinsDb(bins);
    model.advanceBy(Model::kPeakFallTauSeconds);

    const double ratio = (model.peakValue(0) - 1.0 / 3.0) / (2.0 / 3.0 - 1.0 / 3.0);
    QVERIFY2(std::abs(ratio - std::exp(-1.0)) < 1e-9,
             qPrintable(QStringLiteral("peak fall after exactly τ_peak must measure e^-1, got ratio %1")
                            .arg(ratio, 0, 'g', 12)));
}

void SpectrumDisplayModelTest::aboveZeroDbClampsTop()
{
    using Model = Seriona::App::SpectrumDisplayModel;
    Model model;
    std::vector<double> bins(Model::kBinCount, 0.0); // 0dBFS = 顶 1.0
    model.setBinsDb(bins);
    QVERIFY(near(model.displayValue(0), 1.0, 1e-12));
    std::fill(bins.begin(), bins.end(), 6.0); // >0dBFS 一律钳 1
    model.setBinsDb(bins);
    QVERIFY(near(model.displayValue(0), 1.0, 1e-12));
    QVERIFY(near(model.peakValue(0), 1.0, 1e-12));
    QVERIFY(allNear(model.displays(), 1.0, 1e-12));
    // 顶值回落仍不越界
    std::fill(bins.begin(), bins.end(), -30.0);
    model.setBinsDb(bins);
    model.advanceBy(1.0);
    QVERIFY(model.displayValue(0) <= 1.0);
    QVERIFY(model.displayValue(0) >= 0.5 - 1e-9);
}

void SpectrumDisplayModelTest::belowMinusSixtyMapsToZero()
{
    using Model = Seriona::App::SpectrumDisplayModel;
    // 映射层：显示域底 -60 及以下全部 → 0（含 -120 静音地板）
    QVERIFY(near(Model::spectrumDbToFraction(-60.0), 0.0, 1e-15));
    QVERIFY(near(Model::spectrumDbToFraction(-70.0), 0.0, 1e-15));
    QVERIFY(near(Model::spectrumDbToFraction(-120.0), 0.0, 1e-15));

    // 状态层：低于 -60 的目标从高处衰减到 0 显示
    Model model;
    std::vector<double> bins(Model::kBinCount, -20.0);
    model.setBinsDb(bins);
    QVERIFY(near(model.displayValue(0), 2.0 / 3.0, 1e-12));
    std::fill(bins.begin(), bins.end(), -90.0);
    model.setBinsDb(bins);
    model.advanceBy(Model::kBarFallTauSeconds * 10.0); // e^-10
    QVERIFY2(model.displayValue(0) < 1e-4 && model.displayValue(0) >= 0.0,
             "decaying toward a below-floor target must land at ~0 without going negative");
}

void SpectrumDisplayModelTest::silenceFloorRendersAllZeroAndFinite()
{
    using Model = Seriona::App::SpectrumDisplayModel;
    // 全新模型（无数据）= 全 0、全有限（空数据不 NaN 的模型侧保证）
    Model model;
    QVERIFY(model.displays().size() == static_cast<size_t>(Model::kBinCount));
    QVERIFY(model.peaks().size() == static_cast<size_t>(Model::kBinCount));
    QVERIFY(allNear(model.displays(), 0.0, 0.0));
    QVERIFY(allNear(model.peaks(), 0.0, 0.0));
    QVERIFY(allFinite(model.displays()));
    QVERIFY(allFinite(model.peaks()));

    // 全地板输入（-120 静音）：维持全 0
    std::vector<double> bins(Model::kBinCount, -120.0);
    model.setBinsDb(bins);
    model.advanceBy(1.0);
    QVERIFY(allNear(model.displays(), 0.0, 0.0));
    QVERIFY(allFinite(model.displays()));

    // 有声后骤静：回落终点仍是全 0
    std::fill(bins.begin(), bins.end(), -15.0);
    model.setBinsDb(bins);
    QVERIFY(allNear(model.displays(), 0.75, 1e-12));
    std::fill(bins.begin(), bins.end(), -120.0);
    model.setBinsDb(bins);
    model.advanceBy(3.0); // ≫ 多倍 τ
    QVERIFY(allNear(model.displays(), 0.0, 1e-9));
    QVERIFY(allFinite(model.displays()));
}

void SpectrumDisplayModelTest::resetClearsAllChannels()
{
    using Model = Seriona::App::SpectrumDisplayModel;
    Model model;
    std::vector<double> bins(Model::kBinCount, -10.0);
    model.setBinsDb(bins);
    std::fill(bins.begin(), bins.end(), -30.0);
    model.setBinsDb(bins);
    model.advanceBy(0.05);
    QVERIFY(!allNear(model.displays(), 0.0, 1e-12)); // 先确认非零态

    model.reset();
    QVERIFY(allNear(model.displays(), 0.0, 0.0));
    QVERIFY(allNear(model.peaks(), 0.0, 0.0));
    // 复位后新目标照常即时攻击（状态可复用）
    std::fill(bins.begin(), bins.end(), -20.0);
    model.setBinsDb(bins);
    QVERIFY(near(model.displayValue(0), 2.0 / 3.0, 1e-12));
}

void SpectrumDisplayModelTest::freqLogAxisMappingAnchors()
{
    using Model = Seriona::App::SpectrumDisplayModel;
    // 对数轴锚点：20→0、20k→1；每十倍频程等距 → 200Hz = 1/3、2000Hz = 2/3
    QVERIFY(near(Model::freqToFraction(20.0), 0.0, 1e-12));
    QVERIFY(near(Model::freqToFraction(20000.0), 1.0, 1e-12));
    QVERIFY(near(Model::freqToFraction(200.0), 1.0 / 3.0, 1e-12));
    QVERIFY(near(Model::freqToFraction(2000.0), 2.0 / 3.0, 1e-12));
    // 几何中点（√(20·20000) = 632.46Hz）→ 0.5
    QVERIFY(near(Model::freqToFraction(std::sqrt(20.0 * 20000.0)), 0.5, 1e-12));
    // 单调
    QVERIFY(Model::freqToFraction(100.0) < Model::freqToFraction(1000.0));
    QVERIFY(Model::freqToFraction(1000.0) < Model::freqToFraction(10000.0));
    // 域外钳制
    QVERIFY(near(Model::freqToFraction(5.0), 0.0, 1e-15));
    QVERIFY(near(Model::freqToFraction(1e9), 1.0, 1e-15));

    // 逆映射往返
    QVERIFY(near(Model::frequencyAtFraction(1.0 / 3.0), 200.0, 1e-9));
    QVERIFY(near(Model::frequencyAtFraction(0.0), 20.0, 1e-12));
    QVERIFY(near(Model::frequencyAtFraction(1.0), 20000.0, 1e-12));
    QVERIFY(near(Model::freqToFraction(Model::frequencyAtFraction(0.4)), 0.4, 1e-12));
    QVERIFY(near(Model::frequencyAtFraction(-0.5), 20.0, 1e-12));
    QVERIFY(near(Model::frequencyAtFraction(2.0), 20000.0, 1e-12));
}

void SpectrumDisplayModelTest::bandCenterConvention()
{
    using Model = Seriona::App::SpectrumDisplayModel;
    // 桶中心约定：(i+0.5)/binCount（前端 x 用桶中心非桶边）
    for (int i : {0, 1, 3, 59, 60, 119}) {
        const double expectedFraction = (static_cast<double>(i) + 0.5) / static_cast<double>(Model::kBinCount);
        QVERIFY2(near(Model::bandCenterFraction(i), expectedFraction, 1e-15),
                 "bandCenterFraction must be (i+0.5)/binCount");
    }
    // 中心频率严格递增且落在轴内
    double previous = 0.0;
    for (int i = 0; i < Model::kBinCount; ++i) {
        const double hz = Model::bandCenterHz(i);
        QVERIFY(hz > 20.0);
        QVERIFY(hz < 20000.0);
        QVERIFY(hz > previous);
        previous = hz;
    }
    // 频率位置往返：freqToFraction(centerHz(i)) ≈ (i+0.5)/120
    for (int i : {0, 1, 17, 59, 100, 119}) {
        QVERIFY2(near(Model::freqToFraction(Model::bandCenterHz(i)), Model::bandCenterFraction(i), 1e-9),
                 qPrintable(QStringLiteral("center frequency round-trip failed at bin %1").arg(i)));
    }
    // 桶中心落在桶内（i/120, (i+1)/120）而非桶边
    for (int i : {0, 5, 42, 77, 119}) {
        const double c = Model::bandCenterFraction(i);
        QVERIFY(c > static_cast<double>(i) / Model::kBinCount);
        QVERIFY(c < static_cast<double>(i + 1) / Model::kBinCount);
    }
}

void SpectrumDisplayModelTest::dbFractionMappingDisplayAndGainDomains()
{
    using Model = Seriona::App::SpectrumDisplayModel;
    // 频谱显示域 -60..0dBFS 线性
    QVERIFY(near(Model::spectrumDbToFraction(-60.0), 0.0, 1e-15));
    QVERIFY(near(Model::spectrumDbToFraction(0.0), 1.0, 1e-15));
    QVERIFY(near(Model::spectrumDbToFraction(-30.0), 0.5, 1e-15));
    QVERIFY(near(Model::spectrumDbToFraction(-1.0), 59.0 / 60.0, 1e-12));
    QVERIFY(near(Model::spectrumDbToFraction(-45.0), 0.25, 1e-15));
    // 域外钳制
    QVERIFY(near(Model::spectrumDbToFraction(-120.0), 0.0, 1e-15));
    QVERIFY(near(Model::spectrumDbToFraction(6.0), 1.0, 1e-15));
    // EQ 增益域 ±15dB 全高（0dB 恒半高）
    QVERIFY(near(Model::gainDbToFraction(-15.0), 0.0, 1e-15));
    QVERIFY(near(Model::gainDbToFraction(15.0), 1.0, 1e-15));
    QVERIFY(near(Model::gainDbToFraction(0.0), 0.5, 1e-15));
    QVERIFY(near(Model::gainDbToFraction(7.5), 0.75, 1e-15));
    QVERIFY(near(Model::gainDbToFraction(-7.5), 0.25, 1e-15));
    QVERIFY(near(Model::gainDbToFraction(-30.0), 0.0, 1e-15));
    QVERIFY(near(Model::gainDbToFraction(30.0), 1.0, 1e-15));
}

QTEST_GUILESS_MAIN(SpectrumDisplayModelTest)

#include "tst_spectrum_display_model.moc"
