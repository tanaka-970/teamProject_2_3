# C# Scripting 実機Validation レポート

状態: **Debug 全自動検査 PASS / Release・VS手動確認 未完了**
このレポートは完了報告ではありません。

最終更新: 2026-08-03 / Windows 実機実行後

---

## 0-A. 再確認結果（2026-08-03 22:08 / CSharpProject.cpp 修正後）

`.sln` 上書き修正を入れたうえで `Tools\RUN_ME.bat` を全通し。

```
CSHARP_DEBUG_BUILD=0
CSHARP_SCRIPTING=0
script-core=0            animation-undo=0      runtime-scene=0
script-lifecycle=0       camera-component=0    scene-flow=0
script-serialization=0   player-speed=0        runtime-api=0
                                               behaviour=0
CSHARP_RELEASE_BUILD=0
CSHARP_RELEASE_VALIDATION=0
ALL AUTOMATED CHECKS PASSED
```

exe: Debug 22:01 / Release 22:05 いずれも再生成済み。**回帰なし。**

### 修正が効いていることの実証

| 確認 | 結果 |
|---|---|
| `Scripts/RePlayScripts.sln` の mtime | **21:52 のまま**（22:01-22:08 の実行で触られていない） |
| `.sln` 内のプロジェクト数 | 2（`RePlayEngine.Managed` / `RePlayGameScripts` 両方健在） |
| `.slnx` の生成 | なし |

修正前は `EnsureProjectFiles` が呼ばれるたびに `.sln` が書き換わっていました。
今回の実行では validation が `EnsureProjectFiles` を通っているにもかかわらず
mtime が動いていないため、**再生成しない経路に入ったことが確定**しています。

**残るは手順4（Visual Studio 手動確認）のみ。**

---

## 0. 初回実機実行結果（2026-08-03 21:xx / 修正前）

環境: Visual Studio 18 Community (`C:\Program Files\Microsoft Visual Studio\18\Community`)
MSBuild 18.8.2.30814

| 手順 | 結果 | 終了コード |
|---|---|---|
| 1. Debug x64 full MSBuild (`/t:Rebuild`) | **PASS** | `CSHARP_DEBUG_BUILD=0` |
| 2. `--validate-csharp-scripting` (Debug) | **PASS** | `CSHARP_SCRIPTING=0` |
| 3. 既存回帰10本 (Debug) | **全PASS** | 全て `0` |
| 4. Visual Studio 手動確認 | **未実施** | — |
| 5. Release x64 build | **PASS**（2回目） | `CSHARP_RELEASE_BUILD=0` |
| 5. Release `--validate-csharp-scripting` | **PASS** | `CSHARP_RELEASE_VALIDATION=0` |

**自動検査は全て PASS。残るは手順4（Visual Studio 手動確認）のみ。**

Release exe: `x64/Release/3dgp.exe` 2026-08-03 21:52 再生成済み

### 手順3の内訳（全て終了コード 0）

```
script-core=0            animation-undo=0      runtime-scene=0
script-lifecycle=0       camera-component=0    scene-flow=0
script-serialization=0   player-speed=0        runtime-api=0
                                               behaviour=0
```

### 手順5の1回目の失敗内容 — コードのエラーではなかった

2回目（`Tools\RUN_RELEASE.bat`、10秒待機を挟んで実行）で PASS したため、
一過性のファイルロックだったと確定しました。以下は記録として残します。

```
DirectXTK_Desktop_2026.vcxproj(452,5): error MSB3061:
  ファイル "...\DirectXTK-main\src\Shaders\Compiled\
            EnvironmentMapEffect_PSEnvMapDualParabolaPixelLighting.inc"
  を削除できません。別のプロセスで使用されているため
DirectXTK_Desktop_2026.vcxproj(452,5): error MSB3061:
  ...EnvironmentMapEffect_PSEnvMapPixelLighting.inc  （同上）
```

コンパイルエラーでもリンクエラーでもなく、`Rebuild` の Clean 段階での
**ファイルロック**です。原因の候補:

