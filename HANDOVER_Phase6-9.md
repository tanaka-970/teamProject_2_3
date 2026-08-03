あなたは、C++17／Direct3D 11製の自作ゲームエンジン
「RePlayEngine」のRuntime Gameplay Foundation実装担当です。

この作業は途中から引き継ぎます。
Phase 1〜5は完了済みでMSVC検証も通っています。
Phase 6は途中まで実装されています。
Phase 6の残りからPhase 9までを完成させてください。

==================================================
0. 最初に必ず読むこと
==================================================

実装を1行も書く前に、次を実コードで確認してください。
過去の報告を信用せず、必ず自分で読んで確かめてください。

- git rev-parse --abbrev-ref HEAD
- git rev-parse HEAD
- git status
- git diff --stat
- 未追跡ファイル
- 削除ファイル

現在のBranchを勝手に切り替えないでください。
Branch／HEADが想定外に変化していた場合は作業を停止し、現在状態を報告してください。

【重要な前提】
- 名前空間は ReplayEngine です。RePlayEngine ではありません。
  ディレクトリ名だけが RePlayEngine/ です。混同しないでください。
- 未コミットの変更が大量にあります。これはPhase 1〜6の成果です。
  破棄しないでください。

==================================================
1. 技術上の前提
==================================================

- C++17（/std:c++17）
- Direct3D 11
- Windows x64
- Visual Studio 2026 / MSVC v145
- 3dgp.sln / 3dgp.vcxproj
- Dear ImGuiはEditor専用
- C++20機能は禁止
- C++20 designated initializerは禁止
- RuntimeからEditorへの依存は禁止
- 外部物理エンジンを導入しない
- C#、Lua、Node Editor用Libraryを導入しない
- Renderer／Collision基盤を全面的に書き換えない

ファイル規約（.editorconfigに従うこと）:
- *.{cpp,c,h,hpp} は UTF-8 BOM付き、CRLF、末尾改行あり
- 既存ファイルを編集する場合、そのファイルの元のBOM有無を変えないこと
- 文字コード・改行コードの一括変換は禁止

MSVC固有の注意（実際に踏んだもの）:
- 関数ローカルの constexpr をキャプチャ無しラムダから参照すると C3493 で落ちます。
  定数は名前空間スコープへ置いてください。
- 不完全型を指す unique_ptr をメンバに持つクラスは、
  デストラクタと既定コンストラクタを .cpp で定義してください。
  ヘッダで = default にすると、そのクラスを生成する全翻訳単位で落ちます。

==================================================
2. 完了済みの内容（Phase 1〜5）
==================================================

すべてMSVC Debug／Release x64でBuild成功し、
全Validationが終了コード0で通っています。

--------------------------------------------------
Phase 1 — Stable Identity
--------------------------------------------------
新規:
- RePlayEngine/Core/ObjectID/RuntimeIdentity.h/.cpp
  WorldInstanceID / ObjectGeneration / ComponentInstanceID / ComponentStableID
- RePlayEngine/Runtime/Core/RuntimeResult.h/.cpp
  RuntimeStatus（明示値つき enum : int32、16種）、RuntimeResult<T>
- RePlayEngine/Runtime/Handles/RuntimeHandles.h/.cpp
  ObjectHandle{world, object, generation}
  ComponentHandle{owner, instance, type_id}
  どちらもtrivially copyable / standard layoutをstatic_assertで保証
- RePlayEngine/Runtime/Handles/HandleResolver.h/.cpp
  MakeHandle / IsValid / TryResolve / TryResolveAs<T> / 親子 /
  FindByObjectID / GetControlledObjectHandle / FindComponentByStableID /
  HandleDiagnostics
- RePlayEngine/Runtime/Validation/HandleValidation.h/.cpp

改修:
- Component.h/.cpp … StableID / InstanceID
- GameObject.h/.cpp … Generation、StableID採番、AttachComponentWithStableID、
  FindComponentByStableID / FindComponentByInstanceID
- Scene.h/.cpp … WorldInstanceID、ObjectID別世代表、ComponentInstanceID採番

