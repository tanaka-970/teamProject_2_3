# Persona Battle 実装報告

作成日: 2026-08-19  
対象: `Docs/PERSONA_BATTLE_SPEC.md`

## 結論

仕様を読み、エンジン側を変更せずに実装を開始した。

ただし、仕様 8-4 が必須としている **GameObject の名前検索 API が C# へ公開されていない** ことを確認した。

現在の `Managed/RePlayEngine.Managed/ScriptRuntimeContext.cs` にある検索 API は次だけ。

```csharp
public RuntimeResult<ObjectHandle> FindGameObject(ulong objectId)
```

`FindGameObject(string name)` / `FindGameObjectByName(string name)` 相当は存在しない。

C++ 側には `HandleResolver::FindByName(const std::string&)` が存在するが、
`RuntimeContext` → `NativeApiTable` → `NativeBridge.cs` → `ScriptRuntimeContext.cs`
の公開経路には載っていない。

仕様 10 章は「足りない API があれば回避せず報告」と明記しているため、
Object ID の直書き、ObjectReference の代用、ID 総当たり、Hierarchy 全走査などの
回避実装は行っていない。

このため、名前で `Player` / `Enemy_FireWeak` / `Enemy_IceWeak` /
UI オブジェクト / Effect オブジェクトを取得する必要がある
`PersonaBattleDirector`、戦闘 UI、Motion 配線、戦闘シーンの完成には進まず、
そこで停止した。

**ビルド未検証 / 実機未検証。**
指示どおり `dotnet build` / `cl` / `link` / MSBuild は実行していない。

---

## 章ごとの実装状況

### 0. これは何か / 何を作らないか

確認済み。

今回の追加コードには、ペルソナ切り替え、合体、コンペンディウム、
状態異常、バフ、デバフ、アイテム、逃走、経験値、報酬、フィールドエンカウントを入れていない。

### 1. 置き場所と命名

実装済み。

追加した C# はすべて `Scripts/Persona/` 配下。

- `Scripts/Persona/PersonaData.cs`
- `Scripts/Persona/PersonaEngineIds.cs`
- `Scripts/Persona/PersonaCombatant.cs`
- `Scripts/Persona/PersonaEnemyBrain.cs`

クラス名はすべて `Persona` で開始する。

### 2. 使える API

監査済み。

`GetScriptFieldBool/Int/Double/String`、
`SetScriptFieldBool/Int/Double/String`、
イベント payload、UI focus、Motion、Audio、UI 更新 API は確認できた。

`PersonaEngineIds.TypeId()` に FNV-1a 32bit を 1 か所だけ実装した。

一方、必須の名前検索だけが C# 公開 API に存在しなかった。

### 3. 画面と遷移

未実装。

`PersonaBattleDirector` が各画面のオブジェクトを名前で取得して進行する設計になるが、
その名前検索 API が存在しないため停止した。

### 4. データ

実装済み。

`PersonaData.cs` にまとめた。

属性は 3 種のみ。

- Physical
- Fire
- Ice

相性は 3 種のみ。

- Weak
- Normal
- Resist

スキルは 3 種のみ。

- たたかう: Physical / SP 0 / 小
- アギ: Fire / SP 4 / 中
- ブフ: Ice / SP 4 / 中

味方 1 体ぶんと敵 2 体ぶんの初期定義を持つ。

- `Player`
- `Enemy_FireWeak`
- `Enemy_IceWeak`

`Enemy_FireWeak` は炎弱点。
`Enemy_IceWeak` は氷弱点。

味方は炎弱点にしてあり、
`Enemy_IceWeak` がアギを持つため、
敵 AI がこちらの弱点を狙う縦切りを作れるデータになっている。

### 5. 戦闘の進行

未実装。

固定順、1 More、ダウン復帰、総攻撃、勝敗を一元管理する
`PersonaBattleDirector` が必要だが、
仕様どおり名前で戦闘参加者と UI を取得できないため停止した。

ID 直書きなどへ設計変更していない。

### 6. ダメージ計算

実装済み。

`PersonaData.CalculateDamage()` に 1 か所だけ置いた。

式:

```text
damage = power * attackPower * affinityMultiplier
```

相性倍率:

- Weak = 1.5
- Normal = 1.0
- Resist = 0.5

端数切り捨て、最低 1。

乱数は **入れていない**。
同じ入力なら同じ結果になる。

「ぼうぎょ」は今回の縦切りで意味を持たせるため、
被ダメージ 0.5 倍として計算できる引数を用意した。

### 7. 敵 AI

