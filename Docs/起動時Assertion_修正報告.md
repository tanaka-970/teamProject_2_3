# 起動時 Debug Assertion 2 件 修正報告

作成日: 2026-07-31

| # | 発生箇所 | メッセージ |
|---|---|---|
| 1 | `Source\mesh\static_mesh.cpp:24` | `OBJ file not found.` |
| 2 | `MSVC\include\vector:96` | `can't decrement value-initialized vector iterator` |

---

## 調査結果：2 件は同一の原因から連続して起きていました

### 1 件目の根本原因

**`resources\cube.obj` が存在しません。**

`framework_initialize.cpp` の LoadingScene タスクが、無条件にこれを読み込んでいました。

```cpp
loading_scene->AddTask("Debug mesh", [this]
{
    static_meshes[0] = std::make_unique<static_mesh>(
        device.Get(), L".\\resources\\cube.obj", true);   // ← ファイルが無い
    return static_meshes[0] != nullptr;
});
```

`static_mesh` のコンストラクタは開けなかった時点で `_ASSERT_EXPR(fin, L"'OBJ file not found.")` を出します。

**この不足は今回の GameObject 基盤とは無関係の、元からあるものです。**
`git log --all -- resources/cube.obj` が空で、**一度もコミットされていません**。
調べたところ、初期化が要求するファイルのうち **5 つが欠けています**。

| ファイル | 状態 | 影響 |
|---|---|---|
| `resources\cube.obj` | **欠損** | 1 件目の assert |
| `resources\screenshot.jpg` | **欠損** | `sprite_batch` → `texture.cpp` の assert 要因 |
| `resources\ibl\diffuse_iem.dds` | **欠損** | `pbr_renderer` が自前で握って続行（assert なし） |
| `resources\ibl\specular_pmrem.dds` | **欠損** | 同上 |
| `resources\ibl\lut_ggx.dds` | **欠損** | 同上 |
| `resources\AnimationModel\AllAnimation1.cereal` | 存在する | プレイヤーモデルは読める |

### 2 件目の根本原因

**1 件目を「無視」した直後に、空の `vector` へ逆順イテレータを張ったためです。**

```cpp
// static_mesh.cpp
fin.close();

std::vector<subset>::reverse_iterator iterator = subsets.rbegin();
iterator->index_count = ...;   // ← subsets が空
```

OBJ を 1 行も読めていないので `subsets` は空です。空 `vector` の `rbegin()` は
`rend()` と同じ位置を指し、`reverse_iterator::operator->` は内部で `--current` を行うため、
MSVC のデバッグイテレータが `can't decrement value-initialized vector iterator` で停止します。

その直後の `mtl_filenames[0]` も空 `vector` への添字アクセスで、同様に停止します。

つまり**ご推察のとおり、2 件目は 1 件目の後始末が不完全だったことによる二次障害**でした。

### ご指定の調査項目への回答

| # | 確認項目 | 結果 |
|---|---|---|
| 1 | static_mesh へ渡された実際のパス | `.\resources\cube.obj`。**ログ出力を追加**しました |
| 2 | MeshRendererComponent の mesh_asset が空でないか | **今回の件とは無関係**。`static_mesh` を作っているのは初期化の 1 箇所だけで、GameObject 基盤は一切関与していません |
| 3 | GUID から実ファイルパスへの解決失敗 | 同上。ただし将来の事故防止として検証を追加しました |
| 4 | 解決されたパスが存在するか | `cube.obj` は**存在しません** |
| 5 | 相対パスの基準ディレクトリ | `3dgp.vcxproj.user` に `LocalDebuggerWorkingDirectory` が無く、既定の `$(ProjectDir)` が使われます。基準は正しく、**ファイル自体がありません** |
| 6 | OBJ 以外を static_mesh へ渡していないか | 渡していません。ただし**拡張子チェックを追加**しました |
| 7 | 古い Scene / EditorSession に無効な GUID が残っていないか | 現状の `.replayscene` は空で、無効 GUID はありません。将来のために検証を追加しました |
| 8 | 失敗時にキャッシュへ無効エントリを追加していないか | **していました。修正済み**（下記） |
| 9 | 失敗後も RenderItem を提出していないか | 提出していません。念のため描画側にも空 GUID の早期スキップを追加 |
| 10 | vector の end()/begin()/未初期化 iterator をデクリメントしていないか | **`static_mesh.cpp` に 3 系統ありました。全て修正済み** |

---

## 修正内容

### A. `static_mesh` — 事前検証と失敗の返却（assert を単に消していません）

