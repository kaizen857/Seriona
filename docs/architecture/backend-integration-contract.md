# 后端集成契约

Seriona 前端 QML 不直接持有后端状态；中间层 owners 负责把后端快照映射为 QML 可绑定状态，并把用户意图提交给后端。

- `PlaybackController`：播放 read model 与播放命令提交。
- `LibraryController`：曲库扫描、曲库树快照、浏览投影、空曲库和后端不可用状态。
- `LyricsModel`：歌词 read model；透传后端 `TrackLyricsSnapshot` 的每行 original/translation，并持有本地歌词显示状态（当前行、播放位置、译文显隐）。
- `NavigationController`：启动/主界面、本地导航和 sidebar 状态。
- `SettingsController`：音频输出与播放过渡设置；经 `BackendBridge::submitConfigureOutput` / `submitTransitionConfig` 提交，设备能力过滤状态见下；均衡器设置与频谱显示同经真实命令链外发、状态与快照订阅镜像回本控制器只读面（见「均衡器与频谱显示域契约」）。UI 自本版起仅 Mixed 输出模式（无 Direct 选择，灰化机制已移除）。

后端 `PlayerStateSnapshot`、`LibraryStateSnapshot`、`PlaylistTreeSnapshot`、`TrackLyricsSnapshot` 和命令结果仍是权威事实来源；前端不得用生产假数据替代缺失的后端曲库内容。

## 播放过渡配置命令：SetTransitionConfig

后端（Seriona_Backend）`MediaControlCommandKind::SetTransitionConfig` + 载荷 `MediaControlCommand::transitionConfig`（类型 `seriona::audio::TransitionConfig`，定义于 `inc/seriona/audio/audio_contracts.h`）。

- 通道：与 `ConfigureOutput` 同通道的 `submitCommand` 单意图命令。后端归约器只存储过渡配置，**不产生 LoadTrack、设备重开或任何尾意图**（播放中提交不影响当前播放，无重载副作用）。
- 前端链路：`SettingsController`（键组 `transition`，9 键持久化于应用设置存储）→ 滑块 400ms 去抖、档位/开关立即推送当前全量现值 → AppFacade 注入的 executor → `BackendBridge::submitTransitionConfig`（本地预校验）→ `submitCommand(SetTransitionConfig)`；启动/重连时随输出组 apply 一次。
- 组包顺序 = `TransitionConfig` 字段声明顺序 = 后端归约器解析顺序（跨端契约，勿调整）。
- 默认值等价：9 项全默认（`TransitionConfig{}`）时后端采样路径与过渡引擎引入前基线逐位一致（后端回归总闸锁定）；0 时长 = 该淡变即时完成，不做下界钳制。

### TransitionConfig 字段表

| 设置号 | 字段 | 取值/枚举语义 | 默认 | 量程（滑块步进 100ms） | 生效输出模式 |
|---|---|---|---|---|---|
| 1 | `autoAdvanceFadeMode` | `Off`(0)=不交叉（CUE 无间隙组内尽力无缝）；`ExceptGaplessGroup`(1)=除 CUE 邻曲/无间隙组外交叉；`All`(2)=全交叉 | `Off` | 枚举 [0,2] | 仅 Mixed |
| 2 | `fadeOnTransport` | 播放/暂停/停止淡入淡出开关 | `false` | 无（开关）| 全局（含 Direct） |
| 3 | `fadeOnSeek` | seek 淡入淡出开关 | `false` | 无（开关）| 全局（含 Direct） |
| 4 | `gaplessPreloadMs` | 无间隙音轨预解码触发提前量 | `0` | [0,5000] | 仅 Mixed |
| 5 | `crossfadeMs` | 交叉淡入淡出长度（自动交叉与手动档 FullCrossfade 共用） | `3000` | [0,10000] | 仅 Mixed |
| 6 | `transportFadeMs` | 播放/暂停/停止淡变长度 | `300` | [0,3000] | 全局（含 Direct） |
| 7 | `seekFadeMs` | seek 淡变长度 | `300` | [0,3000] | 全局（含 Direct） |
| 8 | `manualAdvanceFadeMode` | `Off`(0)=无；`ShortDip`(1)=短时渐隐 dip（长度=`manualShortCrossfadeMs`，对半分解）；`FullCrossfade`(2)=交叉淡入淡出（长度=`crossfadeMs`） | `Off` | 枚举 [0,2] | 仅 Mixed |
| 9 | `manualShortCrossfadeMs` | 手动档 ShortDip 的淡变长度 | `500` | [0,3000] | 仅 Mixed |