設計判断（変更しないこと）:
- World無効化は Scene::Clear() 1箇所へ集約。
  ApplySceneData は必ず Clear() を通るため、読み込み経路ごとの無効化が不要。
- ComponentStableID は GameObject単位。Scene全体で一意にしない。
  Prefab配置で付け替えが必要なのが所有ObjectIDだけになる。
- ComponentInstanceID は World内で再利用しないため、世代番号を兼ねる。
- 削除予約中は ObjectDestroyed / ComponentDestroyed として扱う。

--------------------------------------------------
Phase 2 — Serialization Foundation
--------------------------------------------------
新規:
- RePlayEngine/Reflection/Registry/TypeGUID.h/.cpp
  128bit型GUID。constexpr MakeTypeGUID("32桁hex")
- RePlayEngine/Reflection/Property/References.h
  ObjectReference / ComponentReference / AssetReference / SceneReference
- RePlayEngine/Object/Component/MissingComponent.h/.cpp
- RePlayEngine/Runtime/Validation/SerializationValidation.h/.cpp

改修:
- PropertyValue.h/.cpp … Int64/UInt64/AssetReference/SceneReference/
  ComponentReference/Array を追加。ValuesEqual を新設
- PropertyDesc.h … Category/Unit/RuntimeOnly/Advanced/OfAssetType/
  OfComponentType、std::vector<T>対応
- PropertyRegistry.cpp … 未知プロパティの預かりとRehydrate
- ComponentTypeInfo.h … type_guid / alias_guids / module_id / type_version /
  property_aliases
- ComponentRegistry.h/.cpp … Find(TypeGUID) / Resolve() / CreateWithStableID() /
  TypeGUIDConflicts()
- SceneData.h/.cpp … ComponentData拡張、参照Remap、MissingComponent生成、
  DuplicateGameObjectの全面書き直し
- SceneSerializer.cpp … v11形式の読み書き
- PrefabSerializer.cpp / InspectorPanel.cpp … 値比較を ValuesEqual へ集約
- PropertyDrawer.cpp … 新型の描画

Scene形式:
- SceneData::current_version = 11
- SceneData::minimum_supported_version = 7
- COMPONENT行はv10のまま。追加行 STABLE_ID / TYPE_GUID / TYPE_MODULE /
  TYPE_VERSION は version >= 11 のときだけ読み書きする
- 既存Sceneは開いただけでは変換されない。保存した時だけv11になる

設計判断（変更しないこと）:
- Missing Componentは「元の型として」書き戻す。
  ファイルにMissingの痕跡を残さないので、型が使えない環境で開いて保存しても
  内容が変わらず、型が戻れば自動復元される。
- 未解決参照の扱いを経路で分ける。
  Scene読み込み = 元の値を保持（Validationが検出できる）
  Prefab配置・複製 = 切る（同じIDの無関係なObjectを指さないため）
- AssetPath と AssetReference は相互変換しない。意味が違う。

--------------------------------------------------
Phase 3〜5 — Behaviour / Event / Runtime API
--------------------------------------------------
新規:
- RePlayEngine/Runtime/Behaviour/BehaviourEvents.h
  TriggerEvent / CollisionEvent / ContactPhase / CollisionHitKind
- RePlayEngine/Runtime/Behaviour/BehaviourComponent.h/.cpp
- RePlayEngine/Runtime/Behaviour/BehaviourRegistry.h/.cpp
  IBehaviourProvider / NativeBehaviourProvider / BehaviourRegistry
- RePlayEngine/Runtime/Events/EventBus.h/.cpp
  EventRecord / ScopedSubscription / EventScope / EngineEvents
- RePlayEngine/Runtime/Events/CollisionEventDispatcher.h/.cpp
- RePlayEngine/Runtime/API/RuntimeContext.h/.cpp
  IPrefabInstantiator / IRuntimeLogSink / RuntimeTime / RuntimeContext
- RePlayEngine/Runtime/Validation/BehaviourValidation.h/.cpp
- Source/game/Behaviours/ValidationBehaviours.h/.cpp
  RotatorBehaviour / TriggerCounterBehaviour / RegisterGameBehaviours()

