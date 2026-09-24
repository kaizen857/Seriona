#include "lyrics_model.h"

#include "seriona/control/control_contracts.h"

#include <QtTest/QTest>

#include <QDir>
#include <QFile>
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
// 歌词渲染语义基线的输入集（todo 1 / W0 第一阶段，在此**钉死**）。
//
// 约束（见 .omo/plans/lyric-split-auto-detect.md todo 1）：
//   * 只能取自 W0 时点已存在的东西 —— 即权威设计文档正文里已经印出来的行。
//     docs/lyrics-original-translation-split-research-and-design-2026-09-19.md
//     每一行都必须能被 `rg -F` 在该文档正文中逐字命中。
//   * 只覆盖 ` / ` 强约定分支下可达的 4 类路由：
//     `//` 哨兵(qq-sentinel) / strong / strong-weak-fallback / validate-failed。
//     no-convention / bracket / weak 三类由后续 todo 承担，**不含 empty**。
//   * 不要求新旧两端结论相同；要求的是差异可穷举、可裁决。
//
// 输入集是「两阶段增长」的第一阶段：不依赖任何后续 todo 的产物
// （尤其不使用 todo 3 的 Seriona_Backend/tests/fixtures/lyric_split/golden-lines.lrc）。
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
        // golden-lines.lrc 的 7 行与上面第一阶段**逐字重复**（第一阶段独立取自同一份设计文档
        // §6.5 与第 179/374 行），故此处只补尚未覆盖的 13 行：既让 inputs.txt 覆盖夹具**全部**
        // 行，又不制造重复行。每行依旧能在设计文档正文或参考实现源码中 `rg -F` 到。
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

seriona::scanner::PlaylistNode makeRootNode()
{
    seriona::scanner::PlaylistNode node;
    node.nodeId = "root";
    node.kind = seriona::scanner::PlaylistNodeKind::Root;
    node.displayName = "Library";
    node.childNodeIds = {"node-semantic"};
    return node;
}

seriona::scanner::LyricLine makeLyricLine(std::chrono::milliseconds timestamp, const QString &text)
{
    seriona::scanner::LyricLine line;
    line.timestamp = timestamp;
    line.text = text.toStdString();
    return line;
}

seriona::scanner::PlaylistNode makeTrackNode(const QString &trackId,
                                             const std::vector<seriona::scanner::LyricLine> &lyrics)
{
    seriona::scanner::SongMetadata song;
    song.trackId = trackId.toStdString();
    song.filePath = "/music/semantic-golden.flac";
    song.sourceFilePath = "/music/semantic-golden.flac";
    song.title = trackId.toStdString();
    song.effectiveLyrics = lyrics;

    seriona::scanner::PlaylistNode node;
    node.nodeId = "node-semantic";
    node.parentNodeId = "root";
    node.kind = seriona::scanner::PlaylistNodeKind::Track;
    node.displayName = trackId.toStdString();
    node.song = song;
    return node;
}

seriona::control::LibraryStateSnapshot makeLibrary(const std::vector<seriona::scanner::LyricLine> &lyrics)
{
    seriona::control::LibraryStateSnapshot library;
    library.libraryTree = seriona::scanner::PlaylistTreeSnapshot{};
    library.libraryTree->version = 1;
    library.libraryTree->rootNodeId = "root";
    library.libraryTree->nodes = {makeRootNode(), makeTrackNode(QStringLiteral("semantic-track"), lyrics)};
    return library;
}

seriona::control::PlayerStateSnapshot makePlayer(const QString &trackId)
{
    seriona::control::PlayerStateSnapshot player;
    player.currentTrack = seriona::control::TrackIdentity{};
    player.currentTrack->trackId = trackId.toStdString();
    player.currentTrack->filePath = "/music/semantic-golden.flac";
    player.timeline.position = std::chrono::milliseconds{0};
    return player;
}

QVariant modelValue(const Seriona::App::LyricsModel &model, int row, int role)
{
    return model.data(model.index(row, 0), role);
}