`Source/mesh/static_mesh.h`

```cpp
// 読み込み前にパスを検証する。呼び出し側はこれで弾いてから構築すること。
static bool can_load(const wchar_t* obj_filename, std::wstring* out_reason = nullptr);

// 構築が成功したか。false の場合、描画してはいけない。
bool is_loaded() const noexcept;
const std::wstring& load_error() const noexcept;
```

`can_load` が確認するもの:

1. パスが空でないか
2. **拡張子が `.obj` か**（FBX / glTF を渡されても解釈できないため事前に弾く）
3. ファイルが存在するか
4. ディレクトリでないか

`Source/mesh/static_mesh.cpp` の変更:

| 箇所 | 修正 |
|---|---|
| コンストラクタ冒頭 | `can_load` で検証し、失敗なら**理由をログへ出して即 return**。空 vector を触る箇所へ到達しない |
| `std::wifstream fin` の直後 | `can_load` を通ったのに開けない場合は**本当に想定外**なので assert を残す。ただし従来と違い、その後は必ず return する |
| `subsets.rbegin()` | **`if (!subsets.empty())` で囲んだ**（2 件目の assert の直接の場所） |
| 頂点数チェック | 頂点が 0 なら**バッファ作成前に return**（0 バイトの頂点バッファ作成で別の assert が出るのを防ぐ） |
| `mtl_filenames[0]` | `if (!mtl_filenames.empty())` で囲み、**開けたときだけ** MTL 解析ループへ入る |
| MTL 解析ループ | `newmtl` より前に `map_Kd` / `Ka` / `Kd` / `Ks` が来る壊れた `.mtl` でも `materials.rbegin()->` を実行しないようガード |
| 末尾 | 最後まで到達したときだけ `loaded_ = true` |

**既存の assert は残しています。** 消したのではなく、「呼び出し前に検証して到達しない」構造にしたうえで、
本当に異常な場合（ファイルは存在するのに開けない）だけ発火するようにしました。

### B. 読み込み側 — 欠損アセットで起動を止めない

`Source/app/Runtime/framework_initialize.cpp`

```cpp
loading_scene->AddTask("Debug mesh", [this]
{
    const wchar_t* debug_mesh = L".\\resources\\cube.obj";
    std::wstring reason;
    if (!static_mesh::can_load(debug_mesh, &reason))
    {
        OutputDebugStringW((L"[Assets] デバッグ用メッシュを読み込めません: " +
            reason + L" （静的メッシュ表示は無効のまま続行します）\n").c_str());
        static_meshes[0].reset();
        enable_static_meshes = false;
        return true;      // 任意アセットなので起動は続行する
    }
    auto candidate = std::make_unique<static_mesh>(device.Get(), debug_mesh, true);
    if (!candidate->is_loaded()) { /* ログを出してスキップ */ }
    else static_meshes[0] = std::move(candidate);
    return true;
});
```

同じ理由で `screenshot.jpg`（UI 画像）も存在確認を挟みました。こちらは
`texture.cpp` の `_ASSERT_EXPR(SUCCEEDED(hr), ...)` を踏む経路だったため、
放置すると次に同じ形で止まっていたはずです。あわせて
`framework_render.cpp` の `sprite_batches[0]->begin(...)` に **null ガード**を追加しました。

IBL の DDS 3 つは `pbr_renderer::load_ibl` が既に失敗を握ってログを出す作りだったので、そのままです。

### C. GameObject 基盤側のアセット解決を堅牢化

今回の assert には関与していませんが、ご指定の観点で `resolve_object_mesh` を見直しました。

| 段階 | 修正前 | 修正後 |
|---|---|---|
| Asset 未指定 | 早期 return | 同じ（正常な状態なので警告も出さない） |
| GUID 未解決 | **キャッシュへ `nullptr` を登録** | **失敗集合へ記録**し、理由をログと Editor ステータスへ出す |
| 拡張子 | チェックなし | **`.fbx` / `.cereal` 以外は事前に弾く**（`.obj` や `.glb` を skinned_mesh へ渡さない） |
| ファイル存在 | `.cereal` の存在確認あり | 同じ。失敗理由を具体的なパス付きでログへ |
| 読み込み例外 | キャッシュへ `nullptr` | 失敗集合へ記録 + ログ |
| キャッシュの中身 | null が混ざる | **有効なメッシュのみ**。無効エントリは一切入らない |

失敗を専用の `object_mesh_failures` に持たせたことで、次の 3 つを同時に満たしています。

