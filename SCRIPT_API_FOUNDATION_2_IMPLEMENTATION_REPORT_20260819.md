# Script API Foundation 2 実装報告

作成: 2026-08-19
対象: `sinotake` / 前回 Script API Foundation 適用済み ZIP

## 1. 実施範囲

- **第1段: 完了（静的実装）** — Object / hierarchy / GameObject enabled / Log の既存 `RuntimeContext` API を C# へ配線。
- **第2段: 完了（静的実装）** — Physics query / deferred operation / component query / runtime state を C# へ配線。
- **第3段: 完了（静的実装）** — `EventRecord::payload` の bool / int / double / string を C# で読み書きできるよう配線。
- **ビルド未検証 / 実機未検証**。MSBuild、`cl`、`link`、`dotnet build` は実行していない。

今回の作業は公開配線と payload の転送表現の追加のみ。既存 API・既存行は削除していない。

## 2. 足した API

|段|表位置|C++ 実装|C++ NativeApiTable|C# NativeApi|C# 公開包み|
|---:|---:|---|---|---|---|
|1|85|`NativeLogInfo`|`log_info`|`LogInfo`|`ScriptRuntimeContext.LogInfo`|
|1|86|`NativeLogWarning`|`log_warning`|`LogWarning`|`ScriptRuntimeContext.LogWarning`|
|1|87|`NativeLogError`|`log_error`|`LogError`|`ScriptRuntimeContext.LogError`|
|1|88|`NativeCreateGameObject`|`create_game_object`|`CreateGameObject`|`ScriptRuntimeContext.CreateGameObject`|
|1|89|`NativeGetWorldPosition`|`get_world_position`|`GetWorldPosition`|`ScriptRuntimeContext.GetWorldPosition`|
|1|90|`NativeSetParent`|`set_parent`|`SetParent`|`ScriptRuntimeContext.SetParent`|
|1|91|`NativeGetParent`|`get_parent`|`GetParent`|`ScriptRuntimeContext.GetParent`|
|1|92|`NativeGetChildren`|`get_children`|`GetChildren`|`ScriptRuntimeContext.GetChildren`|
|1|93|`NativeGetName`|`get_name`|`GetName`|`ScriptRuntimeContext.GetName`|
|1|94|`NativeSetName`|`set_name`|`SetName`|`ScriptRuntimeContext.SetName`|
|1|95|`NativeGetGameObjectEnabled`|`get_game_object_enabled`|`GetGameObjectEnabled`|`ScriptRuntimeContext.IsEnabled`|
|1|96|`NativeSetGameObjectEnabled`|`set_game_object_enabled`|`SetGameObjectEnabled`|`ScriptRuntimeContext.SetEnabled`|
|2|97|`NativeQueryGround`|`query_ground`|`QueryGround`|`ScriptRuntimeContext.QueryGround`|
|2|98|`NativeSweepSphere`|`sweep_sphere`|`SweepSphere`|`ScriptRuntimeContext.SweepSphere`|
|2|99|`NativeInstantiatePrefabDeferred`|`instantiate_prefab_deferred`|`InstantiatePrefabDeferred`|`ScriptRuntimeContext.InstantiatePrefabDeferred`|
|2|100|`NativeFlushDeferredOperations`|`flush_deferred_operations`|`FlushDeferredOperations`|`ScriptRuntimeContext.FlushDeferredOperations`|
|2|101|`NativePendingDeferredOperationCount`|`pending_deferred_operation_count`|`PendingDeferredOperationCount`|`ScriptRuntimeContext.PendingDeferredOperationCount`|
|2|102|`NativeHasComponent`|`has_component`|`HasComponent`|`ScriptRuntimeContext.HasComponent`|
|2|103|`NativeGetTimeScale`|`get_time_scale`|`GetTimeScale`|`ScriptRuntimeContext.TimeScale (getter)`|
|2|104|`NativeGetSceneTransitionInProgress`|`get_scene_transition_in_progress`|`GetSceneTransitionInProgress`|`ScriptRuntimeContext.SceneTransitionInProgress (getter)`|
|2|105|`NativePhysicsAvailable`|`physics_available`|`PhysicsAvailable`|`ScriptRuntimeContext.PhysicsAvailable`|
|2|106|`NativeSceneFlowAvailable`|`scene_flow_available`|`SceneFlowAvailable`|`ScriptRuntimeContext.SceneFlowAvailable`|
|3|107|`NativePollEventWithPayload`|`poll_event_with_payload`|`PollEventWithPayload`|`ScriptRuntimeContext.PollEvent → RuntimeEvent.Payload / TryGet*`|
|3|108|`NativePublishEventWithPayload`|`publish_event_with_payload`|`PublishEventWithPayload`|`ScriptRuntimeContext.PublishEvent(..., RuntimeEventPayload, ...)`|

