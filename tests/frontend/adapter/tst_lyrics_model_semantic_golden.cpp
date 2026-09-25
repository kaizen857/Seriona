#include "lyrics_model.h"

#include "seriona/control/control_contracts.h"

#include <QtTest/QTest>

#include <QDir>
#include <QFile>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringConverter>
#include <QStringList>
#include <QTextStream>

#include <chrono>

#ifndef SERIONA_LYRICS_GOLDEN_DIR
#error "SERIONA_LYRICS_GOLDEN_DIR 必须由构建系统定义（指向 Seriona/tests/fixtures/lyrics）"
#endif

namespace {

// ---------------------------------------------------------------------------
// 歌词渲染语义基线的输入集（todo 1 / W0 第一阶段，在此**钉死**，todo 31 未改动字节）。
// 每条都取自权威设计文档正文里已经印出来的行；只覆盖 ` / ` 强约定分支下可达的 4 类
// 路由（`//` 哨兵 / strong / strong-weak-fallback / validate-failed）。
//
// todo 31 后 LyricsModel 不再自己切分：本输入集的作用转为「冻结的对照物」——
//   ① 作为后端快照的 `text`（清洗行）喂给模型；
//   ② 与冻结的旧口径 golden（lyrics-model-semantic-golden.tsv）逐行做差异归因。
// ---------------------------------------------------------------------------
const QStringList &goldenInputs()
{
    static const QStringList inputs = {
        // §6.5 表的 8 个样本（文档 1004–1011 行）
        QStringLiteral("jAzLYEtN LYAsiance/.（みんなと理想を繋ぐ詩を謳います） / 我将咏唱一首连接大家与理想的诗"),
        QStringLiteral("haf en synk/.（ハンドシェイク受領、好意を返送） / 握手已接收，回赠善意"),
        QStringLiteral("haf.（受領） / 已接收"),
        QStringLiteral("土　肌を見せてなお / 大地啊，即便裸露着肌肤"),
        QStringLiteral("花/開かせる花/花 / 花／绽放的花／花"),
        QStringLiteral("Shake up Tonight(シェイカップトゥナイトゥ) / Shake up Tonight(シェイカップトゥナイトゥ)"),
        QStringLiteral("日文中文"),
        QStringLiteral("日文 中文"),
        // §3.6 的两个反例（文档 278–279 行）
        QStringLiteral("Rrha num wa ene revm /  / 我做了一个梦"),
        QStringLiteral("（Afterburner） / 揺るぎない Spirit / （Afterburner） / 坚定不移的Spirit"),
        // §6.3 的 P1/P2/P3（文档 933–935 行）
        QStringLiteral("[00:00.000][by:随遇不安_]"),
        QStringLiteral("一人"),
        QStringLiteral("「」"),
        // §6.2.4 的 SOL_FAGE/1x10 行（文档 671 行）
        QStringLiteral("Wee paks ra … SOL_FAGE/1x10 enter FRELIA. 「我将开始歌唱诗歌…」"),
        // §6.2.5 的「A」/「B」/「C」/「B」行（文档 687 行）
        QStringLiteral("「A」 / 「B」 / 「C」 / 「B」"),
        // `//` 哨兵：QQ 音乐导出的翻译 LRC 用 `//` 整行表示「此行无译文」
        // （文档 179 / 374 行）—— qq-sentinel 路由
        QStringLiteral("//"),
        // strong-weak-fallback 路由样本：强分隔符候选全在注音括号内（文档 861 行）
        QStringLiteral("蔷薇を想わせる绯色の『口红』(ローズレッドルージュ / )  令人联想起玫瑰的绯色口红"),
        // 第二阶段（todo 3 的 c-2）：吸收 Seriona_Backend/tests/fixtures/lyric_split/ 的夹具行。
        QStringLiteral("背伸びしてもいい\\回首也可以 成长也可以"),                       // route-backslash, doc 878
        QStringLiteral("(Kill you\\)"),                                                   // route-backslash, tool 50
        QStringLiteral("終わり"),                                                          // route-no-convention, doc 872
        QStringLiteral("私を呼んでいるのは誰？"),                                            // route-no-convention, doc 872
        QStringLiteral("惚れた腫れたの馬鹿騒ぎ/愛意被潑冷水的超蠢騷動"),                        // route-strong-bracket-fallback, tool 47
        QStringLiteral("おぼつかぬ足取り 「/略带动摇的步伐」"),                               // route-strong-bracket-fallback, doc 793
        QStringLiteral("言葉 / 这句话"),                                                   // route-strong-fallback, doc 202
        QStringLiteral("矛盾 / 这矛盾"),                                                   // route-strong-fallback, doc 203
        QStringLiteral("高架橋（こうかきょう） 雨（あめ）降（ふ）らす神様（かみさま / ）/高架橋 降雨之神"), // route-strong-fallback, doc 599
        QStringLiteral("あの『悲鸣は』(うたごえが / )『葡萄酒』   那悲鸣（歌声）有如葡萄美酒"),  // route-strong-weak-fallback, doc 862
        QStringLiteral("悔しいけど好きって純情 虽然不甘但还是喜欢你 这份纯情"),                // route-validate-failed / route-weak-bracket, doc 569
        QStringLiteral("繫がるの 真向いの熱が 今 「现在，正面的热情正紧密相连」"),            // route-weak-bracket, doc 606
        QStringLiteral("Wee paks ra … enter FRELIA. 「我将开始歌唱诗歌『METAFALICA』 经由SOL=FAGE 与芙蕾莉亚共享意识」"), // route-weak-bracket, doc 591
    };
    return inputs;
}

// 新口径（后端 splitDocument 在 `S: / ` 约定下的产物，同序；元数据行 #11 被 cleanLine
// 丢弃，以空串占位）。数据来源见 evidence：以 Seriona_Backend 的 seriona_lyric_split_dump
// 对同一份 inputs.txt 取 --dump-tsv，逐行记录 original/translation。
struct ExpectedPair {
    QString original;
    QString translation;
};

const QList<ExpectedPair> &expectedPairs()
{
    static const QList<ExpectedPair> expected = {
        {QStringLiteral("jAzLYEtN LYAsiance/.（みんなと理想を繋ぐ詩を謳います）"), QStringLiteral("我将咏唱一首连接大家与理想的诗")},  // 1
        {QStringLiteral("haf en synk/.（ハンドシェイク受領、好意を返送）"), QStringLiteral("握手已接收，回赠善意")},  // 2
        {QStringLiteral("haf.（受領）"), QStringLiteral("已接收")},  // 3
        {QStringLiteral("土　肌を見せてなお"), QStringLiteral("大地啊，即便裸露着肌肤")},  // 4
        {QStringLiteral("花/開かせる花/花"), QStringLiteral("花／绽放的花／花")},  // 5
        {QStringLiteral("Shake up Tonight(シェイカップトゥナイトゥ)"), QStringLiteral("Shake up Tonight(シェイカップトゥナイトゥ)")},  // 6
        {QStringLiteral("日文中文"), QStringLiteral("")},  // 7
        {QStringLiteral("日文 中文"), QStringLiteral("")},  // 8
        {QStringLiteral("Rrha num wa ene revm"), QStringLiteral("我做了一个梦")},  // 9
        {QStringLiteral("（Afterburner） / 揺るぎない Spirit / （Afterburner） / 坚定不移的Spirit"), QStringLiteral("")},  // 10
        {QString(), QString()},  // 11 (dropped)
        {QStringLiteral("一人"), QStringLiteral("")},  // 12
        {QStringLiteral("「」"), QStringLiteral("")},  // 13
        {QStringLiteral("Wee paks ra … SOL_FAGE/1x10 enter FRELIA. 「我将开始歌唱诗歌…」"), QStringLiteral("")},  // 14
        {QStringLiteral("「A」 / 「B」 / 「C」 / 「B」"), QStringLiteral("")},  // 15
        {QStringLiteral("//"), QStringLiteral("")},  // 16
        {QStringLiteral("蔷薇を想わせる绯色の『口红』(ローズレッドルージュ / )"), QStringLiteral("令人联想起玫瑰的绯色口红")},  // 17
        {QStringLiteral("背伸びしてもいい\\回首也可以 成长也可以"), QStringLiteral("")},  // 18
        {QStringLiteral("(Kill you\\)"), QStringLiteral("")},  // 19
        {QStringLiteral("終わり"), QStringLiteral("")},  // 20
        {QStringLiteral("私を呼んでいるのは誰？"), QStringLiteral("")},  // 21
        {QStringLiteral("惚れた腫れたの馬鹿騒ぎ/愛意被潑冷水的超蠢騷動"), QStringLiteral("")},  // 22
        {QStringLiteral("おぼつかぬ足取り 「/略带动摇的步伐」"), QStringLiteral("")},  // 23
        {QStringLiteral("言葉"), QStringLiteral("这句话")},  // 24
        {QStringLiteral("矛盾"), QStringLiteral("这矛盾")},  // 25
        {QStringLiteral("高架橋（こうかきょう） 雨（あめ）降（ふ）らす神様（かみさま / ）"), QStringLiteral("高架橋 降雨之神")},  // 26
        {QStringLiteral("あの『悲鸣は』(うたごえが / )『葡萄酒』"), QStringLiteral("那悲鸣（歌声）有如葡萄美酒")},  // 27
        {QStringLiteral("悔しいけど好きって純情 虽然不甘但还是喜欢你 这份纯情"), QStringLiteral("")},  // 28
        {QStringLiteral("繫がるの 真向いの熱が 今 「现在，正面的热情正紧密相连」"), QStringLiteral("")},  // 29
        {QStringLiteral("Wee paks ra … enter FRELIA. 「我将开始歌唱诗歌『METAFALICA』 经由SOL=FAGE 与芙蕾莉亚共享意识」"), QStringLiteral("")},  // 30
    };
    return expected;
}

// 元数据行（`[00:00.000][by:随遇不安_]`）由后端 cleanLine 丢弃，快照不含该行。
constexpr int kDroppedInputIndex = 10;

// 归因表：每条实测差异 → expected-diff-list.tsv 的既有类别 + 具名算法决策编号。
// 该文件由 todo 1(d) 在尚无新实现时冻结，本测试只读它做成员校验，不新增类别。
struct Attribution {
    // 输入集 1-based 行号（与差异表首列一致）
    int oneBasedRow;
    QString category;
    QString decisionId;
};

const QList<Attribution> &diffAttributions()
{
    static const QList<Attribution> table = {
        {9, QStringLiteral("空段不构成分段，分隔符含空段时折叠"), QStringLiteral("D37")},
        {10, QStringLiteral("一行内同一分隔符出现 ≥2 次 → 整行作原文"), QStringLiteral("D32")},
        {11, QStringLiteral("元数据行丢弃（先剥时间戳→再去行内 [tag:value]→最后滤制作人员）"), QStringLiteral("D33")},
        {15, QStringLiteral("一行内同一分隔符出现 ≥2 次 → 整行作原文"), QStringLiteral("D32")},
        {17, QStringLiteral("强分隔符候选全部落在括号内 ⇒ 本行无可用分隔符，续走弱边界/SCRIPT"), QStringLiteral("D38")},
        {26, QStringLiteral("切点永不落在配对括号内部"), QStringLiteral("D36")},
        {27, QStringLiteral("强分隔符候选全部落在括号内 ⇒ 本行无可用分隔符，续走弱边界/SCRIPT"), QStringLiteral("D38")},
    };
    return table;
}

QString fixtureDir()
{
    return QString::fromUtf8(SERIONA_LYRICS_GOLDEN_DIR);
}

QString inputsPath()
{
    return fixtureDir() + QStringLiteral("/lyrics-model-semantic-golden.inputs.txt");
}

QString goldenPath()
{
    return fixtureDir() + QStringLiteral("/lyrics-model-semantic-golden.tsv");
}

QString whitelistPath()
{
    return fixtureDir() + QStringLiteral("/expected-diff-list.tsv");
}

QStringList readLines(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    file.setTextModeEnabled(false);
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    QString content = stream.readAll();
    while (content.endsWith(QLatin1Char('\n')) || content.endsWith(QLatin1Char('\r'))) {
        content.chop(1);
    }
    if (content.isEmpty()) {
        return {};
    }
    return content.split(QLatin1Char('\n'));
}

QVariant modelValue(const Seriona::App::LyricsModel &model, int row, int role)
{
    return model.data(model.index(row, 0), role);
}

seriona::control::SplitLyricLine makeSnapshotLine(std::chrono::milliseconds timestamp,
                                                  const QString &text,
                                                  const QString &original,
                                                  const QString &translation)
{
    seriona::control::SplitLyricLine line;
    line.timestamp = timestamp;
    line.text = text.toStdString();
    line.original = original.toStdString();
    line.translation = translation.toStdString();
    line.split = !translation.isEmpty();
    return line;
}

}  // namespace