改修:
- Component.h/.cpp … OnRuntimeAwake() / OnRuntimeDestroy() を追加
- GameObject.cpp … 破棄順を OnDisable -> OnRuntimeDestroy -> OnDetach へ
- SceneServices.h … Runtime() / SetRuntime()
- CharacterMotorComponent.h/.cpp … 接触記録の追加（移動計算は無改変）

設計判断（絶対に変更しないこと）:
- Behaviour専用のUpdateマネージャは作らない。
  Update / FixedUpdate / LateUpdate / Enable / Disable / Start は
  Core::Componentの仮想関数を派生Behaviourが直接overrideする。
  二重Updateが構造的に起こらない。Validationでも1フレーム1回を検査している。
- OnAwakeは Component::SyncEnableState() の有効判定より前で呼ぶ。
  無効なComponentにも必ず1回届く。
- OnTriggerEnter(TriggerContact) は final で塞ぎ、
  内部で TriggerEvent へ組み直して OnTriggerEnter(TriggerEvent) を呼ぶ。
- Collisionは正直に限定する。取得できるのは
  CharacterGround（QueryGroundの結果）と CharacterWall（SweepSphereの結果）だけ。
  一般RigidBody衝突は実装しない。
  penetration_depth と relative_velocity は取得手段が無いので
  フィールド自体を置いていない。0を入れて取れているように見せないこと。
- 未実装Service（Audio / Input Action / SaveGame / Runtime UI）は
  ～Available() が常に false を返すだけ。偽のAPIを置かない。

==================================================
3. Phase 6の現状（途中まで実装済み）
==================================================

新規（実装済み・g++構文確認済み・vcxproj登録済み）:
- RePlayEngine/Runtime/Scene/RuntimeSceneService.h/.cpp

改修:
- RuntimeContext.h/.cpp … Rebind(Scene&) を追加

実装済みの内容:
- ISceneAssetResolver でAssetGUID→パス解決（Runtime→Assets依存なし）
- SceneLoadState { Idle, Loading, ReadyToSwap, Swapping, Completed, Failed }
- SceneRequestResult { Accepted, Busy, InvalidRequest }
- active_ / staging_ を unique_ptr で所有するStaging World方式
- 失敗時は現在のWorldに一切触れない（Clear()を呼ばない）
- Swap順序: EventBus破棄 → CollisionDispatcher::Reset →
  旧World Clear() → unique_ptr差し替え → RuntimeContext::Rebind() →
  Scene::Start()
- RequestLoad / RequestReload / CancelPending / Tick

【未完了】
- Phase 6のValidationが未作成。したがってPhase 6は完了していない。
- RuntimeSceneServiceはまだ誰からも呼ばれていない。
  frameworkがWorldを自前で持っているため、統合が必要。

==================================================
4. 既存Validationコマンド一覧
==================================================

すべてヘッドレスで、D3D11もWindowも使いません。
終了コード0が合格です。

x64\Debug\3dgp.exe --validate-handles            （80-139）
x64\Debug\3dgp.exe --validate-serialization      （140-179）
x64\Debug\3dgp.exe --validate-missing-component  （180-209）
x64\Debug\3dgp.exe --validate-scene-version      （210-249）
x64\Debug\3dgp.exe --validate-behaviour          （250-289）
x64\Debug\3dgp.exe --validate-events             （290-329）
x64\Debug\3dgp.exe --validate-runtime-api        （330-369）
x64\Debug\3dgp.exe --validate-collision          （370-409）
x64\Debug\3dgp.exe --validate-prefab             （30-41）
x64\Debug\3dgp.exe --validate-scene "<path>"     （2-6）
x64\Debug\3dgp.exe --validate-large-scene        （60-72）
x64\Debug\3dgp.exe --validate-material           （50-56）
x64\Debug\3dgp.exe --validate-landscape          （20-28）

新しいValidationを追加する場合は、
Source/app/Runtime/main.cpp の既存パターンに合わせ、
未使用の終了コード帯を割り当ててください。

==================================================
5. Phase 6の残り作業
==================================================

RuntimeSceneServiceのValidationを追加してください。
--validate-runtime-scene（終了コード帯 410-459）