### 第2段の `SaveGameAvailable`

`SaveGameAvailable` は今回の着手前から既に公開済みだったため、重複追加していない。ABI 表では **48番 `save_available` / `SaveAvailable`**、公開側は `ScriptRuntimeContext.SaveGameAvailable`。既存のものを消さず、同じ機能を別名で二重登録もしない判断にした。

### `TimeScale`

現行 `RuntimeContext` に存在するのは `float TimeScale() const` の getter。setter は存在しないため、新規機能を勝手に作らず **getter のみ** C# へ公開した。

## 3. NativeApiTable / NativeBridge 全件突き合わせ

- C++ `NativeApiTable`: **108件**
- C# `NativeBridge.NativeApi`: **108件**
- 正規化した名前・上からの順序: **108 / 108 完全一致**
- 着手前の C++ / C# 84件: **先頭84件が双方とも完全不変**
- `MakeNativeApiTable()`: **108件すべて代入済み、表順と完全一致、`nullptr` 残り 0件**

以下が上からの全一覧。

|#|C++|C#|
|---:|---|---|
|1|`find_game_object`|`FindGameObject`|
|2|`is_game_object_valid`|`IsGameObjectValid`|
|3|`get_local_position`|`GetLocalPosition`|
|4|`set_local_position`|`SetLocalPosition`|
|5|`get_local_rotation_euler`|`GetLocalRotationEuler`|
|6|`set_local_rotation_euler`|`SetLocalRotationEuler`|
|7|`get_local_scale`|`GetLocalScale`|
|8|`set_local_scale`|`SetLocalScale`|
|9|`get_component`|`GetComponent`|
|10|`destroy_game_object`|`DestroyGameObject`|
|11|`destroy_component`|`DestroyComponent`|
|12|`instantiate`|`Instantiate`|
|13|`load_scene`|`LoadScene`|
|14|`reload_scene`|`ReloadScene`|
|15|`return_to_previous_scene`|`ReturnToPreviousScene`|
|16|`subscribe_event`|`SubscribeEvent`|
|17|`unsubscribe_event`|`UnsubscribeEvent`|
|18|`poll_event`|`PollEvent`|
|19|`trigger_scene_flow`|`TriggerSceneFlow`|
|20|`set_scene_flow_bool`|`SetSceneFlowBool`|
|21|`set_scene_flow_int`|`SetSceneFlowInt`|
|22|`set_scene_flow_float`|`SetSceneFlowFloat`|
|23|`raycast`|`Raycast`|
|24|`find_motion_player`|`FindMotionPlayer`|
|25|`motion_play`|`MotionPlay`|
|26|`motion_play_from`|`MotionPlayFrom`|
|27|`motion_pause`|`MotionPause`|
|28|`motion_resume`|`MotionResume`|
|29|`motion_stop`|`MotionStop`|
|30|`motion_reverse`|`MotionReverse`|
|31|`motion_set_time`|`MotionSetTime`|
|32|`motion_set_speed`|`MotionSetSpeed`|
|33|`motion_set_weight`|`MotionSetWeight`|
|34|`motion_is_playing`|`MotionIsPlaying`|
|35|`motion_get_time`|`MotionGetTime`|
|36|`motion_get_duration`|`MotionGetDuration`|
|37|`input_available`|`InputAvailable`|
|38|`input_held`|`InputHeld`|
|39|`input_pressed`|`InputPressed`|
|40|`input_released`|`InputReleased`|
|41|`input_axis`|`InputAxis`|
|42|`input_pointer_delta_x`|`InputPointerDeltaX`|
|43|`input_pointer_delta_y`|`InputPointerDeltaY`|
|44|`audio_available`|`AudioAvailable`|
|45|`audio_play`|`AudioPlay`|
|46|`audio_stop`|`AudioStop`|
|47|`audio_update`|`AudioUpdate`|
|48|`save_available`|`SaveAvailable`|
|49|`save_set_bool`|`SaveSetBool`|
|50|`save_set_int`|`SaveSetInt`|
|51|`save_set_double`|`SaveSetDouble`|
|52|`save_set_string`|`SaveSetString`|
|53|`save_get_bool`|`SaveGetBool`|
|54|`save_get_int`|`SaveGetInt`|
|55|`save_get_double`|`SaveGetDouble`|
|56|`save_get_string`|`SaveGetString`|
|57|`save_has_key`|`SaveHasKey`|
|58|`save_delete_key`|`SaveDeleteKey`|
|59|`save_game`|`SaveGame`|
|60|`load_game`|`LoadGame`|
|61|`delete_save`|`DeleteSave`|
|62|`runtime_ui_available`|`RuntimeUIAvailable`|
|63|`create_ui_element`|`CreateUIElement`|
|64|`set_ui_text`|`SetUIText`|
|65|`get_ui_text`|`GetUIText`|
|66|`set_ui_image_color`|`SetUIImageColor`|
|67|`set_ui_rect`|`SetUIRect`|
|68|`set_ui_button_interactable`|`SetUIButtonInteractable`|
|69|`add_component`|`AddComponent`|
|70|`get_components`|`GetComponents`|
|71|`set_component_enabled`|`SetComponentEnabled`|
|72|`get_component_enabled`|`GetComponentEnabled`|
|73|`get_script_bool`|`GetScriptBool`|
|74|`set_script_bool`|`SetScriptBool`|
|75|`get_script_int`|`GetScriptInt`|
|76|`set_script_int`|`SetScriptInt`|
|77|`get_script_double`|`GetScriptDouble`|
|78|`set_script_double`|`SetScriptDouble`|
|79|`get_script_string`|`GetScriptString`|
|80|`set_script_string`|`SetScriptString`|
|81|`ui_get_focus`|`UIGetFocus`|
|82|`ui_set_focus`|`UISetFocus`|
|83|`ui_find_focus`|`UIFindFocus`|
|84|`publish_event`|`PublishEvent`|
|85|`log_info`|`LogInfo`|
|86|`log_warning`|`LogWarning`|
|87|`log_error`|`LogError`|
|88|`create_game_object`|`CreateGameObject`|
|89|`get_world_position`|`GetWorldPosition`|
|90|`set_parent`|`SetParent`|
|91|`get_parent`|`GetParent`|
|92|`get_children`|`GetChildren`|
|93|`get_name`|`GetName`|
|94|`set_name`|`SetName`|
|95|`get_game_object_enabled`|`GetGameObjectEnabled`|
|96|`set_game_object_enabled`|`SetGameObjectEnabled`|
|97|`query_ground`|`QueryGround`|
|98|`sweep_sphere`|`SweepSphere`|
|99|`instantiate_prefab_deferred`|`InstantiatePrefabDeferred`|
|100|`flush_deferred_operations`|`FlushDeferredOperations`|
|101|`pending_deferred_operation_count`|`PendingDeferredOperationCount`|
|102|`has_component`|`HasComponent`|
|103|`get_time_scale`|`GetTimeScale`|
|104|`get_scene_transition_in_progress`|`GetSceneTransitionInProgress`|
|105|`physics_available`|`PhysicsAvailable`|
|106|`scene_flow_available`|`SceneFlowAvailable`|
|107|`poll_event_with_payload`|`PollEventWithPayload`|
|108|`publish_event_with_payload`|`PublishEventWithPayload`|

