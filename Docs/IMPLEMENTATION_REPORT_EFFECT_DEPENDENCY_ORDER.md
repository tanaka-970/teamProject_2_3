# UI Effect Stack・Component 依存・実行順序 実装報告

日付: 2026-08-14

対象設計:

- `Docs/FIX_EFFECT_STACK_DESIGN.md`
- `Docs/FIX_DEPENDENCY_AND_ORDER_DESIGN.md`

項目 9（Nested Prefab）は対象外のまま変更していない。

## 1. 実装結果の要約

- UI Effect Stack は、読み込み後 Inspector 再構築、Image/Text の RT 確保式、
  HLSL 最大変位に基づく `ExpandBounds`、積算上限、42 種の種類別既定値、Mask 半径を修正した。
- Component 依存は `ComponentTypeInfo::required_components` を正本とする
  `ComponentDependencyRules` へ統一した。Editor 追加入口 4 箇所、削除保護、
  Scene/Prefab 復元を同じ規則へ寄せた。
- Scene/Prefab 復元では、自動追加が後続の保存 StableID を先取りしないよう、
  保存データの最大 StableID より上から自動採番する。
- 実行順序は `Update` / `FixedUpdate` / `LateUpdate` の各箱の中だけへ導入した。
  全値 0 は既存二重ループそのものを通る。
- Debug x64 ビルド、Behaviour、Script lifecycle、Motion、PropertyLink、Shader compile は成功した。

## 2. UI Effect Stack

### 2.1 共通修正

- `UIEffectStackComponent.cpp:517-550`: 保存値を全部復元した後に
  `RebuildDynamicProperties()` を再実行する。
- `UIRendererRender.cpp:1426-1429` と `:1529-1532`: Image/Text とも、
  `source_size * scale + expansion` とし、実ピクセルの確保量へ Canvas scale を掛けない。
- `UIEffectStackComponent.cpp:615-640`: 16 段の積算結果を各辺 2048px で制限する。
- `UIEffectStackComponent.cpp:179-439`: 42 種の種類別既定値を 1 関数へ集約した。
  `seed` は変更しない。
- `UIEffectStackComponent.cpp:553-585`: `property_name == nullptr` の早期 return を維持し、
  `effects[N].type` 変更時だけ種類別既定値を適用する。

### 2.2 ExpandBounds の式と HLSL 根拠

記号:

- `m = 2px`（丸め余裕）
- `r = max(radius, 0)`
- `a = abs(amount)`
- `W`, `H` = 対象 RT の実ピクセル幅・高さ
- `R(c)` = 指定中心 `c` から最遠角までの実ピクセル距離
- `theta = abs(angle) * pi / 180`

| 種類 | 片側の新しい式 | HLSL 根拠 |
|---|---|---|
| Blur | `r + m` | `ui_effect_blur.hlsl:38-45`。最遠タップが `radius` |
| Glow | `r + m` | `ui_effect_glow.hlsl:40-47`。最遠タップが `radius` |
| Outline | `r + m` | `ui_effect_outline.hlsl:29-34` |
| Kuwahara | `r + m` | `ui_effect_kuwahara.hlsl:56-67` |
| Light Streaks | `r + m` | `ui_effect_light_streaks.hlsl:41-43`。正負両方向へ length |
| Edge Detect | `max(r, 0.25) + m` | `ui_effect_edge_detect.hlsl:27-35`。Sobel の最遠 texel |
| Shake | `0.5 * abs(amount * intensity) + m` | `ui_effect_shake.hlsl:27-29`。Hash を `-0.5..0.5` 化 |
| Distortion | `abs(amount * intensity) + m` | `ui_effect_distortion.hlsl:22-27` |
| Chromatic Aberration | `abs(amount * intensity) + m` | `ui_effect_chromatic_aberration.hlsl:23-29` |
| Directional Blur | `0.5 * a + m` | `ui_effect_directional_blur.hlsl:26-33`。`t` は `-0.5..0.5` |
| Radial Blur | `0.5 * a + m` | `ui_effect_radial_blur.hlsl:31-37`。`distance_scale <= 1` |
| Long Shadow | `a + m` | `ui_effect_long_shadow.hlsl:29-30` |
| Ripple | `a + m` | `ui_effect_ripple.hlsl:37-40`。sin 変位の最大値 |
| Brush Stroke | `max(r, max(a, 0.5)) + m` | `ui_effect_brush_stroke.hlsl:70-76`。楕円長軸・短軸 |
| Rotational Blur | `R(c) * 2*sin(min(theta/4, pi/2)) + m` | `ui_effect_rotational_blur.hlsl:34,42-43`。角度全幅の半分まで回転 |
| Twirl | `r*H * 2*sin(min(theta/2, pi/2)) + m` | `ui_effect_twirl.hlsl:39-46`。高さ基準 radius 内の最大弦長 |
| Spherize | `r*H * abs(displacement_scale) + m` | `ui_effect_spherize.hlsl:30-38`。`scale` の下限 0.05 も含める |
| Lens Distortion | `a * normalized_radius^2 * R(c) + m` | `ui_effect_lens_distortion.hlsl:25-30` |
| Waveform | `a * (1 + saturate(progress)) + m` | `ui_effect_waveform.hlsl:79-86,99,112-120`。基本波＋うねり |
| Glitch | `a + abs(intensity) + m` | `ui_effect_glitch.hlsl:32-39`。band shift と channel shift の和 |
| VHS | `r + max(a, abs(threshold)) + m` | `ui_effect_vhs.hlsl:30-48`。行揺れ `[-1,1]*radius` に bleed/RGB shift が重なる |

