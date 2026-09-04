# 后端集成契约

Seriona 前端 QML 不直接持有后端状态；中间层 owners 负责把后端快照映射为 QML 可绑定状态，并把用户意图提交给后端。

- `PlaybackController`：播放 read model 与播放命令提交。
- `LibraryController`：曲库扫描、曲库树快照、浏览投影、空曲库和后端不可用状态。
- `LyricsModel`：歌词 read model 与本地歌词显示状态。
- `NavigationController`：启动/主界面、本地导航和 sidebar 状态。
- `SettingsController`：音频输出与播放过渡设置；经 `BackendBridge::submitConfigureOutput` / `submitTransitionConfig` 提交，Direct 灰化与设备能力过滤状态见下。

后端 `PlayerStateSnapshot`、`LibraryStateSnapshot`、`PlaylistTreeSnapshot` 和命令结果仍是权威事实来源；前端不得用生产假数据替代缺失的后端曲库内容。

## 播放过渡配置命令：SetTransitionConfig

后端（Seriona_Backend）`MediaControlCommandKind::SetTransitionConfig` + 载荷 `MediaControlCommand::transitionConfig`（类型 `seriona::audio::TransitionConfig`，定义于 `inc/seriona/audio/audio_contracts.h`）。

- 通道：与 `ConfigureOutput` 同通道的 `submitCommand` 单意图命令。后端归约器只存储过渡配置，**不产生 LoadTrack、设备重开或任何尾意图**（播放中提交不影响当前播放，无重载副作用）。
- 前端链路：`SettingsController`（键组 `transition`，9 键持久化于应用设置存储）→ 滑块 400ms 去抖、档位/开关立即推送当前全量现值 → AppFacade 注入的 executor → `BackendBridge::submitTransitionConfig`（本地预校验）→ `submitCommand(SetTransitionConfig)`；启动/重连时随输出组 apply 一次。
- 组包顺序 = `TransitionConfig` 字段声明顺序 = 后端归约器解析顺序（跨端契约，勿调整）。
- 默认值等价：9 项全默认（`TransitionConfig{}`）时后端采样路径与过渡引擎引入前基线逐位一致（后端回归总闸锁定）；0 时长 = 该淡变即时完成，不做下界钳制。

### TransitionConfig 字段表

| 设置号 | 字段 | 取值/枚举语义 | 默认 | 量程（滑块步进 100ms） | 生效输出模式 |
|---|---|---|---|---|---|
| 1 | `autoAdvanceFadeMode` | `Off`(0)=不交叉（CUE 无间隙组内尽力无缝）；`ExceptGaplessGroup`(1)=除 CUE 邻曲/无间隙组外交叉；`All`(2)=全交叉 | `Off` | 枚举 [0,2] | 仅 Mixed（Direct 灰化） |
| 2 | `fadeOnTransport` | 播放/暂停/停止淡入淡出开关 | `false` | 无（开关）| 全局（含 Direct） |
| 3 | `fadeOnSeek` | seek 淡入淡出开关 | `false` | 无（开关）| 全局（含 Direct） |
| 4 | `gaplessPreloadMs` | 无间隙音轨预解码触发提前量 | `0` | [0,5000] | 仅 Mixed（Direct 灰化） |
| 5 | `crossfadeMs` | 交叉淡入淡出长度（自动交叉与手动档 FullCrossfade 共用） | `3000` | [0,10000] | 仅 Mixed（Direct 灰化） |
| 6 | `transportFadeMs` | 播放/暂停/停止淡变长度 | `300` | [0,3000] | 全局（含 Direct） |
| 7 | `seekFadeMs` | seek 淡变长度 | `300` | [0,3000] | 全局（含 Direct） |
| 8 | `manualAdvanceFadeMode` | `Off`(0)=无；`ShortDip`(1)=短时渐隐 dip（长度=`manualShortCrossfadeMs`，对半分解）；`FullCrossfade`(2)=交叉淡入淡出（长度=`crossfadeMs`） | `Off` | 枚举 [0,2] | 仅 Mixed（Direct 灰化） |
| 9 | `manualShortCrossfadeMs` | 手动档 ShortDip 的淡变长度 | `500` | [0,3000] | 仅 Mixed（Direct 灰化） |

### 校验与拒绝

- 后端归约器（`handleSetTransitionConfig`）与前端 `BackendBridge::submitTransitionConfig` 本地预校验逐条同界：两枚枚举 int ∈ [0,2]；`crossfadeMs` [0,10000]；`transportFadeMs`/`seekFadeMs`/`manualShortCrossfadeMs` [0,3000]；`gaplessPreloadMs` [0,5000]；负值一并拒绝。越界 → `InvalidCommand` 结果 + `CommandRejected` 域通知（过渡组无 UI 回滚路径，前端控件量程已先拦截，实际不可达）。
- Direct 灰化规则（`SettingsController::advanceTransitionsGreyed`，NOTIFY `outputModeChanged`）：输出模式为 Direct（`outputMode==0`）时，仅 Mixed 生效的行 {1 自动档、4 预加载、5 交叉长度、8 手动档、9 手动短交叉} 在设置窗口灰化（禁用 + 0.45 透明度 + 行内提示「仅混合输出可用」）；{2,3,6,7}（传送/进度淡变，全局语义）恒可用。
- Direct 播放语义：后端在 Direct 下忽略过渡档位与预解码（恒瞬时硬切 + 重开设备），灰化只是 UI 对"不生效"的表达，命令本身合法、不按输出模式拒绝。

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