### 校验与拒绝

- 后端归约器（`handleSetTransitionConfig`）与前端 `BackendBridge::submitTransitionConfig` 本地预校验逐条同界：两枚枚举 int ∈ [0,2]；`crossfadeMs` [0,10000]；`transportFadeMs`/`seekFadeMs`/`manualShortCrossfadeMs` [0,3000]；`gaplessPreloadMs` [0,5000]；负值一并拒绝。越界 → `InvalidCommand` 结果 + `CommandRejected` 域通知（过渡组无 UI 回滚路径，前端控件量程已先拦截，实际不可达）。
- 输出模式能力语义：`transition` 组各行的生效范围按后端输出模式划分——仅 Mixed 生效：{1 自动档、4 预加载、5 交叉长度、8 手动档、9 手动短交叉}；全局（含 Direct）：{2,3,6,7} 传送/进度淡变。UI 自本版起仅 Mixed 输出模式，灰化机制已移除。
- Direct 播放语义：后端在 Direct 下忽略过渡档位与预解码（恒瞬时硬切 + 重开设备）；命令本身合法、不按输出模式拒绝。

## 过渡域事件契约（后端 audio 契约）

以下事件定义于 Seriona_Backend `inc/seriona/audio/audio_contracts.h`（`BackendEventType` + `PlaybackEvent` variant），是"音频服务 → 控制层"的内部事件；前端桥接面不直接接收 `BackendEvent`，只经 `PlayerStateSnapshot` / 域通知观察结果。列出以便跨仓契约追溯与行为预期。

| 事件/类型 | 载荷 | 语义 |
|---|---|---|
| `EndApproaching` | `{ remainingMs }` | 自然播完阈值预告（距终点毫秒估计），一次性发射（armed 去重）；Mixed 下自动前进侧启用预解码提前量或交叉档位时触发，Direct 与默认全关路径不发射 |
| `AdvanceCompleted` | `{ trackId }` | 无缝直切 / 交叉重叠交接完成通知，携带已接管的新曲 trackId；发射序先于新曲的 `TrackChanged`/状态事件；控制层凭 pendingAdvance 账本提交、不重发 LoadTrack |
| `prepareNext(request, meta)` | `PrepareNextKind`: `SeamlessDirect`（就绪即直切）/ `Crossfade`（双源重叠）；`PrepareNextMeta{kind, isGaplessGroup}` | 控制层在 `EndApproaching` 后经 `AudioPlaybackService::prepareNext` 下发预解码交接方式与无间隙组标记 |

前端可见行为预期：

- 启用预载/过渡后，自动前进由后端内部预解码交接完成：快照流表现为新曲 `TrackChanged`（位置从交接点延续，无 `PlaybackEnded`、无整轨重载）。
- 手动切歌（`SkipNext`/`SkipPrevious`/`SelectTrack`）在 Mixed + 档位下由 dip/交叉短暂延迟后完成（Loading → TrackChanged → Ready → Playing 背靠背呈现）；Direct 恒即时硬切。
- 未启用（默认全关）时自然播完仍走 `PlaybackEnded` → 控制层自动下一曲的既有流程；默认配置下整体行为与过渡引擎引入前一致。

## 歌词域契约（TrackLyricsSnapshot → LyricsModel）

歌词正文的切分（原文/译文判定、文档级约定推断）由后端完成；前端 `LyricsModel` 是透传 read model，不含按分隔符切分的逻辑，也没有兜底切分分支。