- DirectXTK は `.inc` を **構成非依存の共有フォルダ** `Src/Shaders/Compiled` に出力する。
  直前の Debug ビルドが数百個の `.inc` を書いた直後に Release の Clean が
  同じファイルを消しに行っている。
- 書き込み直後のファイルは Windows Defender 等がスキャンハンドルを保持しやすい。
  失敗したのが 2 ファイルだけ、という分布はこれと整合する。

**リトライで隠さず**、`Tools\RUN_RELEASE.bat` で
プロセス確認 → 待機 → Release のみ再実行して切り分けます。
同じ 2 ファイルで再発する場合はスキャン以外の常駐ハンドルを疑います。

### 手順2の実証ログ

`Saved/Validation/CSharp/csharp_validation.log`

```
Intentional failed build succeeded=false exit=1
  RePlayEngine.Managed -> ...\Managed\RePlayEngine.Managed\bin\Debug\net8.0\
                          RePlayEngine.Managed.dll
...\Scripts\ValidationBrokenBehaviour.cs(3,80): error CS1002: ; が必要です
ビルドに失敗しました。
```

ここから確定できること:

- **Managed API Build** — `RePlayEngine.Managed.dll` が実際に生成されている
- **Compile Failure** — 意図的な構文エラーが検出され `exit=1` が返っている
- **エラーにファイル名／行番号が出る** — `ValidationBrokenBehaviour.cs(3,80)`

---

## 1. ビルド成果物の鮮度

実行前は exe が古い状態でした（下記）。手順1の Rebuild により解消済みです。

```
x64/Debug/3dgp.exe                       Aug 3 20:47
RePlayEngine/.../CSharpScriptBackend.cpp Aug 3 20:59   ← exeより新しかった
RePlayEngine/.../CSharpScriptValidation.cpp Aug 3 21:00 ← exeより新しかった
```

vcxproj への登録は確認済み（3dgp.vcxproj:316-318, 506-508）。
`CSharpProject.cpp` / `CSharpScriptBackend.cpp` / `CSharpScriptValidation.cpp`
は ClCompile に入っており、full build 対象から漏れていません。
実際のビルドログにも 3 ファイルとも現れています。

---

## 2. Validationコマンド名 — 全て実在（推測なし）

`Source/app/Runtime/main.cpp` で登録を実確認しました。

| コマンド | 行 |
|---|---|
| `--validate-csharp-scripting` | 989 |
| `--validate-script-core` | 974 |
| `--validate-script-lifecycle` | 979 |
| `--validate-script-serialization` | 984 |
| `--validate-animation-undo` | 960 |
| `--validate-camera-component` | 648 |
| `--validate-player-speed` | 774 |
| `--validate-runtime-scene` | 912 |
| `--validate-scene-flow` | 927 |
| `--validate-runtime-api` | 896 |
| `--validate-behaviour` | 884 |

指示にあった11本すべて一致。名前の修正は不要です。

---

## 3. Validationが実際に検査している項目 / していない項目

`CSharpScriptValidation.cpp` の `check.Expect(...)` を全件読みました。

### 実際に assert されている（16項目）

| チェックリスト項目 | 根拠 |
|---|---|
| Game Scripts csproj Build | "C# compile succeeds" / "compiled C# Assembly exists" |
| C# Behaviour生成 | "validation C# Behaviour source is written" / "C# Behaviour metadata can be read" |
| Compile Success | "C# compile succeeds" |
| Compile Failure | "C# compile failure is reported" |
| 失敗時の旧Assembly維持 | "old Assembly remains loaded after compile failure" / "old TypeState remains usable after compile failure" |
| Assembly Reload | "C# compile succeeds again after fixing errors" |
| 100回Reload | "Assembly reload succeeds 100 times" (403行) |
| Serialized Field保持 | "restored Scene keeps float field value" |
| ObjectReference保持 | "restored Scene keeps ObjectReference value" |
| ComponentReference保持 | "restored Scene keeps ComponentReference value" |
| Scene保存／再読込 | "Scene with C# Behaviour saves to disk" / "reloads from disk" |
| Prefab保存／Instantiate | "Prefab with C# Behaviour saves" / "Prefab Instantiate restores C# ScriptComponent" |
| Missing C# Behaviour保持 | "Missing C# Behaviour keeps Type GUID" / "keeps serialized field data" |
| Play開始／停止 | "Play start creates managed instance..." / "Play stop destroys managed instances" |
| Managed Instance破棄 | "managed instance is released after DestroyInstance" / LiveInstanceCount()==0 |
| Type GUID安定性 | "class rename does not regenerate Type GUID" 他2箇所 |