検査項目:
- 正常なScene Load
- Scene AからScene Bへの切替
- 同一Scene Reload
- 存在しないAssetGUID
- 壊れたScene
- 未対応Version
- Missing Componentを含むScene
- Unknown Propertyを含むScene
- Reference解決
- Load失敗時に旧Worldが維持される
- Load失敗時に旧ObjectHandleが無効化されない
- Swap後に旧Handleが無効になる
- 新WorldのHandleが有効になる
- Event Subscriptionが旧Worldから漏れない
- Collision接触状態が漏れない
- Deferred Destroy予約中の切替
- 連続Load要求
- Busy／Reject方針
- CancelPending

Validationの書き方は SerializationValidation.cpp と
BehaviourValidation.cpp を参照してください。
Checkerクラスで全項目を実行し、最初の失敗番号を返す形に揃えること。

Scene Assetの用意については、
実ファイルを新規に増やさず、テスト内で一時ファイルを
Saved/Validation/ 配下へ書いて使ってください。
既存のScene原本を絶対に変更しないでください。

ISceneAssetResolver のテスト実装をValidation内に用意し、
GUID→パスの対応を検証側から差し替えられるようにしてください。

==================================================
6. Phase 7 — SceneFlowService / Startup Scene
==================================================

RuntimeSceneServiceの上にSceneFlowServiceを実装してください。

必須API:
- LoadScene(SceneReferenceまたはAssetGUID)
- ReloadCurrentScene()
- ReturnToPreviousScene()
- QuitApplication()
- CanReturn()
- CurrentSceneGUID()
- PendingSceneGUID()
- CurrentTransitionState()
- LastResult / LastError

SceneFlowServiceはSceneの実読込を直接行わず、
RuntimeSceneServiceへ要求を渡す上位サービスにしてください。

履歴:
- 成功したScene切替だけ履歴へ追加
- 失敗したLoadを履歴へ追加しない
- Reloadで履歴を増殖させない
- Return実行時に履歴ループを作らない
- 履歴数に上限を設ける
- 無効GUIDを保持しない

Quit:
- テスト可能なQuit Requestとして扱う
- Validation中にプロセスを強制終了しない
- 実アプリケーション層がQuit要求を受け取れる構造にする

ProjectSettingsへStartup Sceneを追加してください。
現状 RePlayEngine/Project/ProjectSettings.h には
startup_scene_guid が存在しません。
ProjectSettingsSerializer.cpp の current_version は 1 です。

必須:
- startup_scene_guid
- AssetGUIDで保存
- 空GUIDを許容
- ProjectSettings独自Versionを2へ上げる
- v1からのMigration
- 読込失敗時の安全な既定値
- Editorの最後に開いたSceneとは完全に別設定
- Saved/EditorSession/ の情報と混同しない

起動時の流れ:
- Engine Boot
- ProjectSettings Load
- Startup Scene GUID確認
- RuntimeSceneServiceへLoad要求
- 成功後にゲーム開始
- 空GUID／無効GUID／Load失敗時は明示的な診断状態へ入る
- 無言で適当なSceneへフォールバックしない

既存のIScene／SceneManagerを削除しないでください。
役割分離:
- IScene / SceneManager … Boot、Loading、Game、Shutdown等のアプリケーション状態
- .replayscene … GameObjectを持つRuntime World Scene
- RuntimeSceneService … Runtime Worldの読込とSwap
- SceneFlowService … ゲーム側のScene遷移要求と履歴

Title、Game、Resultを巨大な専用C++ Sceneクラスとして作らないでください。

SceneTransitionBehaviourを Source/game/Behaviours/ へ実装してください。
Property例:
- destination_scene（SceneReference型）
- transition_mode
- trigger_once

要件:
- 遷移中の再発火を防止
- Missing Scene Referenceを診断
- Load失敗時にBehaviourを破壊しない
- Trigger EventとCollision Eventを混同しない
- OnTrigger処理中に即Swapしない。実際のSwapは安全な同期点で行う

