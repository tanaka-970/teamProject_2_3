# RePlayEngine Phase 7 regression fix + Phase 10 / 16 implementation report

更新日: 2026-08-07

## このパッチの目的

このパッチは、Phase 7 適用後に Windows 実機で確認された以下の回帰を修正し、
そのまま次に必要な Shader Layer Asset 化（Phase 10）と Layer / Pass の役割分離（Phase 16）までまとめて進める。

直前の Windows 結果:

- BUILD=0
- SHADER_EDITOR=0
- SHADER_RENDER=1400
- SHADER_TEXTURE=1450
- SHADER_MATERIAL=0
- SHADER_BUILTIN=1224
- SHADER_ASSET=0
- MATERIAL_LEGACY=0

## 1. Phase 7 回帰修正

### 原因

Phase 7 で FbxDefault / Unlit / Pixelate の built-in HLSL が UTF-8 BOM 付きになっていた。
Runtime compile は自動生成した b9 cbuffer を HLSL の先頭へ挿入するため、元ファイル先頭の BOM が結合ソースの途中へ移動し、D3DCompile が失敗し得る。

これにより FbxDefault Static compile failure が `SHADER_BUILTIN=1224` を起こし、Catalog 全体の compile_failed が `SHADER_RENDER=1400` / `SHADER_TEXTURE=1450` へ連鎖した可能性が高い。

### 修正

- `ShaderSource.cpp` の HLSL 読み込みで UTF-8 BOM を除去。
- `ShaderLibrary.cpp` の compile 用読み込みでも UTF-8 BOM を除去。
- 現在の built-in HLSL 5 枚を BOM なしへ正規化。
- Layer validation に BOM 付き HLSL の runtime compile 回帰テストを追加。
- Hot Reload で fatal pragma / invalid lighting / GUID 消失・変更が起きた場合、壊れた metadata へ置換せず last-successful bytecode/schema を維持。

## 2. Phase 10 — Shader Layer Asset 化

### MaterialAsset v4

Material version を 4 に更新。
Layer の正本は次の 3 点:

- `ShaderID shader`
- `Reflection::PropertyBag properties`
- 永続 `uint64_t id`

`ShaderLayerType` と旧固定値は v2/v3 移行および既存 7 種の専用描画を維持する compatibility bridge とする。
新しい Layer Shader を増やす条件にはしない。

v4 は Layer ごとに以下を保存する。

- Shader GUID
- persistent layer ID
- blend
- enabled
- Layer PropertyBag
- 旧 7 種用 compatibility fields

Layer ID は Save / Reload 後も維持する。同じ Shader を複数枚積んでも別 Layer として識別できる。
重複 / 0 / UINT64_MAX の ID は loader / validation で拒否する。

### Built-in Layer 固定 GUID

Material が永続保存するため以下は変更禁止。

- Pixelate          `00000000000000000000000000000101`
- PBR Auxiliary     `00000000000000000000000000000102`
- Toon Auxiliary    `00000000000000000000000000000103`
- Unlit Glow        `00000000000000000000000000000104`
- Wireframe         `00000000000000000000000000000105`
- Outline           `00000000000000000000000000000106`
- StylizedCharacter `00000000000000000000000000000107`

### Editor

Material > Shader Stack の「追加パスを選ぶ...」は hard-coded enum 一覧ではなく、ShaderCatalog 内の `domain=layer` を列挙する。

- built-in Layer
- project custom Layer
- 将来 Shader Composer が生成する Layer

を同じ経路で表示できる。

各 Layer の UI は `ShaderPropertyInspector` が Schema から自動生成する。
Texture は Asset picker + Project Browser drag & drop を使う。
Missing Layer Shader でも PropertyBag は捨てない。

Project Browser の Create に `Layer Shader` を追加。
作成すると `Shader/Layers/Project/` に HLSL を生成し、AssetDatabase 登録後に ShaderLibrary を即 rescan するため、Material の Add Layer 一覧へすぐ現れる。

## 3. Phase 16 — Layer と Shader-owned Pass

役割を明確化した。

- Layer = Material が所有。ユーザーが追加 / 削除 / ON/OFF / 並び替えする。
- Pass = Shader が所有。宣言順固定。Material 側から並び替えない。

HLSL 宣言:

`#pragma replay_pass "Glow" GlowPass additive`

blend は以下。

- inherit
- alpha
- additive
- multiply