### assert されていない — 完了扱いにできない（5項目）

| 項目 | 実態 |
|---|---|
| **Shadow Copy Assembly Load** | `ShadowCopyAssembly()` は実装済み（CSharpScriptBackend.cpp:996, 呼出1070）だが、**Validation内に "shadow" の文字列が1つも無い**。ロード中に元DLLが差し替え可能かを確認する assert が無い。手順4の「DLLロックエラーが出ない」を裏付ける自動検査は存在しません。 |
| **Event購読／解除** | 生成される Behaviour ソースに `SubscribeEvent` / `PollEvent` があり `LastEventType` に書き込むが、**`LastEventType` を読み返す Expect がどこにも無い**。`Unsubscribe` は生成ソースに存在しない。購読も解除も未検証。 |
| **Scene切替** | `OnWorldActivating` / `OnWorldUnloading` は1つの world に対してのみ（522-529行）。world A → world B の切替は行っていない。 |
| **ServiceUnavailable** | managed 側 `Update()` で `Counter = -100..-103` を代入して判定するが、**`Counter` を読み返す Expect が無い**。失敗しても無言で通ります。 |
| **Managed API Build** | 独立した assert が無い。`Initialize()` 成功に間接的に含まれるだけ。 |
| ObjectHandle保持 / ComponentHandle保持 | Push/Pull（"can be pushed and pulled"）はあるが、Reload をまたいだ Handle 安定性の assert は無い。 |

→ **Validationが緑になっても、上記は「確認済み」として報告できません。**
実装済みだが未検査、という扱いが正確です。

---

## 4. Visual Studio 連携で先に潰すべき実バグ 1件

`CSharpProject.cpp:429`

```cpp
RunDotnet(L"new sln --force --format sln -n RePlayScripts -o " + Quote(scripts), {});
RunDotnet(L"sln " + solution + L" add " + project, {});
```

**問題A: `--force` が既存 .sln を毎回上書きします。**
リポジトリの `Scripts/RePlayScripts.sln` には **2プロジェクト** 入っています。

```
RePlayGameScripts        (Scripts/RePlayGameScripts.csproj)
RePlayEngine.Managed     (../Managed/RePlayEngine.Managed/...)
```

しかし再生成後に `add` されるのは `RePlayGameScripts` だけです。
→ **EnsureProjectFiles を1回呼んだ時点で RePlayEngine.Managed が .sln から消えます。**

コンパイル自体は `RePlayGameScripts.csproj` の `ProjectReference` で通るので
IntelliSense は動くと思われますが、Solution Explorer から Managed API が
消える／VS が開いている最中に .sln を外部上書きして再読込ダイアログが出る、
という形で手順4の確認が濁ります。

**問題B: 戻り値を捨てています。**
`RunDotnet` の結果を両方とも無視しているため、`dotnet` が失敗しても
`EnsureProjectFiles` は `true` を返します。
`.sln` は既にリポジトリに存在するので、Validation の
`exists(GameScriptsSolutionPath(root))` チェック（CSharpScriptValidation.cpp:234）は
**dotnet が完全に失敗していても通ります。**

**問題C: `--format sln` は新しめの .NET SDK 限定オプションです。**
`global.json` が無く SDK は固定されていません。TargetFramework は net8.0 です。
実機の SDK が `--format` を解さない場合、問題B により無言で失敗します。