判断ロジック部分を実装済み。
ランタイム配線は未実装。

`PersonaEnemyBrain.Decide()` の優先順位:

1. アギを持ち、SP が足り、相手が炎弱点ならアギ
2. ブフを持ち、SP が足り、相手が氷弱点ならブフ
3. それ以外は通常攻撃

対象選択は縦切りでは味方 1 体固定のため、
Director 側で固定する予定だったが未配線。

### 8. UI

未実装。

`CanvasComponent` / `RectTransformComponent` / `UITextComponent` /
`UIImageComponent` / `UISelectableComponent` / `UIScrollViewComponent` の
Type ID は `PersonaEngineIds` に用意した。

ただし UI オブジェクトを仕様どおり名前で取得できないため、
UI と `resources/Scenes/PersonaBattle.replayscene` は作成していない。

### 9. 演出と UI の動き

未実装・未配線。

Motion / Audio API 自体は存在するが、
Director とシーンを作れないため再生箇所まで進めていない。

#### 必要になる Motion 一覧

依頼者が Motion 編集シーンで作る前提の候補。
現時点では C# の public field へは未配線。

| Motion 名候補 | 動かすもの | 長さ目安 |
|---|---|---:|
| `Persona_CommandIn` | Command panel の `anchored_position` / `scale` | 0.20〜0.25s |
| `Persona_SkillIn` | Skill panel の `anchored_position` / `scale` | 0.20〜0.25s |
| `Persona_TargetPulse` | Target 強調 + Vignette `intensity` | 0.7〜1.0s loop |
| `Persona_Attack` | 攻撃演出。中間に `PersonaHit` MotionEvent | 0.45〜0.60s |
| `Persona_Skill` | スキル演出。中間に `PersonaHit` MotionEvent | 0.55〜0.75s |
| `Persona_DamagePopup` | Damage text の `anchored_position` / `scale` | 0.40〜0.55s |
| `Persona_HpBar` | HP bar の `size_delta` | 0.25〜0.40s |
| `Persona_WeakHit` | ChromaticAberration + Glitch + RadialBlur `intensity` | 0.20〜0.30s |
| `Persona_Down` | 対象のダウン強調 | 0.25〜0.40s |
| `Persona_OneMore` | 1 More 表示の飛び出し | 0.30〜0.45s |
| `Persona_AllOutConfirmIn` | 総攻撃確認 UI | 0.20〜0.30s |
| `Persona_AllOutAttack` | Letterbox `progress` + RadialBlur。中間に hit event | 0.9〜1.2s |
| `Persona_Victory` | 勝利表示 | 0.4〜0.7s |
| `Persona_Defeat` | 敗北表示 | 0.4〜0.7s |

最低限必要な音は仕様どおり以下を予定していた。

- 攻撃
- 弱点
- ダウン

未配線のため public 音声フィールドはまだ追加していない。

### 10. エンジンを触らない

遵守。

元 ZIP と変更後ツリーをファイル内容で比較し、
次のディレクトリは全ファイル一致を確認した。

- `RePlayEngine/`: 592 files 一致
- `Source/`: 174 files 一致
- `Managed/`: 36 files 一致

`Scripts/RePlayGameScripts.csproj` も元 ZIP と完全一致。

---

## 作ったシーンのオブジェクト名一覧

シーン未作成のため該当なし。

理由は名前検索 API 不足であり、
壊れた / 動かないシーンを作って「完了」としないため。

予定していた必須名は仕様に合わせて以下。

- `Player`
- `Enemy_FireWeak`
- `Enemy_IceWeak`

UI / Effect の最終名は Director 実装時に一字一句固定する予定。

## Effect オブジェクト一覧

シーン未作成のため該当なし。

仕様 9 章に沿い、最終的には以下を積む想定。

- 常時: Halftone / CrossHatch / Posterize
- 弱点: ChromaticAberration / Glitch / RadialBlur
- 被弾: Shake
- 総攻撃: Letterbox / RadialBlur
- 切替: Wipe または Dissolve
- 対象選択: Vignette

## シーンの数合わせ

シーン未作成のため対象外。

- `OBJECT_COUNT`: N/A
- `COMPONENT_COUNT`: N/A
- `PROPERTY_COUNT`: N/A

---

## クラス一覧

| クラス | 役割 |
|---|---|
| `PersonaData` | 属性、相性、スキル、戦闘員初期値、ダメージ計算を 1 ファイルへ集約 |
| `PersonaEngineIds` | C++ component type 名から FNV-1a 32bit ID を作る唯一の場所 |
| `PersonaCombatant` | 各キャラが所有する HP / SP / 相性 / Down / Guard 等の公開状態 |
| `PersonaEnemyBrain` | 相手の弱点を突けるスキルを優先する最小 AI |