- 数据来源：`BackendBridge::registerSubscriptions` 注册的 **6 路订阅**（player / library / notification / equalizer / spectrum / trackLyrics）之一 `subscribeTrackLyrics` 回调 `TrackLyricsSnapshot`；`BackendBridge::trackLyricsChanged` → `AppFacade::handleTrackLyricsChanged` 把快照整份投给 `LyricsModel::applyTrackLyricsSnapshot`。`SERIONA_HAS_BACKEND=0`（mock-only）时该订阅不编译，`LyricsModel` 呈现空态。
- 前端消费字段：`trackId`（空则视为无快照、清空模型）、`convention`（仅 `BackendBridge` 读取并序列化为纠错命令载荷，见下），以及每行的 `timestamp` / `text`（`cleanLine` 之后的清洗行）/ `original` / `translation`（空串表示该行无译文）/ `manualOverride` / `autoOriginal` / `autoTranslation`。`targetLanguage`、`position`、`freshness` 属快照契约字段，前端当前不读取。
- 模型角色：既有 5 个（`RawLineRole` = `text`、`DisplayLineRole` = `original`、`TranslationRole` = `translation`、`CurrentRole`、`TimestampRole`）角色名与值不变；W3 起追加 `ManualOverrideRole` / `AutoOriginalRole` / `AutoTranslationRole`。`autoOriginal` / `autoTranslation` 是被 manual 覆盖之前的自动判定结果，仅在 `manualOverride` 为 true 时有值（未覆盖时后端留空）。
- 内容去重谓词为 `timestamp + text + original + translation`（仅译文变化也触发整份替换）。
- 行推进：`currentIndex` 由 `playbackPosition` 与各行 `timestamp` 计算，与内容来源无关。`LyricsModel::lines()` 供 QML 切歌动画取行快照（既有 3 键不变，另附 W3 三键）。

### 三层纠错入口（同一后端命令面）

| 层级 | 入口 | 前端链路 |
|---|---|---|
| 行级 | `qml/components/LyricLineContextMenu.qml`（`MainContent.qml` 在歌词行右键打开） | `AppFacade::commitLyricSplitCorrection(rawLine, original, translation)` → `BackendBridge::upsertLyricSplitCorrection` → `UpsertLyricSplitCorrection`；恢复本行 → `AppFacade::removeLyricSplitCorrection` → `RemoveLyricSplitCorrection` |
| 整首 | `qml/windows/LyricSplitEditorWindow.qml`（`Main.qml` 实例化） | 拖动分界 → `AppFacade::lyricSplitBoundaryParts` / `lyricSplitBoundaryCut` / `commitLyricSplitBoundary`（分界换算为 header-only 纯函数 `src/app/lyric_split_boundary.h`），仍落到同一条 upsert 命令 |
| 管理列表 | `qml/windows/SettingsWindow.qml` 内的 `LyricCorrectionManager`（`qml/components/LyricCorrectionManager.qml`） | 当前曲目 `manualOverride` 行的呈现层；逐行恢复对每行调用一次 `RemoveLyricSplitCorrection`（键为 `rawLine`，即快照 `text`） |

- 行级与整首两处的提交门同源：`AppFacade::isLyricOriginalSubmittable`（原文非空，含全空白视为空）与 `AppFacade::lyricSplitBoundaryCommitAllowed`（原文非空且构成真实变更）；未过门时不外发命令。
- `lyricConvention` 载荷取自**当前** `TrackLyricsSnapshot.convention`，经 `seriona::control::conventionToken` 序列化；快照未命中时按约定为 None（token `-`），前端不自造约定值。
- 纠错管理列表的范围限定为当前曲目；切歌或列表增删后由 `LyricCorrectionManager::refresh()` 整份重建。

### 译文语言设置

- `SettingsController` 键组 `lyrics` / 键 `targetLanguage`（默认 `zh`），QML 在设置面板「译文语言」下拉绑定；变更即经注入的 executor → `BackendBridge::setLyricsTargetLanguage` 外发 `SetLyricsTargetLanguage` 真命令，载荷为语言 token（`zh` / `ja` / `ko` / `en`）。
- 启动路径在设置 reload 后经 `SettingsController::applyLyricsTargetLanguage()` 同步一次。

## 均衡器与频谱显示域契约

均衡器命令面（`SetEqualizerConfig`）与频谱显示命令面（`SetSpectrumEnabled`）是前端中间层两条已接通的真实后端链路；本仓 `EqualizerWindow.qml` 的图谱区渲染为前端自绘（`SpectrumGraph`，QSG），不依赖后端提供像素。命令载荷与快照类型定义于 Seriona_Backend `inc/seriona/audio/audio_contracts.h`（`EqualizerConfig` / `EqualizerStateSnapshot` / `SpectrumSnapshot`）与 `inc/seriona/control/control_contracts.h`，以下数字逐字对齐两侧实现。

### 命令面