Validation（--validate-scene-flow、460-519）:
- Startup Scene正常起動
- Startup Scene未設定
- 無効Startup Scene
- ProjectSettings旧Version Migration
- Load / Reload / Return
- 履歴
- Load失敗時の履歴保持
- 連続遷移要求
- Triggerからの遷移要求
- 同一フレーム複数Trigger
- Quit Request
- SceneFlowService不在時のServiceUnavailable

==================================================
7. Phase 8 — Editor統合
==================================================

Phase 1〜7の機能を既存Editorへ統合してください。

1. Project Settings
- Startup Scene選択UI
- .replayscene Assetだけ選択可能
- AssetGUIDで保存
- Missing Asset表示
- Clear設定
- 保存失敗のエラー表示

2. Add Component
- Native Behaviourを追加可能
- Behaviour Type GUIDを利用
- 表示名／カテゴリ／説明
- 型ごとのEditor if／switchを増殖させない
- BehaviourRegistry／既存ComponentRegistryと整合

3. Inspector
現状 PropertyDrawer.cpp は次が未完成です。
- ComponentReference … 所有GameObject選択のみ。Component一覧が未実装
- Array … 表示のみ。追加・削除・並べ替えUIが未実装
これらを完成させてください。
対応していない型を対応しているように見せないこと。

4. Missing Behaviour／Missing Component
- Type GUID / 元の型名 / Serialized Property / Unknown Property /
  Reference情報 を失わず表示
- ユーザーが明示的に削除するまでデータを保持
- Missing Componentを含むSceneを開いて保存しても未知データが失われないこと
  （この保証はPhase 2で実装済み。UIで壊さないこと）

5. Runtime診断
- Current Runtime Scene
- Pending Scene
- RuntimeSceneService State
- SceneFlow State
- Last Error
- Startup Scene
- WorldInstanceID
- Play／Edit Mode
RuntimeモジュールからEditorへ依存させないこと。
Editor側が読み取り専用APIを参照する形にすること。

6. Play Mode
- Play開始時にStartup Sceneを使う設定と
  現在編集中のSceneを使う設定を混同しない
- Runtime Scene切替後にEditorSelectionが古いGameObjectポインタを保持しない
- Play終了時にEditor Sceneを安全に復元
- Runtime Worldの変更をEditor Sceneへ暗黙保存しない
- Edit ModeのUndo履歴へRuntime操作を混ぜない

7. Asset選択
SceneReferenceは手入力パスではなく、
既存Asset Database／Asset GUID経路から選択すること。

8. framework統合
RuntimeSceneServiceがWorldを所有する形へ移行してください。
現在frameworkがSceneを自前で持っています。
Source/app/Runtime/framework_gameobject_scene.cpp を確認してください。

Validation（--validate-editor-integration、520-579）:
- Startup Scene設定の保存／再読込
- Scene Asset選択
- Behaviour追加
- Behaviour Property編集
- Missing Behaviour表示と保持
- Unknown Property保持
- Play開始／停止
- Runtime Scene切替後のSelection安全性
- Undo履歴分離
- Prefab内Behaviour
- Prefab InstanceのSceneReference

==================================================
8. Phase 9 — 最終QA・回帰・報告
==================================================

全Validationを実行し、追加で次を検査してください。
- Scene v7〜v11の読込
- v11での保存／再読込
- Prefab保存／生成／Remap
- Unknown Componentの往復保持
- Unknown Propertyの往復保持
- Missing Behaviourの往復保持
- ObjectReference／ComponentReferenceの解決
- 削除済みObjectへのReference
- Scene切替後の旧Handle
- Scene切替を繰り返した場合の状態漏れ
- 100回以上のScene Load／Reload
- 連続した失敗Load
- 1000以上のGameObject
- Deferred DestroyとScene Swapの競合
- Event Subscriptionの解除漏れ
- Collision Dispatcher状態漏れ
- SceneTransitionBehaviourの再発火
- Startup Scene不正時の診断
- Quit Request
- Debug用Validationが通常起動へ影響しないこと

可能な範囲で:
- g++／clang構文確認（-Wall -Wextra相当）
- XMLパース
- vcxproj／filters登録集合一致
- 重複登録0
- 実体欠落0
- Include依存確認
- Runtime→Editor逆依存確認
- 新規ファイルの改行／文字コード確認
- git diff --check