---

## スクリプト間で読み書きしているフィールド名

現在は Director 未実装のため、**ランタイムでの相互読み書きはまだ 0 件**。

`PersonaCombatant` では、Director から将来
`GetScriptField*` / `SetScriptField*` で扱うための公開フィールドを定義済み。

- `DisplayName`
- `IsEnemy`
- `MaxHp`
- `CurrentHp`
- `MaxSp`
- `CurrentSp`
- `AttackPower`
- `PhysicalAffinity`
- `FireAffinity`
- `IceAffinity`
- `HasAgi`
- `HasBufu`
- `Downed`
- `Guarding`
- `Alive`
- `LastDamage`
- `LastResult`

---

## 足りなかったエンジン API

### GameObject の名前検索

やりたかったこと:

```csharp
var player = Runtime.FindGameObject("Player");
var enemy1 = Runtime.FindGameObject("Enemy_FireWeak");
var enemy2 = Runtime.FindGameObject("Enemy_IceWeak");
```

現状:

```csharp
Runtime.FindGameObject(ulong objectId)
```

のみ。

C++ の `HandleResolver` には `FindByName()` があるので、
ゲーム固有機能を新設する必要はない。
ただし C# まで配線されていない。

必要になる最小の公開口は概念的には次。

```text
RuntimeContext: FindGameObjectByName(name, out ObjectHandle)
NativeApiTable: 末尾へ追加
NativeBridge.cs: 同じ末尾・同じ順序で追加
ScriptRuntimeContext.cs:
    RuntimeResult<ObjectHandle> FindGameObject(string name)
```

今回は `RePlayEngine/` / `Source/` / `Managed/` を変更してはいけないため、
**実装していない**。

---

## 11 章チェック結果

1. 追加 `.cs` の先頭 3 bytes `EF BB BF`  
   **OK: 4 / 4**

2. すべて `Scripts/Persona/` の下  
   **OK**

3. すべてのクラス名が `Persona` で始まる  
   **OK**

4. `[ReplayGuid]` が全クラスに付き、重複なし  
   **OK**  
   `Scripts/**/*.cs` 全体で 8 GUID を抽出し、重複 0。

5. `RePlayEngine/` `Source/` `Managed/` を変更していない  
   **OK**  
   元 ZIP と相対パス単位の SHA-256 比較で全ファイル一致。

6. `Scripts/RePlayGameScripts.csproj` を変更していない  
   **OK**

7. 掴めなかったオブジェクトを `LogError` で報告  
   **未到達**  
   名前検索 API 自体が無いため Director を作らず停止した。
   存在しない API を呼ぶコードや ID 回避実装は入れていない。

8. 範囲外機能を作っていない  
   **OK**

9. Scene count 一致  
   **N/A: Scene 未作成**

10. Scene object 名と `FindGameObject` 名の一致  
    **N/A: Scene / Director 未作成**

---

## BOM / 改行確認

追加 C# 4 ファイル:

- UTF-8 BOM: OK
- CRLF only: OK

---

## ビルド・実機

- **ビルド未検証**
- **実機未検証**
- `dotnet build`: 実行していない
- MSBuild: 実行していない
- `cl` / `link`: 実行していない
- `--profile-scene`: 実行していない

---

## 迷った箇所 / 判断を変えた箇所 / やり残し

### 名前検索を ID 参照へ置き換えなかった

`ObjectReference` や固定 Object ID を使えば見かけ上は先へ進められるが、
仕様 8-4 の「名前で探す」と仕様 10 の「足りない API は回避しない」に反する。

そのため採用しなかった。

### GameObject ID を総当たりして `GetName()` で比較しなかった

技術的には ID を推測・総当たりする回避が考えられるが、
Scene の ID 配置に依存し、仕様違反かつ壊れやすいので採用しなかった。

### やり残し

名前検索 API が C# まで配線された後に必要。

- `PersonaBattleDirector`
- コマンド選択
- スキル選択
- 対象選択
- 1 More
- ダウン復帰
- 総攻撃確認 / 総攻撃
- 勝利 / 敗北
- Runtime UI 配線
- UI focus 配線
- Motion public field と再生・待機
- MotionEvent の hit timing
- fallback hit timing
- 攻撃 / 弱点 / ダウン音
- `resources/Scenes/PersonaBattle.replayscene`
- Scene count 静的検証
- `--profile-scene` 実機検証（依頼者側）