// 强制 LF 写入：避免 Windows 下 QTextStream 文本模式把 `\n` 变成 `\r\n`，
// 破坏 inputs.txt 与 tsv 的逐行一一对应。
bool writeUtf8Lf(const QString &path, const QString &content, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = QStringLiteral("无法写入 %1: %2").arg(path, file.errorString());
        return false;
    }
    file.setTextModeEnabled(false);
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << content;
    stream.flush();
    const bool ok = stream.status() == QTextStream::Ok;
    file.close();
    if (!ok) {
        *error = QStringLiteral("写入 %1 时 QTextStream 出错").arg(path);
    }
    return ok;
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

}  // namespace

// 只记录**现状**：把输入行喂给当前 LyricsModel，逐行 dump (原文, 译文)。
// 本测试不"修好"任何行为，也不断言切分结果是否正确 —— 它只固化基线。
class LyricsModelSemanticGoldenTest : public QObject
{
    Q_OBJECT

private slots:
    void dumpSemanticGolden();
};

void LyricsModelSemanticGoldenTest::dumpSemanticGolden()
{
    const QStringList inputs = goldenInputs();
    QVERIFY(!inputs.isEmpty());

    std::vector<seriona::scanner::LyricLine> lyrics;
    lyrics.reserve(static_cast<std::size_t>(inputs.size()));
    for (qsizetype i = 0; i < inputs.size(); ++i) {
        lyrics.push_back(makeLyricLine(std::chrono::milliseconds{i * 1000}, inputs.at(i)));
    }

    const seriona::control::PlayerStateSnapshot player = makePlayer(QStringLiteral("semantic-track"));
    const seriona::control::LibraryStateSnapshot library = makeLibrary(lyrics);

    Seriona::App::LyricsModel model;

    // 不调用 setLyricDelimiters：保持 lyrics_model.h 的默认分隔符 {QStringLiteral(" / ")}，
    // 这样 dump 记录的是"今天"的默认切分路径。
    QCOMPARE(model.lyricDelimiters(), QStringList{QStringLiteral(" / ")});

    model.applyPlayerStateSnapshot(player, &library);

    QCOMPARE(model.rowCount(), static_cast<int>(inputs.size()));

    const QString dir = QString::fromUtf8(SERIONA_LYRICS_GOLDEN_DIR);
    QVERIFY2(QDir().mkpath(dir), qPrintable(QStringLiteral("无法创建目录 %1").arg(dir)));

    const QString inputsPath = dir + QStringLiteral("/lyrics-model-semantic-golden.inputs.txt");
    const QString tsvPath = dir + QStringLiteral("/lyrics-model-semantic-golden.tsv");

    QString inputsDump;
    QString tsvDump;
    for (qsizetype row = 0; row < inputs.size(); ++row) {
        inputsDump += inputs.at(row);
        inputsDump += QLatin1Char('\n');

        const QString original = modelValue(model, static_cast<int>(row),
                                            Seriona::App::LyricsModel::DisplayLineRole).toString();
        const QString translation = modelValue(model, static_cast<int>(row),
                                               Seriona::App::LyricsModel::TranslationRole).toString();
        tsvDump += original;
        tsvDump += QLatin1Char('\t');
        tsvDump += translation;
        tsvDump += QLatin1Char('\n');
    }

    QString error;
    QVERIFY2(writeUtf8Lf(inputsPath, inputsDump, &error), qPrintable(error));
    QVERIFY2(writeUtf8Lf(tsvPath, tsvDump, &error), qPrintable(error));

    // inputs.txt 与 tsv 行数必须一致且一一对应（同一顺序）——这是两阶段对照的基础。
    const QStringList inputsLines = readLines(inputsPath);
    const QStringList tsvLines = readLines(tsvPath);
    QCOMPARE(inputsLines.size(), inputs.size());
    QCOMPARE(tsvLines.size(), inputs.size());
    QVERIFY(!inputsLines.isEmpty());
    QVERIFY(!tsvLines.isEmpty());
    QCOMPARE(inputsLines, inputs);

    for (const QString &tsvLine : tsvLines) {
        // 每行恰好一个 TAB（原文 / 译文），且原文非空。
        QCOMPARE(tsvLine.count(QLatin1Char('\t')), 1);
        QVERIFY(!tsvLine.section(QLatin1Char('\t'), 0, 0).isEmpty());
    }
}

QTEST_GUILESS_MAIN(LyricsModelSemanticGoldenTest)

#include "tst_lyrics_model_semantic_golden.moc"