VHS について、設計書の表には `0.5 * radius` とあるが、実 HLSL は
`(row_noise * 2 - 1) * radius` で最大 `abs(radius)` だった。
設計書 3 章の「HLSL が動かしうる最大距離」に従い、HLSL と一致する式を採用した。

### 2.3 ExpandBounds を 0 のままにした種類

次の 3 種は近傍／セル中心をサンプルするが、結果は現在の出力ピクセルへ書くだけで、
矩形外へ出力形状を生成しない。

- Mosaic: `ui_effect_mosaic.hlsl:34-38`
- Crystallize: `ui_effect_crystallize.hlsl:50-53`
- Stained Glass: `ui_effect_stained_glass.hlsl:56-60`

次の 18 種も、現在の出力ピクセルの色または alpha を変更するだけである。
UV を変換する Polar Coordinates / CRT も、変換先を現在ピクセルへサンプルする領域フィルタであり、
矩形外へ新しい出力ピクセルは作らない。

- Color Adjust
- Noise
- Mask
- Wipe
- Dissolve
- Halftone
- Vignette
- Posterize
- Threshold
- Color Ramp
- Levels
- Temperature
- Cross Hatch
- Polar Coordinates
- Scanlines
- CRT
- Dither
- Letterbox

実装上の根拠コメントは `UIEffect.cpp:141-170` に置いた。

### 2.4 42 種の種類別既定値

表にないフィールドは種類変更時に書き換えない。`seed` は全種類で維持する。