ShaderCatalog は base variant と各 pass variant の last-successful bytecode / diagnostics を保持する。
Custom Layer runtime は Layer main の直後に shader-owned pass を宣言順で実行する。

`ShaderExecutionPlan` を追加し、

Layer 1 main -> Layer 1 pass 1 -> Layer 1 pass 2 -> Layer 2 main -> ...

という展開規則をコード上でも固定した。

## 4. Outline bool の統合

`MaterialAsset::outline_pass` は削除した。
Outline の正本は `BuiltInShaderLayers::Outline` が Material LayerStack に存在すること。

古い v2/v3 ファイルの `OUTLINE_PASS=true` は Load 時に Outline Layer 1 枚へ移行する。
v4 のファイルには旧 reader 互換用の `OUTLINE_PASS` token を LayerStack から導出して書くが、MaterialAsset 内に bool 状態は持たない。

Outline Layer の Color / Width は描画直前に toon outline constants へ反映し、描画後に元の値へ戻す。
複数 Outline Layer でも個別値を使える。

## 5. Runtime Layer binding

Custom Layer Shader は既存 Phase 6 / 12 の基盤を再利用する。

Layer PropertyBag
-> ShaderLayerBindingResolver
-> ShaderCatalog
-> ShaderConstantPacker
-> b9
-> t40+
-> MaterialGpuBinder
-> D3D11 draw

Built-in 7 種は既存画面を変えないため、Windows Acceptance が通るまでは従来の専用描画パスを bridge として維持する。
Custom Layer は Catalog runtime path を使う。

## 6. 新Validation

### `--validate-shader-layer`

主な確認:

- built-in 7 Layer の固定 GUID と legacy enum 対応
- Layer domain の走査
- Static / Skinned compile
- Material v4 GUID / PropertyBag / order / enabled / blend / persistent ID roundtrip
- unknown / missing Layer Property retention
- v3 -> v4 migration
- outline bool -> Outline Layer migration
- 64 layers
- custom Layer Shader 作成 -> scan -> compile -> schema
- UTF-8 BOM shader compile regression

### `--validate-shader-pass`

主な確認:

- `replay_pass` parser
- unknown blend を fatal error にする
- main + 複数 pass の Static / Skinned compile
- Catalog の pass order / bytecode
- Layer と Pass の execution order
- pass blend override

## 7. ローカル確認済み

この環境では Windows/MSVC/D3D11 実描画は実行できないため、最終 Acceptance は Windows 実機で行う。

実施済み:

- C++17 `-Wall -Wextra -Wpedantic` target syntax checks: PASS
- Material v4 Save/Load runnable test: PASS
- persistent Layer ID Save/Reload + next ID test: PASS
- Material v3 -> v4 migration runnable test: PASS
- replay_pass parser + ShaderExecutionPlan runnable test: PASS
- vcxproj / filters XML parse: PASS
- new source registration: PASS
- Layer HLSL は `FxCompile` ではなく runtime shader asset (`None`) として登録
- built-in surface HLSL 5 枚: UTF-8 BOM なし

## 8. Windows Acceptance

最優先で、直前に落ちた 3 件が 0 へ戻ることを確認する。

- SHADER_BUILTIN: 1224 -> 0
- SHADER_RENDER: 1400 -> 0
- SHADER_TEXTURE: 1450 -> 0

その後、新しい Layer / Pass validation と既存 regression を全て 0 にする。

Editor 手動確認:

- Material > Shader Stack > Add Layer に built-in Layer が動的に出る
- Project Browser > Create > Layer Shader が使える
- 作った Layer Shader が Add Layer に即時出る
- custom Layer の property / texture を編集できる
- Layer を複数追加、並び替え、ON/OFF できる
- 同じ Layer Shader を複数枚追加できる
- Save -> engine restart -> GUID / ID / order / enabled / Property が復元される
- Outline Layer の Color / Width が個別に効く
- 既存 PBR / Toon / Pixelate 等の見た目が変わらない

## 9. Acceptance 後の次工程

このパッチが Windows で全 PASS したら、Shader Composer / Mini Shader Graph の前提となる
「Shader Asset -> Schema -> Material / Layer -> Runtime」の経路は揃う。

次は Shader Composer のデータモデル / Node IR / HLSL generator を設計・実装する。
`#include` dependency tracking、render-state pragma、disk bytecode cache 等は Composer と独立して追加できる。