- `SetEqualizerConfig`：载荷经 `BackendBridge::submitEqualizerConfig` 外发（键组 `equalizer`：总开关 `enabled`、档位 `bandMode`（10/31）、`bandGains10`/`bandGains31`（各 10/31 个 ±15 dB 值）、前置增益 `preGainDb`、限幅开关）。滑块类改动 50ms 去抖、开关/档位/预设/复位即时推送当前全量现值；启动/重连时随配置组 apply 一次。
- `SetSpectrumEnabled`：频谱显示离散开关（默认关）。点击即经独立真命令即时外发——不经 EQ 的 50ms 滑块去抖，缓存去重防同值重发，不占 `SetEqualizerConfig` 载荷。

### 镜像只读面（SettingsController 订阅镜像，纯内存态）

后端快照经订阅镜像回 `SettingsController` 只读面（`mirrorEqualizerCurve` / `mirrorSpectrumBins` 写者，由 AppFacade 从 BackendBridge 接线）；前端不节流、不放大，更新频率 = 后端发布频率。严禁持久化、严禁进 reload。

- `curvePoints` / `curveFrequencies`：各 **181 点**；对数频率轴 20–20k（20×1000^(i/180)）。曲线由后端控制层 reducer 纯函数先行合成（生效快照 `sampleRate` 未回填 → fs=0 → 全轴计算、截断分支不参与，见下）；长度不符的推送丢弃。
- `spectrumBins`：**120 桶** dBFS（满刻度归一；前端单一源 `kEqSpectrumBinCount` = 120，与后端 `binsDb` 定长锁定拷贝一致）；`mirrorSpectrumBins` 对长度 ≠120 的推送直接丢弃（防御异常数据破坏 QML 消费）。
- 均衡器状态镜像（10/31 增益、preGain、enabled、bandMode、限幅、预设 CRUD）驱动 EQ 界面与写回同键。

### 后端频谱分析契约（发布节奏与桶语义）

- 120 对数桶（20 Hz–20 kHz，桶边界 20×1000^(i/120)，i=0..120），桶能量按范围重叠比例分摊（Σ 可测桶 = Σ 参与 FFT bin，能量守恒；窄桶无死桶）。
- Hann 周期窗 + 前向 RDFT；窗长按采样率分档：fs ≤ 24k → 2048（22050 档 Δf = 10.8 Hz）、24k < fs ≤ 48k → 4096（44.1k：10.8 Hz；48k：11.7 Hz）、48k < fs ≤ 96k → 8192（96k：11.7 Hz）、fs > 96k → 16384（192k：11.7 Hz）。
- 重叠推进：**hop = 窗长/4**（相邻窗数据重叠 75%；每消耗 hop 帧产出一份分析）。
- 发布节奏：worker 按 **~10ms 轮询节流**（2ms tick × 5）取最新摘录块（512 帧），发布上界 **~40 Hz 级（44.1/48k 精确上界 ~43/45 Hz）**；采样率越高随窗档递减（96k ~25 Hz / 192k ~12.5 Hz，单一常量无法全率保 ~40 Hz——率依赖契约见后端 `spectrum_analyzer.h` / `audio_playback_service.cpp`）。
- fs < 40k（22050/32000）时桶轴按奈奎斯特截断；不可测桶与静音同置 **−120 dB 地板**。

### fs 截断语义（频段与曲线合成面）

- 后端 DSP 越界守卫：中心频率 ≥ 0.95×(fs/2) 的频段整体硬直通（不进滤波链；奈奎斯特外的增益命令零影响）。
- 曲线合成同源守卫仅 fs > 0 时参与；产品路径镜像快照恒 fs=0（sampleRate 未回填）→ 镜像 181 点曲线按全轴 20–20k 计算，截断分支不参与。
- 频段中心频率与 Q 常量单点定义于后端 `inc/seriona/audio/equalizer_tables.h`；前端物理合成函数（`equalizer_curve_synth::synthesizeEqualizerCurve` 拷贝自 reducer）与后端 reducer 同源公式（conformance 锁 worst ≤ 0.05 dB 漂移上界）。R5 起图谱区显示曲线改为**本地 PCHIP 手柄点包络**（`synthesizeGraphicEnvelopeCurve`，恒过手柄、与 DSP 物理响应解耦——消费级 GEQ 惯例），物理合成函数仅保留为同源锁/未来真实响应显示面，不再作拖动显示源。