**.slnx について**: ソース全体を検索し `slnx` の文字列は **0件** でした。
`--format sln` の明示もあり、.slnx 再生成の懸念自体は当たっていません。
危険なのは `--force` による **.sln の方** です。

### 提案（手順4の前に入れる最小修正）

```cpp
// 既に .sln があるなら再生成しない
std::error_code sln_error;
if (!std::filesystem::exists(GameScriptsSolutionPath(root), sln_error))
{
    const CSharpBuildResult created =
        RunDotnet(L"new sln --format sln -n RePlayScripts -o " + Quote(scripts), {});
    if (!created.succeeded) { error = created.output; return false; }
    // Managed API も忘れず追加する
    RunDotnet(L"sln " + solution + L" add " + Quote(ManagedApiProjectPath(root)), {});
    RunDotnet(L"sln " + solution + L" add " + project, {});
}
```

これは「追加実装」ではなく、手順4を成立させるための前提修正です。
判断は大地さんにお任せします。

---

## 5. CSharpScriptBackend.cpp 構造レビュー（分割は実施しません）

全1,341行の内訳：

| 範囲 | 行数 | 内容 |
|---|---|---|
| 28-751 | 約720行 (54%) | 無名namespace: Native Interop 一式 |
| 763-1341 | 約578行 (43%) | `CSharpScriptBackend` クラス実装 |

### 責務の集中状況

指示にあった8責務のうち **7つがこのファイルに同居** しています。

| 責務 | 所在 |
|---|---|
| CoreCLR / hostfxr Host | `LoadHost()` 852, `ResolveManagedEntryPoints()` 937 |
| Assembly Loader | `LoadGameAssembly()` 1063, `UnloadGameAssembly()` 1100 |
| Shadow Copy | `ShadowCopyAssembly()` 996 |
| Script Instance管理 | `type_states_` / `instance_types_` + CreateInstance/DestroyInstance |
| Native Interop | 無名namespace 全体（`NativeApiTable`, `Native*` 約40関数） |
| Compile / Project生成 | `CompileAndReload()` 1121 → **CSharpProject.cpp に分離済み（良い）** |
| File Watch | このファイルには無し |
| Reload | `CompileAndReload()` 1121, `ReloadLastBuiltAssembly()` 1143 |
| Diagnostics | `RefreshLastError()` 1319, `SetLastError()` 1334 |

追加の懸念: 無名namespace にプロセス全体で共有される可変グローバルが2つあります。

```cpp
RuntimeContext* g_runtime_context = nullptr;                              // :36
std::unordered_map<uint64_t, NativeEventSubscription> g_event_subscriptions; // :126
```

Native コールバックが static 関数である以上ほぼ避けられませんが、
バックエンド1インスタンス前提が暗黙になっています。

### 分割案（**動作確定後**に実施）

| 新ファイル | 移す範囲 | 概算 |
|---|---|---|
| `NativeBridgeBindings.cpp/h` | 無名namespace 28-751 全部 | ~720行 |
| `CSharpHost.cpp/h` | `LoadHost` / `LoadManagedApi` / `ResolveManagedEntryPoints` / `SetNativeApi` | ~145行 |
| `ShadowCopyManager.cpp/h` | `ShadowCopyAssembly` | ~67行 |
| `ManagedAssemblyLoader.cpp/h` | `LoadGameAssembly` / `UnloadGameAssembly` | ~58行 |
| `ScriptReloadCoordinator.cpp/h` | `CompileAndReload` / `ReloadLastBuiltAssembly` | ~40行 |
| `ManagedInstanceRegistry.cpp/h` | `type_states_` / `instance_types_` / Create / Destroy / Set / Get Field | ~120行 |
| `ManagedDiagnostics.cpp/h` | `RefreshLastError` / `SetLastError` + `last_error_*` | ~30行 |
| 残 `CSharpScriptBackend.cpp` | `IScriptBackend` 実装の薄い委譲のみ | ~150行 |

`CSharpCompiler` は既に `CSharpProject` が担っているので新設不要です。

