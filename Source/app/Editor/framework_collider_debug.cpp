// Collider の可視化と、衝突まわりの診断表示。
//
// 【描画方法について】
//   このプロジェクトには線を引く仕組みが無く、TransformGizmo も数値入力だけで
//   3D のギズモを描いていない。そこへ線描画のパイプラインを新設すると
//   影響範囲が広くなりすぎるため、ImGui のオーバーレイへ投影して描いている。
//
//   「どんな線を引くか」は ColliderDebugDraw が決め、
//   ここは投影と ImGui への受け渡しだけを行う。役割を分けてあるので、
//   将来ちゃんとした線描画を入れるときは、このファイルだけ差し替えればよい。

#include "framework.h"

#include "../../RePlayEngine/Components/Gameplay/CharacterMotorComponent.h"
#include "../../RePlayEngine/Components/Physics/ColliderComponent.h"
#include "../../RePlayEngine/Object/GameObject/GameObject.h"
#include "../../RePlayEngine/Physics/CollisionLayers.h"

#ifdef USE_IMGUI

namespace
{
    using BackendMode = ReplayEngine::Scene::SceneCollisionWorld::BackendMode;

    // ワールド座標を画面座標へ落とす。
    // カメラの後ろにある点は false を返し、線ごと捨てる
    // （投影すると画面の反対側へ跳ねて、無関係な位置に線が出るため）。
    bool ProjectToScreen(const DirectX::XMMATRIX& view_projection,
        const DirectX::XMFLOAT3& world, const ImVec2& origin, const ImVec2& size,
        ImVec2& out)
    {
        using namespace DirectX;

        const XMVECTOR position = XMVectorSet(world.x, world.y, world.z, 1.0f);
        const XMVECTOR clip = XMVector4Transform(position, view_projection);

        const float w = XMVectorGetW(clip);
        if (w <= 1.0e-4f) return false;

        const float x = XMVectorGetX(clip) / w;
        const float y = XMVectorGetY(clip) / w;

        out.x = origin.x + (x * 0.5f + 0.5f) * size.x;
        out.y = origin.y + (0.5f - y * 0.5f) * size.y;
        return true;
    }

    const char* SourceText(const ReplayEngine::Scene::CollisionSourceInfo& source)
    {
        return ReplayEngine::Scene::ToString(source.backend);
    }
}

void framework::draw_collider_debug_overlay()
{
    if (!show_collider_debug_draw) return;
    if (game_scene == nullptr) return;

    ReplayEngine::Editor::ColliderDebugDraw::Options options;
    options.draw_bounds = show_collider_debug_bounds;
    options.draw_shapes = true;
    options.draw_mesh_wireframe = show_collider_debug_wireframe;

    ReplayEngine::Editor::ColliderDebugDraw::Build(
        object_collision_world, options, object_collider_debug_lines);
    if (object_collider_debug_lines.empty()) return;

    const Camera& camera = game_scene->Gameplay().GetCamera();
    const DirectX::XMMATRIX view_projection =
        DirectX::XMLoadFloat4x4(&camera.GetView()) *
        DirectX::XMLoadFloat4x4(&camera.GetProjection());

    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
    const ImVec2 origin = ImGui::GetMainViewport()->Pos;
    const ImVec2 size = ImGui::GetMainViewport()->Size;

    for (const ReplayEngine::Editor::DebugLine& line : object_collider_debug_lines)
    {
        ImVec2 start{};
        ImVec2 end{};
        if (!ProjectToScreen(view_projection, line.start, origin, size, start)) continue;
        if (!ProjectToScreen(view_projection, line.end, origin, size, end)) continue;
        draw_list->AddLine(start, end, line.color, 1.0f);
    }
}