## 4. 第3段: Event payload の寿命調査と扱い

既存 `NativeSubscribeEvent` は `EventBus::Subscribe` の callback で受け取った `const EventRecord&` を、その callback 内で `EncodeEventRecord(record)` して `NativeEventSubscription.pending` の `std::deque<std::string>` へ保存していた。つまり poll 時点では元の `EventRecord` を保持しておらず、**既に値コピーされた文字列だけを保持する設計**だった。

この既存方針に合わせ、payload も callback 内で即座に値コピーするようにした。Managed 側へ `EventRecord*` / `PropertyBag*` / `PropertyValue*` を渡していない。流れは次の通り。

1. EventBus callback 中に `EventRecord::payload.Entries()` を走査。
2. bool / int / float / double / string のみを型タグ付き文字列へコピー。float は C# 側要求に合わせ double として扱う。
3. キーと string 値は UTF-8 を hex 化し、改行・区切り文字との衝突を避ける。double は classic / invariant locale の round-trip 形式。
4. 完成した文字列を既存の購読キュー `pending` に所有させる。この時点で元 `EventRecord` の寿命から独立。
5. C# poll は v8 の `PollEventWithPayload` を使用し、**必要サイズ問い合わせ → 必要量の byte[] を確保 → コピー**の2段階で取得。固定長切り捨てはしない。
6. `ParseRuntimeEvent` が `RuntimeEventPayload` へ復元し、`RuntimeEvent.Payload` が Managed の値を所有する。