| # | 種類 | 変更する既定値 |
|---:|---|---|
| 0 | Blur | radius=8, intensity=1 |
| 1 | Glow | radius=12, intensity=1.5, threshold=0, color=(1,1,1,1) |
| 2 | Color Adjust | radius=1, intensity=1, amount=0, angle=0, color=(1,1,1,1) |
| 3 | Noise | intensity=0.08, amount=2, speed=1, color=(1,1,1,1) |
| 4 | Shake | amount=6, intensity=1, speed=8 |
| 5 | Mask | amount=1, angle=0, softness=0.02, direction=(0.5,0.5), speed=0.5 |
| 6 | Wipe | progress=0.5, angle=0, softness=0.05 |
| 7 | Dissolve | progress=0.35, threshold=0.08, color=(1,0.35,0.05,1) |
| 8 | Distortion | threshold=12, amount=6, intensity=1, speed=1 |
| 9 | Chromatic Aberration | amount=4, intensity=1 |
| 10 | Kuwahara | radius=5, intensity=1 |
| 11 | Halftone | radius=8, intensity=1, angle=15, softness=0.15, color=(1,1,1,1) |
| 12 | Directional Blur | angle=0, amount=12, intensity=1 |
| 13 | Radial Blur | direction=(0.5,0.5), amount=16, intensity=1 |
| 14 | Rotational Blur | direction=(0.5,0.5), angle=12, intensity=1 |
| 15 | Vignette | radius=0.35, softness=0.35, intensity=0.65, color=(0,0,0,1) |
| 16 | Light Streaks | amount=4, radius=24, angle=0, threshold=0.6, intensity=1.2, color=(1,1,1,1) |
| 17 | Lens Distortion | direction=(0.5,0.5), amount=0.2, intensity=1 |
| 18 | Posterize | amount=6, intensity=1 |
| 19 | Threshold | threshold=0.5, softness=0.04, intensity=1, color=(1,1,1,1) |
| 20 | Color Ramp | color=(0.02,0.02,0.08,1), color2=(0.12,0.18,0.55,1), color3=(0.95,0.32,0.12,1), color4=(1,0.95,0.55,1), stops=(0.333333,0.666667,1), intensity=1 |
| 21 | Levels | threshold=0.05, amount=0.95, angle=0.15, direction=(0,1), intensity=1 |
| 22 | Temperature | angle=0.35, progress=0.05, intensity=1 |
| 23 | Edge Detect | radius=1, intensity=2, color=(1,1,1,1) |
| 24 | Outline | radius=3, intensity=1, color=(0,0,0,1) |
| 25 | Long Shadow | angle=45, amount=24, intensity=1, color=(0,0,0,0.75) |
| 26 | Cross Hatch | radius=8, angle=45, amount=3, softness=0.1, intensity=0.85, color=(0.05,0.05,0.05,1) |
| 27 | Brush Stroke | radius=8, amount=3, threshold=0.1, intensity=0.8 |
| 28 | Mosaic | radius=12, intensity=1 |
| 29 | Crystallize | radius=24, threshold=0.45, intensity=1 |
| 30 | Stained Glass | radius=24, threshold=0.12, softness=0.2, intensity=1, color=(0,0,0,1) |
| 31 | Twirl | direction=(0.5,0.5), angle=180, radius=0.45, intensity=1 |
| 32 | Spherize | direction=(0.5,0.5), angle=0.55, radius=0.45, intensity=1 |
| 33 | Ripple | direction=(0.5,0.5), radius=48, amount=6, speed=2, intensity=1 |
| 34 | Polar Coordinates | direction=(0.5,0.5), progress=0.65, angle=0 |
| 35 | Scanlines | radius=4, intensity=0.25, speed=30, color=(0,0,0,1) |
| 36 | CRT | progress=0.12, radius=4, intensity=0.2, threshold=0.25, softness=0.35 |
| 37 | Glitch | radius=12, amount=8, threshold=0.18, intensity=2, speed=8 |
| 38 | Dither | amount=6, radius=4, intensity=1 |
| 39 | VHS | radius=3, amount=4, threshold=1, softness=0.04, speed=1.5 |
| 40 | Letterbox | radius=2.35, softness=0.01, intensity=1, color=(0,0,0,1) |
| 41 | Waveform | amount=9, radius=420, speed=2, softness=0, progress=0.25, intensity=0.6, angle=0, direction=(0,1), threshold=0.15, waveform=0 |

### 2.5 Mask の見た目変更

`Shader/ui_effect_mask.hlsl:32` の境界を `shape=0.5` 相当から `shape=1.0` 相当へ変更した。
同じ保存 `radius` なら実効半径が 2 倍になるため、円の直径、矩形の幅・高さも 2 倍になる。
保存形式と値は変えず、「半径」と表示した値が実際の半径になる。

## 3. Component 依存

### 3.1 共通規則

`ComponentDependencyRules` を追加した（`ComponentDependencyRules.h:63-81`）。
正本は既存の `ComponentTypeInfo::required_components` のままで、別表は作っていない。

- `PlanRequiredAdd`: DFS で全依存を収集し、循環・未登録・Runtime 不可を生成前に検出する。
- `ApplyRequiredAddPlan`: 検証済み順序を依存先から生成する。
- `FindDirectDependents`: 同じ owner の生存 Component だけから直接依存元を返す。

Editor の追加入口は 4/4 を共通化した。

1. Add Component Panel: `AddComponentPanel.cpp:160-163`
2. Inspector の不足関係追加: `InspectorPanelComponents.cpp:275-279`
3. 複数選択一括追加: `InspectorPanelMultiSelection.cpp:184-208`
4. Player 構成修復: `InspectorPanelMultiSelection.cpp:468-472`

### 3.2 Scene / Prefab 復元

保証は共通の `BuildComponents` に置いた（`SceneDataApply.cpp:53-259`）。

- 保存済み型集合を先に作り、保存データにある型を既定値で先取りしない。
- 欠落依存だけを要求元より前へ追加し、保存 Component 同士の相対順を維持する。
- 保存 StableID の最大値より上へ自動採番位置を進める（`:64-72`）。
  これがないと自動追加が後続の保存 StableID を先取りする。
- 自動追加数と未解決依存辺数を別カウンタで報告する（`SceneData.h:192-193`）。
- 未解決でも dependent と Prefab root は残し、warning を返す。
- Runtime 上位も未解決数をログ／診断へ出す。

### 3.3 Editor 削除保護

- 単体 Inspector は直接依存元があれば削除ボタンを無効化する
  （`InspectorPanelComponents.cpp:443`）。
- 押下処理でも再確認し、`RemoveComponent` の戻り値が true の場合だけ Commit する
  （`InspectorPanel.cpp:131` 以降）。