分割時に壊してはいけない不変条件：

1. Type GUID — `[ReplayGuid]` 由来。`type_states_` のキーとして
   Reload をまたいで同一であること。
2. Serialized Field — `SetField` / `GetField` の `saved_name` 文字列。
   `"field.Speed"` 形式を変えないこと。
3. Missing Script — バックエンド不在時に `ScriptComponent` が
   `type_id` と `field.*` を保持し続けること（Validation 485-520行の経路）。
4. 旧Assembly維持 — コンパイル失敗時に `UnloadGameAssembly` を呼ばないこと。
   現状 `CompileAndReload()` は build 失敗時に early return しています。この形を保つこと。

順序としては `NativeBridgeBindings` の切り出しだけ先に行うのが安全です
（ファイルの54%が減り、クラス側は無傷）。

---

## 5-B. 手順4 Visual Studio 手動確認 チェックリスト

自動検査は全て緑。ここから先は人の目でしか確認できません。

### 事前

`.sln` が再生成されないことを mtime で確認できるよう、開始前に控えておきます。

```
dir Scripts\RePlayScripts.sln
```

### 手順

| # | 確認 | 見るもの | 結果 |
|---|---|---|---|
| 1 | Project Browser から C# Behaviour を1つ作成 | `.cs` が `Scripts/` に出る | ☐ |
| 2 | `Scripts/RePlayScripts.sln` が存在する | — | ☐ |
| 3 | **`.sln` の mtime が変わっていない** | 事前に控えた値と比較 | ☐ |
| 4 | `.slnx` が生成されていない | `dir Scripts\*.slnx` が空 | ☐ |
| 5 | Visual Studio で `.sln` を開ける | 外部変更の再読込ダイアログが出ないこと | ☐ |
| 6 | Solution Explorer に2プロジェクト見える | `RePlayGameScripts` / `RePlayEngine.Managed` | ☐ |
| 7 | Managed API 参照が解決される | 参照に黄色い警告が付かない | ☐ |
| 8 | IntelliSense が動く | 波線が誤爆しない | ☐ |
| 9 | `ScriptBehaviour` を補完できる | `: Script` で候補が出る | ☐ |
| 10 | Runtime API を補完できる | `Runtime.` で候補が出る | ☐ |
| 11 | 保存を Editor が検出する | 保存後に Editor 側が反応 | ☐ |
| 12 | Compile 成功後に動作が変わる | 値を変えて Play で反映 | ☐ |
| 13 | Syntax Error を入れても旧 Assembly が動き続ける | Play 中の挙動が止まらない | ☐ |
| 14 | エラーにファイル名／行番号が出る | `Xxx.cs(12,5)` 形式 | ☐ |
| 15 | エラー選択で VS の該当行が開く | — | ☐ |
| 16 | 修正後に再 Build できる | — | ☐ |
| 17 | DLL ロックエラーが出ない | `.dll` が使用中で書けない、が起きない | ☐ |

### 注意

- **17 が本番の Shadow Copy 検査です。** Validation 側に shadow copy の
  assert が無いため、ここが唯一の実証機会になります。
- 13 と 14 は Debug 実行時に `Saved/Validation/CSharp/csharp_validation.log`
  と同じ形式の診断が出るかで裏が取れます。
- ビルド中に VS を開いていると DirectXTK のシェーダ `.inc` を掴んで
  MSB3061 を誘発します。手順4は**ビルドが終わってから**行ってください。

---

## 6. 次にやること

1. **x64 Native Tools Command Prompt for VS** を開く
2. `Tools\verify_csharp_scripting.bat` を実行
   （手順1・2・3・5をまとめて実行し、失敗したものを最後に列挙します）
3. Debug build が落ちたら `Saved\Build\csharp_debug.errors.txt` の
   最初の根本エラーを共有してください
4. 自動分が全部緑になってから、手順4の Visual Studio 手動確認

上記4が終わるまで「Visual Studio編集対応完了」も
「C# Scripting 完了」も報告しません。