発行側は `RuntimeEventPayload` に `SetBool / SetInt / SetDouble / SetString` で値を積み、型タグ付き文字列を native へ渡す。native は新しい `EventRecord` の `payload` に `PropertyValue` として復元してから既存 `RuntimeContext::PublishEvent` を呼ぶ。

対応外の `PropertyType` は C# へ公開しない。依頼で要求された4系統だけを扱う。

## 5. 提出前チェック 1〜9

1. **BOM: OK** — 編集した8ソースすべて先頭3バイト `EF BB BF`。`.cs` 3ファイルも確認。
2. **ABI 順序: OK** — C++ 108件 / C# 108件、全順一致。旧84件 prefix も不変。全一覧は3章。
3. **MakeNativeApiTable: OK** — 108件すべて代入、表順一致、`nullptr` 残り0件。
4. **禁止 max/min: OK** — 全ソースを検索し、括弧なし `std::max(` / `std::min(` / `numeric_limits<T>::max()` / `min()` は0件。今回使用した `numeric_limits<int>::max` は `(std::numeric_limits<int>::max)()` 形式。
5. **ImGui 1.80 禁止API: OK** — `ImGui::BeginDisabled` / `ImGui::EndDisabled` 0件。
6. **Registry: OK** — `ComponentRegistry::Register` **88 → 88**、`PropertyRegistry::Register` **535 → 535**。減少なし。
7. **削除: OK** — 着手前 ZIP との行単位比較で、実装ソース8ファイル合計 **+1147 / -0**。削除0行。
8. **ゲーム固有概念: OK** — 追加行に HP / Damage / Skill / Turn / Enemy / Player 等のゲーム概念なし。
9. **SetWorldPosition: OK** — 新規実装・新規公開なし。着手前/提出時の既存文字列出現件数も **7 → 7** で増えていない。

## 6. 変更ファイル

- `Managed/RePlayEngine.Managed/NativeBridge.cs`
- `Managed/RePlayEngine.Managed/RuntimeTypes.cs`
- `Managed/RePlayEngine.Managed/ScriptRuntimeContext.cs`
- `RePlayEngine/Scripting/CSharp/CSharpScriptBackendHostInternal.h`
- `RePlayEngine/Scripting/CSharp/CSharpScriptBackendNative.cpp`
- `RePlayEngine/Scripting/CSharp/CSharpScriptBackendNativeEvents.cpp`
- `RePlayEngine/Scripting/CSharp/CSharpScriptBackendNativeInternal.h`
- `RePlayEngine/Scripting/CSharp/CSharpScriptBackendNativeScene.cpp`

新規 `.cpp` / `.h` / `.cs` ファイルは作っていないため、`.vcxproj` / `.filters` への追加対象はない。

## 7. 迷った箇所・判断を変えた箇所・やり残し

- **payload poll のバッファ方式を変更した。** 当初は既存 poll と同様の固定長を考えたが、payload 追加後はイベントサイズが増えるため固定長だと切り捨て/詰まりの原因になる。v8 callback を2段階のサイズ問い合わせ方式にして、既存 `PollEvent` は一切変更せず fallback として残した。
- **`SaveGameAvailable` は追加しなかった。** 既に 48番で存在するため。
- **`SetWorldPosition` は実装していない。** 親 transform を含む設定仕様は別判断という依頼をそのまま守った。
- **`TimeScale` setter は作っていない。** `RuntimeContext` に getter しか無いため。
- `RuntimeContext::FlushDeferredOperations()` のコメントは「Scene の同期点で framework が呼ぶ」としている。一方、今回の依頼は C# 公開を明示しているため配線した。安全点以外で呼ぶことを防ぐ新しい guard/仕様変更は足していない。これは実機で呼び出しタイミングを確認すべき点。
- payload の対応型は bool / int / double / string。Native の Float は double としてコピーする。それ以外の PropertyType を C# へ写す機能は今回の範囲外。
- **ビルド未検証 / 実機未検証**のため、ABI の機械照合・静的差分確認までは完了しているが、`SetNativeApi` 後の実呼び出し、Physics query、Deferred flush、payload 往復は実機確認が必要。

## 8. 検証状態

**ビルド未検証 / 実機未検証。**

MSBuild、手動 `cl` / `link`、`dotnet build` は実行していない。git commit / git push も実行していない。