// 迁移后的语义基线（todo 31）：模型不再自己切分，本测试改为
//   ① 喂后端 TrackLyricsSnapshot → 断言渲染出的 (原文, 译文) 对与快照一致；
//   ② 逐行比对冻结的旧口径 golden，断言全部差异命中 expected-diff-list.tsv。
class LyricsModelSemanticGoldenTest : public QObject
{
    Q_OBJECT

private slots:
    void backendSnapshotRendersProvidedPairs();
    void diffAgainstFrozenGoldenHitsOnlyWhitelistCategories();
};

void LyricsModelSemanticGoldenTest::backendSnapshotRendersProvidedPairs()
{
    const QStringList inputs = goldenInputs();
    const QList<ExpectedPair> &expected = expectedPairs();
    QCOMPARE(inputs.size(), expected.size());
    QVERIFY(!inputs.isEmpty());

    seriona::control::TrackLyricsSnapshot snapshot;
    snapshot.trackId = "semantic-track";
    snapshot.targetLanguage = "zh";
    snapshot.lines.reserve(static_cast<std::size_t>(inputs.size()));

    QList<int> keptInputIndexes;
    for (qsizetype index = 0; index < inputs.size(); ++index) {
        if (index == kDroppedInputIndex) {
            continue;
        }
        keptInputIndexes.append(static_cast<int>(index));
        snapshot.lines.push_back(makeSnapshotLine(std::chrono::milliseconds{index * 1000},
                                                  inputs.at(index),
                                                  expected.at(index).original,
                                                  expected.at(index).translation));
    }

    Seriona::App::LyricsModel model;
    model.applyTrackLyricsSnapshot(snapshot);

    QCOMPARE(model.rowCount(), static_cast<int>(snapshot.lines.size()));

    // 前端不再切分：快照里含 ` / ` 但 translation 为空的行，渲染出的译文必须为空
    // （若前端仍按分隔符切分，这些行会渲染出非空译文而失败）。放在透传循环之前，使
    // 「透传被切分污染」的失败直接归因到这条语义断言。
    // model 行号不能直接用 input 行号：元数据行（input index 10）被后端丢弃后，其后的
    // input 行在 model 里整体前移 1；这里用 keptInputIndexes 反查真实 model 行号。
    for (const int inputIndex : {9, 14}) {
        const int modelRow = static_cast<int>(keptInputIndexes.indexOf(inputIndex));
        QVERIFY2(modelRow >= 0, qPrintable(QStringLiteral("input 第 %1 行不在快照里").arg(inputIndex + 1)));
        // 判别前提：该行确实含 ` / `，否则「译文为空」对「前端仍切分」没有判别力。
        QVERIFY2(inputs.at(inputIndex).contains(QStringLiteral(" / ")),
                 qPrintable(QStringLiteral("input 第 %1 行不含 ` / `，该断言不具判别力").arg(inputIndex + 1)));
        QCOMPARE(expected.at(inputIndex).translation, QString());
        QCOMPARE(modelValue(model, modelRow, Seriona::App::LyricsModel::TranslationRole).toString(), QString());
    }

    QString rendered;
    for (qsizetype row = 0; row < keptInputIndexes.size(); ++row) {
        const int inputIndex = keptInputIndexes.at(row);
        const ExpectedPair &pair = expected.at(inputIndex);
        QCOMPARE(modelValue(model, static_cast<int>(row), Seriona::App::LyricsModel::RawLineRole).toString(),
                 inputs.at(inputIndex));
        QCOMPARE(modelValue(model, static_cast<int>(row), Seriona::App::LyricsModel::DisplayLineRole).toString(),
                 pair.original);
        QCOMPARE(modelValue(model, static_cast<int>(row), Seriona::App::LyricsModel::TranslationRole).toString(),
                 pair.translation);

        rendered += QStringLiteral("%1\t%2\n").arg(pair.original, pair.translation);
    }

    qInfo().noquote() << "[task-31] 新口径渲染结果（original<TAB>translation，共"
                      << keptInputIndexes.size() << "行）:\n" + rendered;
}

