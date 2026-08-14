// Editor 統合検証のうち、API を跨ぐ一連の判定を持つ。
//
//   EditorIntegrationValidation.cpp       … 統合判定（このファイル）
//   EditorIntegrationValidationInternal.h … Probe・Resolver・往復 Fixture
//
// 判定は同じ Editor / Runtime 状態を引き継ぐため、公開検証関数は分断しない。
#include "EditorIntegrationValidationInternal.h"

namespace ReplayEngine::Editor::Validation
{
    using namespace Detail;
    // =====================================================================
    // Editor 統合
    // =====================================================================

    int RunEditorIntegrationValidation()
    {
        RegisterProbes();
        Checker check(520);

        std::string error;

        // -----------------------------------------------------------------
        // Project Settings — Startup Scene
        // -----------------------------------------------------------------

        Project::ProjectSettings settings;
        settings.SetStartupSceneGuid(guid_startup);

        Project::ProjectSettings restored_settings;
        const bool settings_ok = SettingsRoundTrip(settings, restored_settings, error);
        check.Expect(settings_ok && restored_settings.StartupSceneGuid() == guid_startup,
            "Startup Scene が AssetGUID として往復する");

        // 変更。UI の Combo が呼ぶのと同じ API を直接叩く。
        settings.SetStartupSceneGuid(guid_second);
        check.Expect(SettingsRoundTrip(settings, restored_settings, error) &&
            restored_settings.StartupSceneGuid() == guid_second,
            "Startup Scene を別の Scene へ変更できる");

        // Clear。空は正常な設定値なので、保存も復元もできる。
        settings.ClearStartupScene();
        check.Expect(!settings.HasStartupScene() &&
            SettingsRoundTrip(settings, restored_settings, error) &&
            !restored_settings.HasStartupScene(),
            "Startup Scene を解除でき、解除した状態も保存される");

        // 解決できない GUID でも設定値は消さない。
        Assets::AssetDatabase database;
        settings.SetStartupSceneGuid(guid_missing_asset);
        const Project::AssetReferenceStatus missing_status =
            settings.ResolveStartupScene(database);
        check.Expect(missing_status.IsMissing() &&
            settings.StartupSceneGuid() == guid_missing_asset,
            "解決できない Startup Scene は Missing 表示になり、GUID は保持される");
        check.Expect(missing_status.guid == guid_missing_asset,
            "Missing でも診断へ元の GUID が残る");

        settings.ClearStartupScene();
        check.Expect(settings.ResolveStartupScene(database).IsUnset(),
            "未設定は Missing ではなく Unset として区別される");

        // -----------------------------------------------------------------
        // Native Behaviour の列挙と追加
        // -----------------------------------------------------------------

        const RRuntime::BehaviourRegistry::Entry* probe_entry =
            RRuntime::BehaviourRegistry::Find(EditorProbeBehaviour::StaticTypeGUID());
        check.Expect(probe_entry != nullptr &&
            probe_entry->type_id == EditorProbeBehaviour::StaticTypeID(),
            "Behaviour を BehaviourRegistry から TypeGUID で引ける");
        check.Expect(probe_entry != nullptr && probe_entry->provider != nullptr &&
            std::string(probe_entry->provider->ProviderName()) == "Native",
            "供給元が Native Provider として登録されている");
        check.Expect(RRuntime::BehaviourRegistry::CanInstantiate(
            EditorProbeBehaviour::StaticTypeGUID()),
            "登録された Behaviour は今すぐ生成できる");

        const Core::ComponentTypeInfo* probe_info =
            Core::ComponentRegistry::Find(EditorProbeBehaviour::StaticTypeID());
        check.Expect(probe_info != nullptr && probe_info->category == "Internal" &&
            !probe_info->DisplayName().empty() && !probe_info->tooltip.empty() &&
            probe_info->module_id == "RePlayEngine.Validation",
            "表示名・カテゴリ・説明・モジュールを Registry のメタデータから引ける");
        check.Expect(probe_info != nullptr &&
            probe_info->type_guid == EditorProbeBehaviour::StaticTypeGUID(),
            "ComponentRegistry と BehaviourRegistry の TypeGUID が一致する");

        Scene::Scene editor_scene("EditorScene");
        Core::GameObject* actor = editor_scene.CreateGameObject("Actor");
        Core::GameObject* target = editor_scene.CreateGameObject("Target");
        check.Expect(actor != nullptr && target != nullptr,
            "編集 Scene に GameObject を作れる");
        if (actor == nullptr || target == nullptr)
        {
            return check.Report("Editor integration validation");
        }

        // Add Component パネルと同じ経路。型ごとの分岐を書かずに ID だけで足せる。
        Core::Component* added = actor->AddComponent(EditorProbeBehaviour::StaticTypeID());
        check.Expect(added != nullptr &&
            added->TypeID() == EditorProbeBehaviour::StaticTypeID(),
            "ComponentTypeID だけで Behaviour を追加できる（型ごとの分岐が要らない）");

        auto* probe = dynamic_cast<EditorProbeBehaviour*>(added);
        check.Expect(probe != nullptr, "追加した Component が Behaviour として解決できる");
        if (probe == nullptr) return check.Report("Editor integration validation");

        Core::Component* second = target->AddComponent(SecondProbeBehaviour::StaticTypeID());
        check.Expect(second != nullptr && second->StableID() != Core::invalid_component_stable_id,
            "追加した Component に ComponentStableID が振られる");

        // -----------------------------------------------------------------
        // Property の編集と保存／再読込
        // -----------------------------------------------------------------

        probe->marker = 42;
        probe->speed = 2.5f;
        probe->label = "日本語 と \"引用符\"";
        probe->target_object.object = target->ID();
        probe->target_component.owner = target->ID();
        probe->target_component.component = second != nullptr
            ? second->StableID() : Core::invalid_component_stable_id;
        probe->destination_scene = Reflection::SceneReference(std::string(guid_startup));
        probe->weights = { 1.0f, -2.0f, 0.5f };

        Serialization::SceneData captured;
        Serialization::CaptureScene(editor_scene, captured);

        Serialization::SceneData reloaded;
        const bool scene_ok = SceneRoundTrip(captured, reloaded, error);
        check.Expect(scene_ok, "編集した Scene を保存して読み戻せる");

        const Serialization::ComponentData* saved_probe = FindComponentData(
            reloaded, actor->ID().Value(), EditorProbeBehaviour::StaticTypeName());
        check.Expect(saved_probe != nullptr &&
            saved_probe->type_guid == EditorProbeBehaviour::StaticTypeGUID(),
            "保存された Component に TypeGUID が残る");
        check.Expect(saved_probe != nullptr &&
            saved_probe->module_id == "RePlayEngine.Validation",
            "保存された Component に Module ID が残る");

        Scene::Scene restored_scene("RestoredScene");
        Serialization::SceneLoadReport restore_report;
        Serialization::ApplySceneData(reloaded, restored_scene, restore_report);

        Core::GameObject* restored_actor = restored_scene.FindGameObjectByName("Actor");
        auto* restored_probe = restored_actor != nullptr
            ? restored_actor->GetComponent<EditorProbeBehaviour>() : nullptr;
        check.Expect(restored_probe != nullptr && restored_probe->marker == 42 &&
            restored_probe->speed > 2.4f && restored_probe->label == probe->label,
            "Behaviour の Property が保存・再読込で保たれる");
        check.Expect(restored_probe != nullptr && restored_probe->weights.size() == 3 &&
            restored_probe->weights[1] < -1.9f,
            "配列プロパティが要素数と値ごと保たれる");

        // 参照の解決。Editor の Picker が保存するのと同じ形。
        Core::GameObject* restored_target =
            restored_scene.FindGameObjectByID(target->ID());
        check.Expect(restored_probe != nullptr && restored_target != nullptr &&
            restored_probe->target_object.object == restored_target->ID(),
            "ObjectReference が読み込み後の Scene で解決できる");
        check.Expect(restored_probe != nullptr && restored_target != nullptr &&
            restored_target->FindComponentByStableID(
                restored_probe->target_component.component) != nullptr,
            "ComponentReference が ComponentStableID から解決できる");
        check.Expect(restored_probe != nullptr &&
            restored_probe->destination_scene.guid == guid_startup,
            "SceneReference が AssetGUID として保たれる");

        // -----------------------------------------------------------------
        // Missing Behaviour / Unknown Property の往復
        // -----------------------------------------------------------------

        Serialization::SceneData ghost_data;
        ghost_data.scene_name = "GhostScene";
        {
            Serialization::GameObjectData object;
            object.id = ObjectID{ 1 };
            object.name = "GhostOwner";

            Serialization::ComponentData ghost;
            ghost.type_name = ghost_type_name;
            ghost.type_id = Core::MakeComponentTypeID(ghost_type_name);
            ghost.type_guid = Reflection::MakeTypeGUID("ee000000000000000000000000000042");
            ghost.module_id = "Game.NotLoaded";
            ghost.type_version = 7;
            ghost.stable_id = 3;
            ghost.properties.Set("ghost_int", PropertyValue::MakeInt(99));
            ghost.properties.Set("ghost_ref",
                PropertyValue::MakeObjectReference(ObjectID{ 1 }));
            object.components.push_back(std::move(ghost));

            Serialization::ComponentData known;
            known.type_name = EditorProbeBehaviour::StaticTypeName();
            known.type_id = EditorProbeBehaviour::StaticTypeID();
            known.type_guid = EditorProbeBehaviour::StaticTypeGUID();
            known.stable_id = 1;
            known.properties.Set("marker", PropertyValue::MakeInt(5));
            known.properties.Set("unknown_property",
                PropertyValue::MakeString("消えてはいけない値"));
            object.components.push_back(std::move(known));

            ghost_data.objects.push_back(std::move(object));
        }

        Scene::Scene ghost_scene("GhostWorld");
        Serialization::SceneLoadReport ghost_report;
        Serialization::ApplySceneData(ghost_data, ghost_scene, ghost_report);

        check.Expect(ghost_report.missing_components >= 1 &&
            ghost_report.skipped_components == 0,
            "未登録の型は Missing Component として保持され、失敗にはならない");
        check.Expect(ghost_report.unknown_properties >= 1,
            "型が知らないプロパティが件数として記録される");

        Core::GameObject* ghost_owner = ghost_scene.FindGameObjectByName("GhostOwner");
        const auto* missing_component = ghost_owner != nullptr
            ? dynamic_cast<const Core::MissingComponent*>(
                ghost_owner->FindComponent(Core::MissingComponent::StaticTypeID()))
            : nullptr;
        check.Expect(missing_component != nullptr &&
            missing_component->Original().type_name == ghost_type_name &&
            missing_component->Original().module_id == "Game.NotLoaded" &&
            missing_component->Original().type_version == 7 &&
            missing_component->Original().type_guid.IsValid(),
            "Missing Component が型名・Module・Version・TypeGUID を保持する");
        check.Expect(missing_component != nullptr &&
            missing_component->Original().properties.Size() == 2,
            "Missing Component が Serialized Property を丸ごと保持する");
        check.Expect(missing_component != nullptr &&
            !missing_component->DescribeMissingType().empty() &&
            !missing_component->DescribeReason().empty(),
            "Inspector へ出す表示文字列を作れる");

        // 開いて保存し直しても未知データが失われないこと。
        Serialization::SceneData ghost_saved;
        Serialization::CaptureScene(ghost_scene, ghost_saved);

        const Serialization::ComponentData* ghost_written =
            FindComponentData(ghost_saved, 1, ghost_type_name);
        check.Expect(ghost_written != nullptr &&
            ghost_written->properties.Size() == 2 &&
            ghost_written->module_id == "Game.NotLoaded" &&
            ghost_written->type_version == 7,
            "Missing Component は元の型として書き戻され、内容が失われない");
        check.Expect(ghost_written != nullptr &&
            ghost_written->type_name != "MissingComponent",
            "ファイルへ MissingComponent という痕跡を残さない");

        const Serialization::ComponentData* known_written = FindComponentData(
            ghost_saved, 1, EditorProbeBehaviour::StaticTypeName());
        check.Expect(known_written != nullptr &&
            known_written->properties.Find("unknown_property") != nullptr,
            "型が知らないプロパティも保存し直したときに残る");

        // -----------------------------------------------------------------
        // Prefab 内 Behaviour
        // -----------------------------------------------------------------

        Serialization::SceneData subtree;
        const bool captured_subtree =
            Serialization::CaptureGameObjectSubtree(editor_scene, actor->ID(), subtree);
        check.Expect(captured_subtree && !subtree.objects.empty(),
            "Behaviour を持つ GameObject を Prefab 用に取り出せる");

        Scene::Scene prefab_scene("PrefabScene");
        Serialization::SceneLoadReport prefab_report;
        Core::GameObject* placed = Serialization::InstantiateSceneData(
            subtree, prefab_scene, prefab_report, "prefab-guid");
        auto* placed_probe = placed != nullptr
            ? placed->GetComponent<EditorProbeBehaviour>() : nullptr;
        check.Expect(placed_probe != nullptr && placed_probe->marker == 42,
            "Prefab 配置後も Behaviour と Property が復元される");

        // 同じ Prefab を 2 回置いて確かめる。
        //
        // 「元の ObjectID と違うこと」では確かめられない。
        // まっさらな Scene では採番が 1 から始まるので、
        // 採番し直していても元と同じ番号になることがある。
        // 2 回置いて衝突しないことが、採番し直しの本当の意味。
        Serialization::SceneLoadReport prefab_report_second;
        Core::GameObject* placed_again = Serialization::InstantiateSceneData(
            subtree, prefab_scene, prefab_report_second, "prefab-guid");
        check.Expect(placed != nullptr && placed_again != nullptr &&
            placed->ID() != placed_again->ID() &&
            placed_again->GetComponent<EditorProbeBehaviour>() != nullptr,
            "同じ Prefab を 2 回配置しても ObjectID が衝突しない");

        // -----------------------------------------------------------------
        // EditorContext と Selection
        // -----------------------------------------------------------------

        EditorContext context;
        context.AttachScene(&editor_scene);
        context.Selection().Select(actor->ID());
        context.BeginEdit("編集");
        context.CommitEdit();

        check.Expect(context.Selection().IsSelected(actor->ID()) &&
            context.History().CanUndo(),
            "編集 Scene で選択と Undo 履歴を作れる");

        context.ResetSceneState();
        check.Expect(context.Selection().Empty() && !context.History().CanUndo(),
            "ResetSceneState で選択と Undo 履歴が捨てられる");

        // -----------------------------------------------------------------
        // Play Mode — World の所有者は RuntimeSceneService だけ
        // -----------------------------------------------------------------

        RRuntime::RuntimeSceneService scenes;
        RRuntime::RuntimeContext runtime(scenes.ActiveWorld());
        scenes.ActiveWorld().Services().SetRuntime(&runtime);
        scenes.SetRuntimeContext(&runtime);

        const Core::WorldInstanceID empty_world = scenes.ActiveWorldID();
        check.Expect(scenes.HasActiveWorld() &&
            scenes.ActiveWorld().GameObjectCount() == 0 &&
            scenes.ActiveSceneGuid().empty(),
            "Play 前の Runtime World は空で、Scene GUID も未設定");

        // Play 開始。framework がやるのと同じ手順。
        Serialization::SceneData play_snapshot;
        Serialization::CaptureScene(editor_scene, play_snapshot);
        check.Expect(scenes.RequestAdopt(play_snapshot, std::string()) ==
            RRuntime::SceneRequestResult::Accepted,
            "編集 Scene の内容を Runtime World として受理できる");

        scenes.Tick();
        scenes.Tick();
        check.Expect(scenes.State() == RRuntime::SceneLoadState::Completed &&
            scenes.ActiveWorldID() != empty_world &&
            scenes.ActiveWorld().GameObjectCount() == editor_scene.GameObjectCount(),
            "Play 開始で Runtime World が作られる");
        check.Expect(&scenes.ActiveWorld() != &editor_scene,
            "Runtime World と編集 Scene が別の実体である（二重所有をしない）");

        // Play 中の変更は編集 Scene へ戻らない。
        Core::GameObject* runtime_actor = scenes.ActiveWorld().FindGameObjectByName("Actor");
        auto* runtime_probe = runtime_actor != nullptr
            ? runtime_actor->GetComponent<EditorProbeBehaviour>() : nullptr;
        if (runtime_probe != nullptr) runtime_probe->marker = 999;
        scenes.ActiveWorld().CreateGameObject("SpawnedAtRuntime");

        check.Expect(probe->marker == 42,
            "Runtime 側の Property 変更が編集 Scene へ戻らない");
        check.Expect(editor_scene.FindGameObjectByName("SpawnedAtRuntime") == nullptr,
            "Runtime 中に生成した GameObject が編集 Scene へ残らない");

        // Play 中は Editor が Runtime World を見る。
        context.SetPlayMode(true);
        context.AttachScene(&scenes.ActiveWorld());
        context.ResetSceneState();
        check.Expect(context.GetScene() == &scenes.ActiveWorld() && !context.CanEdit(),
            "Play 中は Editor が Runtime World を参照し、編集は禁止される");

        // Runtime 中の操作は Edit Mode の Undo へ混ざらない。
        check.Expect(!context.History().CanUndo(),
            "Play 開始時に Undo 履歴が空になる（Runtime 操作が混ざらない）");

        // -----------------------------------------------------------------
        // Runtime Scene 切替と Selection
        // -----------------------------------------------------------------

        const std::filesystem::path folder =
            std::filesystem::path("Saved") / "Validation" / "EditorIntegration";
        std::error_code directory_error;
        std::filesystem::create_directories(folder, directory_error);
        const std::filesystem::path next_path = folder / "Next.replayscene";

        Serialization::SceneData next_data;
        next_data.scene_name = "NextScene";
        {
            Serialization::GameObjectData object;
            object.id = ObjectID{ 1 };
            object.name = "NextRoot";
            next_data.objects.push_back(std::move(object));
        }
        const bool next_written = !directory_error &&
            Serialization::SceneSerializer::SaveToFile(next_data, next_path, error);
        check.Expect(next_written, "切替先の一時 Scene を書き出せる");

        TestSceneAssetResolver resolver;
        resolver.Map(guid_second, next_path);
        scenes.SetAssetResolver(&resolver);

        const ObjectID selected_before = runtime_actor != nullptr
            ? runtime_actor->ID() : ObjectID::Invalid();
        context.Selection().Select(selected_before);
        check.Expect(context.Selection().IsSelected(selected_before),
            "切替前に Runtime World の GameObject を選択できる");

        const Core::WorldInstanceID before_switch = scenes.ActiveWorldID();
        scenes.RequestLoad(guid_second);
        scenes.Tick();
        scenes.Tick();
        check.Expect(scenes.State() == RRuntime::SceneLoadState::Completed &&
            scenes.ActiveWorldID() != before_switch &&
            scenes.ActiveSceneGuid() == guid_second,
            "Runtime Scene を切り替えられる");

        // framework が World 入れ替えのたびに行うのと同じ後始末。
        context.AttachScene(&scenes.ActiveWorld());
        context.ResetSceneState();
        check.Expect(context.Selection().Empty() &&
            context.GetScene() == &scenes.ActiveWorld(),
            "Scene 切替で旧 World の選択が無効化され、参照先も差し替わる");

        // 保険としての PruneMissing。
        //
        // 【これだけに頼ってはいけない】
        //   PruneMissing は ObjectID が「今の Scene に無い」ときだけ外す。
        //   World をまたぐと、同じ ObjectID が別の World にも存在しうる。
        //   その場合 PruneMissing は何もせず、選択はまったく無関係な
        //   GameObject を指したまま残る。
        //   だから World 入れ替え時は ResetSceneState() で番号ごと捨てる。
        //   ここでは「今の World に無い ID なら外れる」ことだけを確かめる。
        Core::ObjectID absent_id{ 999999 };
        check.Expect(scenes.ActiveWorld().FindGameObjectByID(absent_id) == nullptr,
            "検証用の ObjectID が新しい World に存在しないことを確かめる");
        context.Selection().Select(absent_id);
        context.Selection().PruneMissing(scenes.ActiveWorld());
        check.Expect(context.Selection().Empty(),
            "今の World に居ない GameObject の選択は PruneMissing で外れる");

        // -----------------------------------------------------------------
        // Play 終了
        // -----------------------------------------------------------------

        const std::size_t editor_objects_before = editor_scene.GameObjectCount();
        scenes.ResetToEmptyWorld();
        context.SetPlayMode(false);
        context.AttachScene(&editor_scene);
        context.ResetSceneState();

        check.Expect(scenes.ActiveWorld().GameObjectCount() == 0 &&
            scenes.ActiveSceneGuid().empty(),
            "Play 終了で Runtime World が空へ戻る");
        check.Expect(editor_scene.GameObjectCount() == editor_objects_before &&
            editor_scene.FindGameObjectByName("Actor") != nullptr,
            "Play 終了後に編集 Scene がそのまま残っている");
        check.Expect(context.GetScene() == &editor_scene && context.CanEdit(),
            "Play 終了で Editor が編集 Scene へ戻り、編集できる状態になる");
        check.Expect(probe->marker == 42 &&
            editor_scene.FindGameObjectByName("SpawnedAtRuntime") == nullptr,
            "Runtime の変更が編集 Scene へ暗黙保存されていない");
        check.Expect(!context.History().CanUndo(),
            "Play 終了時に Undo 履歴が Runtime 操作を含んでいない");

        // -----------------------------------------------------------------
        // Startup Scene 起動
        // -----------------------------------------------------------------

        RRuntime::SceneFlowService flow(scenes);
        runtime.SetSceneFlow(&flow);
        resolver.Map(guid_startup, next_path);

        check.Expect(flow.BeginStartupScene(std::string()) ==
            RRuntime::RuntimeStatus::SceneMissing &&
            flow.StartupState() == RRuntime::StartupSceneState::NotConfigured,
            "Startup Scene 未設定は診断状態になり、別の Scene を読まない");

        check.Expect(RRuntime::Succeeded(flow.BeginStartupScene(guid_startup)),
            "Startup Scene の起動要求が受理される");
        flow.Tick();
        flow.Tick();
        check.Expect(flow.StartupState() == RRuntime::StartupSceneState::Ready &&
            scenes.ActiveSceneGuid() == guid_startup,
            "Startup Scene から Runtime World が起動する");

        // World の実体が入れ替わるたびに番号が変わること。
        // 「所有者が 1 つ」であることを、番号の一意性で裏取りする。
        check.Expect(scenes.ActiveWorldID() != before_switch &&
            scenes.ActiveWorldID() != empty_world,
            "World の実体番号が入れ替えのたびに変わる");

        // Runtime -> Editor の逆依存が無いことは、この検証が
        // Editor 側に置かれていること自体で示している。
        // Runtime 側から EditorContext を触る経路はコンパイル時に存在しない。
        check.Expect(scenes.ActiveWorld().Services().Runtime() == &runtime,
            "Runtime World は RuntimeContext だけを知り、Editor を参照しない");

        return check.Report("Editor integration validation");
    }
}