void framework::draw_collision_diagnostics_panel()
{
    if (!show_collision_diagnostics) return;

    if (!ImGui::Begin(u8"衝突の診断", &show_collision_diagnostics))
    {
        ImGui::End();
        return;
    }

    ReplayEngine::Scene::Scene& scene = active_object_scene();
    ReplayEngine::Scene::SceneServices& services = scene.Services();

    // ---- 問い合わせ先の構成 ------------------------------------------------
    ImGui::TextUnformatted(u8"問い合わせ先");
    ImGui::Separator();

    int backend = services.CollisionBackendMode();
    const char* backend_labels[]{ "Legacy Only", "Hybrid", "Scene Colliders Only" };
    if (backend < 0 || backend > 2) backend = 1;
    if (ImGui::Combo(u8"Backend Mode", &backend, backend_labels, 3))
    {
        // Scene 単位の設定なので Scene の状態を書き換える。
        // Collider ごとには保存しない。
        services.SetCollisionBackendMode(backend);
        object_editor_context.MarkDirty();
    }
    ImGui::TextDisabled(u8"Scene 単位の設定です。Collider ごとには保存されません。");

    // ---- 旧 Stage の移行状態 ------------------------------------------------
    ImGui::Spacing();
    ImGui::TextUnformatted(u8"旧 Stage の移行");
    ImGui::Separator();

    auto& migration = services.LegacyStageMigration();
    const auto stage_source =
        ReplayEngine::Scene::LegacyStageMigrationState::stage_source_id;
    const bool stage_migrated = migration.IsMigrated(stage_source);

    ImGui::Text(u8"旧 Stage: %s", stage_migrated ? u8"移行済み" : u8"未移行");
    ImGui::Text(u8"移行済みの移行元: %zu 件", migration.Count());
    ImGui::Text(u8"旧 Stage へ問い合わせた: %s",
        object_collision_world.LegacyConsulted() ? u8"はい" : u8"いいえ");

    if (!stage_migrated && ImGui::Button(u8"旧 Stage を移行済みにする"))
    {
        // 移行済みにすると、Hybrid でも旧 Stage へ問い合わせなくなる。
        // 同じ地形へ二重に当たらないための唯一のスイッチ。
        migration.MarkMigrated(stage_source, ReplayEngine::Core::ObjectID::Invalid());
        object_editor_context.MarkDirty();
    }
    if (stage_migrated && ImGui::Button(u8"旧 Stage の移行を取り消す"))
    {
        migration.ClearMigrated(stage_source);
        object_editor_context.MarkDirty();
    }

    // ---- 登録表 --------------------------------------------------------------
    ImGui::Spacing();
    ImGui::TextUnformatted(u8"Collider の登録表");
    ImGui::Separator();

    ImGui::Text(u8"登録: %zu   有効: %zu   押し戻し: %zu   Trigger: %zu",
        object_collision_world.RegisteredColliderCount(),
        object_collision_world.ActiveColliderCount(),
        object_collision_world.BlockingColliderCount(),
        object_collision_world.TriggerColliderCount());
    ImGui::Text(u8"Cook 済み Mesh: %zu   接触ペア: %zu",
        object_collision_world.MeshColliderCount(),
        object_collision_world.ActiveTriggerPairCount());
    ImGui::Text(u8"再走査した回数: %zu", object_collision_world.RescanCount());
    ImGui::TextDisabled(u8"再走査は Scene の構成が変わったときだけ増えます。"
        u8"毎フレーム増えていれば、どこかで構成変更が起き続けています。");

    ImGui::Text(u8"Cook キャッシュ: 生存 %zu / 表 %zu   Cook 実行 %zu 回",
        object_collision_cook_cache.LiveEntryCount(),
        object_collision_cook_cache.TableSize(),
        object_collision_cook_cache.CookCount());

    // ---- 直近の Hit 元 -------------------------------------------------------
    ImGui::Spacing();
    ImGui::TextUnformatted(u8"直近の Hit 元");
    ImGui::Separator();

    const auto& ground = object_collision_world.LastGroundSource();
    const auto& sweep = object_collision_world.LastSweepSource();
    ImGui::Text(u8"接地: %s  Object %s  Collider %u", SourceText(ground),
        ground.object.ToString().c_str(), ground.collider);
    ImGui::Text(u8"壁　: %s  Object %s  Collider %u", SourceText(sweep),
        sweep.object.ToString().c_str(), sweep.collider);

    // 操作対象の Motor が「何に」当たっているかも出す。
    const ReplayEngine::Core::ObjectID controlled = services.ControlledObject();
    if (const ReplayEngine::Core::GameObject* target = scene.FindGameObjectByID(controlled))
    {
        if (const auto* motor =
            target->GetComponent<ReplayEngine::Components::CharacterMotorComponent>())
        {
            ImGui::Spacing();
            ImGui::Text(u8"操作対象: %s", target->Name().c_str());
            ImGui::Text(u8"接地している: %s", motor->Grounded() ? u8"はい" : u8"いいえ");
            ImGui::Text(u8"地形を検出: %s", motor->HasGround() ? u8"はい" : u8"いいえ");

            const std::string status = motor->PrimaryColliderStatus();
            if (!status.empty())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f), u8"⚠ %s", status.c_str());
            }
            else if (const auto* primary = motor->ResolvePrimaryCollider())
            {
                ImGui::Text(u8"移動用 Collider: %s #%d",
                    ReplayEngine::Components::ToString(primary->Shape()),
                    primary->collider_key);
            }
        }
    }

    // ---- 表示設定 -------------------------------------------------------------
    ImGui::Spacing();
    ImGui::TextUnformatted(u8"表示");
    ImGui::Separator();
    ImGui::Checkbox(u8"Collider の形を描く", &show_collider_debug_draw);
    ImGui::Checkbox(u8"境界ボックスを描く", &show_collider_debug_bounds);
    ImGui::Checkbox(u8"Mesh の三角形を描く", &show_collider_debug_wireframe);
    ImGui::TextDisabled(u8"Mesh は既定で境界ボックスのみです。"
        u8"三角形は Collider 側の「三角形を表示」も有効にしたものだけ描かれます。");

    ImGui::End();
}

#else

void framework::draw_collider_debug_overlay() {}
void framework::draw_collision_diagnostics_panel() {}

#endif