==================================================
9. 絶対禁止
==================================================

- git reset / git clean / git restore
- git checkoutによる変更破棄
- git switchによるBranch変更
- 自動Commit
- 未追跡ファイルの削除
- ユーザー変更の上書き
- Scene原本／Asset原本の削除・変更
- 大規模sed行削除
- ファイル途中から末尾までのtruncate
- 大規模な正規表現一括置換
- 無関係なコード整形・Rename
- 文字コード／改行コードの一括変換

- エラーを隠すための機能削除
- Validationの無効化
- 問題コードのコメントアウト
- 固定値でTestを通す
- 警告の一括無効化
- EditorAPIをRuntimeから参照
- Scene Load失敗時に旧Worldを破壊
- Type GUIDの再生成
- Unknown Dataの破棄
- 一般RigidBody Collisionを実装済みと偽る
- C# Runtime／hostfxrの実装
- Runtime UI／Audio／SaveGameを実装済みと偽る

- Playerクラスの復活
- 巨大PlayerComponent／巨大GameManager Singleton
- Scene名／Object名による特殊判定
- Scene読込時のPlayer自動生成
- controlledObjectIdの自動選出
- PlayerのSphere／Capsule Collider削除

- Test未実行を合格として報告
- MSVC未実行をMSVC成功として報告
- ASan未実行をASan成功として報告
- g++／clangの結果をMSVC成功の代わりにしない

==================================================
10. C#について
==================================================

Phase 6〜9でC#自体を実装しないでください。
ただし将来のC# Behaviour統合に必要な次の拡張点を壊さないでください。

- Stable Behaviour Type GUID（Reflection::TypeGUID）
- IBehaviourProvider（Native以外のProviderを後から足せること）
- Serialized Field Data
- Missing Behaviour保持
- Object／Component Handle Facade
- World Generation（WorldInstanceID）
- External Behaviour Provider登録
- Runtime API境界
- Scene切替時のAssembly側状態破棄に使える通知点

特に BehaviourRegistry を
「Nativeのfactoryしか置けないRegistry」へ退化させないでください。

==================================================
11. 進め方
==================================================

Phase 6の残り、Phase 7、Phase 8を順に進めてください。
各Phase終了時に次を済ませてから次へ進むこと。

- 変更ファイル確認
- include依存確認
- 宣言／定義の一致確認
- 新規ファイルのvcxproj／filters登録
- XML妥当性
- 重複登録確認
- 実体欠落確認
- g++／clangで可能な範囲の構文確認
- Serializerの読み書き対称性確認
- 参照検索
- 既存APIの呼び出し箇所確認
- Phase単位のValidation追加

MSVC Buildは私がWindows実機で行います。
Phase 9完了後にまとめて1回、または各Phase終了時のどちらでも構いません。
Buildを依頼するときは、実行コマンドと echo %ERRORLEVEL% を必ず別行にし、
Validationは1件ずつ記載してください。

作業容量の都合で全Phaseを完了できない場合は、
無理に書き切らず、そこまでの到達点と残作業を正確に報告してください。
未検証のコードを完了として報告することの方が問題です。

==================================================
12. 最終報告に含めること
==================================================

1. Phase 6〜9の完了概要
2. 新規／変更／削除ファイル数
3. 主要クラス一覧
4. Runtime Scene Loadの処理順
5. Scene Flowの処理順
6. Startup Sceneの起動手順
7. Editor統合内容
8. Scene／ProjectSettingsのVersion変更
9. Migration対応範囲
10. Missing／Unknown Data保持方法
11. Validation一覧と検査数
12. 実行済み検証
13. 未実施検証
14. 既知の制限
15. Windows実機で実行するDebug x64 Buildコマンド
16. Debug全Validationコマンド
17. Release x64 Clean Rebuildコマンド
18. Release全Validationコマンド
19. 手動確認手順
20. git diff --stat相当の変更量

MSVC、Windows実機、D3D11 Live Object、ASanを実行できていない場合は、
実行済みと記載せず、必ず未実施と明記してください。