- 複数選択は全 Object を preflight し、1 件でも不可能なら全件中止する
  （`InspectorPanelMultiSelection.cpp:57-89` ほか）。
- `GameObject::RemoveComponent` へ依存拒否は追加していない。
- `RuntimeContext::AddComponent` / `DestroyComponent` は変更していない。

## 4. Update 実行順序

### 4.1 既存 Scene 調査

`resources/Scenes/` の `.replayscene` と `.replayscene.bak` を全 11 ファイル調査した。
`__script.execution_order` / `execution_order` は 0 件で、NUL を含むファイルも 0 件だった。
したがって既存保存 Scene に非 0 指定はない。

### 4.2 実装

- `Core::Component::ExecutionOrder()` は既定 0（`Component.h:133`）。
- `BehaviourComponent` は既存 `execution_order` を返す（`BehaviourComponent.h:65-68`）。
- `ScriptComponent` は既存保存値を返す（`ScriptComponent.h:102`）。
- 全値 0 は `Scene.cpp:428-441`, `:462-475`, `:497-510` の既存二重ループをそのまま通る。
- 非 0 があれば、フェーズ開始時の Component を snapshot し、
  `(execution_order, discovery_index)` 昇順で呼ぶ（`Scene.cpp:26-98`）。
- 呼び出し直前に `PendingDestroy` と `ActiveInHierarchy` を再確認する。
- 更新中に追加された Component は次の同フェーズから実行する。
- 対象は `Update` / `FixedUpdate` / `LateUpdate` の 3 箱だけ。
- Motion / PropertyLink / UI Sprite / UILayout の後段コードと `MotionMixer` は変更していない。

修正前 `f515963` の `Scene.cpp` は `ExecutionOrder()` を参照せず二重ループだけだったため、
新設した `-100,0,100` 検証の期待列にはならない。旧 revision の別ビルドは行っていないが、
修正前コード経路では検証が NG になることを式と呼び出し列で確認した。

## 5. 検証結果

最終状態:

- Debug x64 (`3dgp.sln`): 成功、警告 0、エラー 0
- `--validate-behaviour`
  - Execution order / Motion setter: 10 checks passed
  - Component dependency: 16 checks passed
  - Behaviour lifecycle: 28 checks passed
- `--validate-script-lifecycle`: 48 checks passed
- `--validate-motion-trigger`: OK
- `--validate-motion-events`: 7 cases OK
- `--validate-property-link`: OK
- `--validate-shader-compile`: 40 checks passed

実行順序の自動検証は次を含む。

- 全値 0 の Update / Fixed / Late が従来発見順と一致
- `-100,0,100` の各フェーズ昇順
- 同値の GameObject 順 x Component 順
- disabled、削除予約、更新中追加／削除
- 複数 fixed substep
- ScriptComponent の 3 フェーズ昇順
- 同一 Property への Motion 2 寄与でも setter は 1 回

依存の自動検証は次を含む。

- 2 段依存の収集後生成
- 循環・未登録で部分追加なし
- 直接依存元と削除予約除外
- Scene 読み込みと Runtime Prefab 配置
- OnRuntimeAwake 前の補完
- 保存 StableID 7/8 の維持
- 未解決 Prefab の root 維持と warning

## 6. 提出前自己点検

| 確認 | 結果 |
|---|---|
| 括弧なし `std::max(` / `std::min(` | 0 件 |
| 新規 C++ が `.vcxproj` / `.filters` の両方にある | 4/4 |
| BOM | 既存ファイルは変更前と一致 |
| 改行混在 | 0 件。既存の主形式へ統一 |
| OnDeserialize 末尾の再構築 | あり |
| OnPropertyChanged nullptr 早期 return | あり |
| scale を expansion に掛けない RT 箇所 | 2/2 |
| 0 のままの Effect の根拠コメント | 全件あり |
| HLSL 分岐内 return | 0 件 |
| 42 種の既定値 case | 42/42、seed 書き込み 0 |
| Scene.cpp の既存 3 ループ | 3/3 残存 |
| Editor 追加入口の共通化 | 4/4 |
| GameObject::RemoveComponent の依存拒否 | 追加なし |
| RuntimeContext Add/Destroy | 変更なし |
| 「未反映」ツールチップ | 0 件 |
| 項目 9 / PrefabSerializer | 変更なし |
| `git diff --check` | 問題なし |

## 7. コミット

- `f515963` — エフェクトスタックの修正
- `b48d477` — 依存関係・実行順序の実装保存
- `a2f437d` — 依存関係と実行順序の検証を追加