void LyricsModelSemanticGoldenTest::diffAgainstFrozenGoldenHitsOnlyWhitelistCategories()
{
    const QStringList inputs = readLines(inputsPath());
    const QStringList golden = readLines(goldenPath());
    const QStringList whitelist = readLines(whitelistPath());
    const QList<ExpectedPair> &expected = expectedPairs();

    QCOMPARE(inputs.size(), 30);
    QCOMPARE(golden.size(), 30);
    QCOMPARE(expected.size(), inputs.size());
    // 冻结的 inputs.txt 与源码内钉死的输入集逐行一致（防止任一侧被静默改写）。
    QCOMPARE(inputs, goldenInputs());
    QVERIFY(whitelist.size() > 1);

    QSet<QString> whitelistKeys;
    for (qsizetype i = 1; i < whitelist.size(); ++i) {
        const QStringList columns = whitelist.at(i).split(QLatin1Char('\t'));
        QVERIFY2(columns.size() >= 2, qPrintable(QStringLiteral("白名单第 %1 行列数不足").arg(i + 1)));
        whitelistKeys.insert(columns.at(0) + QChar(0x1f) + columns.at(1));
    }

    int diffCount = 0;
    QString table = QStringLiteral("row\tdiff\toldOriginal\toldTranslation\tnewOriginal\tnewTranslation\tcategory\tdecisionId\n");
    for (qsizetype row = 0; row < inputs.size(); ++row) {
        const QStringList oldColumns = golden.at(row).split(QLatin1Char('\t'));
        QVERIFY2(oldColumns.size() >= 2, qPrintable(QStringLiteral("golden 第 %1 行列数不足").arg(row + 1)));
        const QString oldOriginal = oldColumns.at(0);
        const QString oldTranslation = oldColumns.at(1);

        const bool dropped = (row == kDroppedInputIndex);
        const QString newOriginal = dropped ? QStringLiteral("<dropped>") : expected.at(row).original;
        const QString newTranslation = dropped ? QStringLiteral("<dropped>") : expected.at(row).translation;
        const bool diff = dropped || oldOriginal != newOriginal || oldTranslation != newTranslation;

        QString category = QStringLiteral("-");
        QString decisionId = QStringLiteral("-");
        if (diff) {
            ++diffCount;
            const Attribution *match = nullptr;
            for (const Attribution &attribution : diffAttributions()) {
                if (attribution.oneBasedRow == row + 1) {
                    match = &attribution;
                    break;
                }
            }
            QVERIFY2(match != nullptr,
                     qPrintable(QStringLiteral("第 %1 行出现清单外差异（无归因条目）").arg(QString::number(row + 1))));
            QVERIFY2(whitelistKeys.contains(match->category + QChar(0x1f) + match->decisionId),
                     qPrintable(QStringLiteral("第 %1 行归因 (%2 / %3) 不在冻结白名单里")
                                    .arg(QString::number(row + 1), match->category, match->decisionId)));
            category = match->category;
            decisionId = match->decisionId;
        }

        table += QString::number(row + 1) + QLatin1Char('\t')
                 + (diff ? QStringLiteral("yes") : QStringLiteral("no")) + QLatin1Char('\t')
                 + oldOriginal + QLatin1Char('\t') + oldTranslation + QLatin1Char('\t')
                 + newOriginal + QLatin1Char('\t') + newTranslation + QLatin1Char('\t')
                 + category + QLatin1Char('\t') + decisionId + QLatin1Char('\n');
    }

    // 归因表条数的独立上界（冻结白名单的类别条数）：每条差异都要由白名单里的既有类别承担，
    // 白名单在 todo 1(d) 冻结（本批只读）。这条挡的是「把每行都标成差异、再给每行补一条归因」
    // 式的自指吸收 —— 那种做法会让归因表条数超过白名单类别总数，而 diffCount 与实际差异数
    // 仍相等，故紧随其后的 QCOMPARE 抓不到。
    const int frozenCategoryCount = static_cast<int>(whitelist.size()) - 1;
    QVERIFY2(static_cast<int>(diffAttributions().size()) <= frozenCategoryCount,
             qPrintable(QStringLiteral("归因表条数 %1 超过冻结白名单类别数 %2")
                            .arg(QString::number(diffAttributions().size()), QString::number(frozenCategoryCount))));
    QCOMPARE(diffCount, diffAttributions().size());

    qInfo().noquote() << "[task-31] 逐行差异表（冻结 inputs × 旧 golden × 新口径）:\n" + table;
}

QTEST_GUILESS_MAIN(LyricsModelSemanticGoldenTest)

#include "tst_lyrics_model_semantic_golden.moc"