- キャッシュには常に有効なメッシュしか入らない
- 毎フレーム同じ Asset を探し直さない
- 同じ警告をログへ出し続けない

描画側（`draw_object_scene_meshes` と Forward ループ）にも、
空 GUID の早期スキップと null チェックを入れてあります。

---

## 確認結果

### g++ で実行して確認できた範囲

新規に「欠損アセット」専用の検証を書き、**ASan + UBSan + LeakSanitizer 付きで実行**しました。

| 確認項目 | 結果 |
|---|---|
| 空 Scene でも更新・収集が成立する | OK |
| Asset 未指定の MeshRenderer があっても破綻しない | OK |
| Asset 未指定の GameObject は提出リストへ入らない | OK |
| 存在しない GUID を持つ Scene を保存・再読み込みできる | OK |
| 欠損 GUID が文字列としてそのまま保持される | OK |
| MeshRenderer 無し / Asset 未指定が混在しても収集が安定 | OK |
| **300 フレーム回しても結果が安定**（失敗ループの模擬） | OK |
| メモリリーク・未定義動作 | **ゼロ** |

既存の受け入れテストも再実行し、**49 項目すべて合格**しています。
エンジン側 `.cpp` の構文検証も `-Wall -Wextra -Wpedantic` でエラー・警告ゼロです。

### Visual Studio Debug x64 で未確認

**実行確認はできていません。** 作業環境に MSVC / Windows SDK がないためです。
特に以下は **`static_mesh.cpp` と `framework_*.cpp` が g++ でコンパイルできない**
（`windows.h` / `wrl.h` / `d3d11.h` に依存）ため、静的なレビューのみです。

- `static_mesh::can_load` の実際の動作
- 修正後の起動シーケンス
- `OutputDebugStringW` の出力内容

**お手数ですが Debug x64 で実行して、以下を確認してください。**

| # | 期待する結果 |
|---|---|
| 1 | assert が 1 件も出ずに起動する |
| 2 | 出力ウィンドウに `[Assets] デバッグ用メッシュを読み込めません: ファイルが見つかりません: .\resources\cube.obj` が出る |
| 3 | 同様に背景画像の欠損メッセージが出る |
| 4 | F1 で Editor が開く |
| 5 | 階層の「GameObject」から GameObject を作成できる |
| 6 | Mesh Renderer を Asset 未指定のまま追加してもクラッシュしない |
| 7 | Ctrl+S で保存でき、再起動後に復元される |
| 8 | 終了時に assert が出ない |

---

## 欠損アセットについて

**`cube.obj` / `screenshot.jpg` / IBL の DDS 3 つは、リポジトリに一度も入っていません。**
今回の修正で「無くても起動する」ようにしましたが、本来の見た目には必要なはずです。

- `cube.obj` … デバッグ用の立方体。`enable_static_meshes` が既定 false なので、無くても実害は小さい
- `screenshot.jpg` … 背景画像。`draw_background_image` が既定 false なので同様
- **IBL の DDS 3 つ** … PBR の環境光。**これが無いと PBR の見た目が本来と変わります**

チームの誰かのローカルにあるなら、`.gitattributes` / `.gitignore` を確認のうえコミットしておくと、
別のマシンで同じ問題が起きなくなります。判断が必要な箇所なのでこちらでは触っていません。

---

## 変更ファイル一覧

| ファイル | 変更内容 |
|---|---|
| `Source/mesh/static_mesh.h` | `can_load` / `is_loaded` / `load_error` を追加。`loaded_` / `load_error_` メンバ追加 |
| `Source/mesh/static_mesh.cpp` | `can_load` 実装。空 vector への `rbegin()` / `[0]` を 3 系統ガード。頂点 0 の早期 return。`<cwctype>` 追加 |
| `Source/app/Runtime/framework_initialize.cpp` | Debug mesh と UI image を「無ければスキップして続行」へ。`<filesystem>` 追加 |
| `Source/app/Rendering/framework_render.cpp` | `sprite_batches[0]` の null ガード。提出リスト描画で空 GUID を早期スキップ |
| `Source/app/Runtime/framework_gameobject_scene.cpp` | `resolve_object_mesh` に拡張子・存在・解決の検証と失敗ログを追加。無効エントリをキャッシュしない。`<cctype>` 追加 |
| `Source/app/framework.h` | `object_mesh_failures` を追加。`<unordered_set>` 追加 |

新規ファイルはありません。`3dgp.vcxproj` / `.filters` の変更もありません。
