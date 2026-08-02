#include <time.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>

#include "framework.h"
#include "../../../RePlayEngine/Assets/AssetDatabase.h"
#include "../../../RePlayEngine/Components/Gameplay/StageGameplayComponents.h"
#include "../../../RePlayEngine/Components/Rendering/LightComponents.h"
#include "../../../RePlayEngine/Editor/Validation/SceneValidator.h"
#include "../../../RePlayEngine/Landscape/LandscapeCollision.h"
#include "../../../RePlayEngine/Landscape/LandscapeData.h"
#include "../../../RePlayEngine/Landscape/LandscapeEditorTool.h"
#include "../../../RePlayEngine/Landscape/LandscapeRenderer.h"
#include "../../../RePlayEngine/Object/Registry/BuiltInComponents.h"
#include "../../../RePlayEngine/Scene/Runtime/Scene.h"
#include "../../../RePlayEngine/Scene/Serialization/SceneData.h"
#include "../../../RePlayEngine/Scene/Serialization/PrefabSerializer.h"
#include "../../../RePlayEngine/Scene/Serialization/SceneSerializer.h"

namespace
{
    int RunHeadlessPrefabValidation(const char* command_line)
    {
        std::istringstream arguments(command_line != nullptr ? command_line : "");
        std::string command;
        if (!(arguments >> command) || command != "--validate-prefab") return -1;

        ReplayEngine::Core::RegisterBuiltInComponents();
        ReplayEngine::Scene::Scene scene("PrefabValidation");
        ReplayEngine::Core::GameObject* root = scene.CreateGameObject("PrefabRoot");
        ReplayEngine::Core::GameObject* child = scene.CreateGameObject("PrefabChild");
        if (root == nullptr || child == nullptr || !child->SetParent(root, false) ||
            root->AddComponent<ReplayEngine::Components::PointLightComponent>() == nullptr ||
            child->AddComponent<ReplayEngine::Components::SpawnPointComponent>() == nullptr)
        {
            std::fprintf(stderr, "Prefab source hierarchy creation failed\n");
            return 30;
        }
        root->GetTransform().SetLocalPosition({ 1.0f, 2.0f, 3.0f });
        child->GetTransform().SetLocalPosition({ 0.0f, 4.0f, 0.0f });

        namespace Serialization = ReplayEngine::Scene::Serialization;
        const std::filesystem::path prefab_path = std::filesystem::path("Saved") /
            "Validation" / "PrefabFoundation.replayprefab";
        constexpr const char* source_guid = "validation-prefab-guid";
        std::string error;
        const ReplayEngine::Core::ObjectID original_root = root->ID();
        const ReplayEngine::Core::ObjectID original_child = child->ID();
        if (!Serialization::PrefabSerializer::Save(scene, original_root, prefab_path, error) ||
            !Serialization::PrefabSerializer::LinkInstance(scene, original_root, source_guid, error))
        {
            std::fprintf(stderr, "Prefab save/link failed: %s\n", error.c_str());
            return 31;
        }
        if (!root->IsPrefabRoot() || !child->IsPrefabInstance() ||
            child->PrefabInstanceRoot() != original_root ||
            root->PrefabLocalID() == 0 || child->PrefabLocalID() == 0 ||
            root->PrefabLocalID() == child->PrefabLocalID())
        {
            std::fprintf(stderr, "Prefab instance identity is invalid\n");
            return 32;
        }

        Serialization::PrefabOverrideSummary summary =
            Serialization::PrefabSerializer::InspectOverrides(
                scene, original_root, prefab_path, source_guid);
        if (summary.missing_source || summary.has_overrides)
        {
            std::fprintf(stderr, "Fresh Prefab incorrectly reports overrides\n");
            return 33;
        }
        root->GetTransform().SetLocalPosition({ 9.0f, 2.0f, 3.0f });
        summary = Serialization::PrefabSerializer::InspectOverrides(
            scene, original_root, prefab_path, source_guid);
        if (!summary.has_overrides)
        {
            std::fprintf(stderr, "Prefab transform override was not detected\n");
            return 34;
        }

        Serialization::SceneLoadReport report;
        if (!Serialization::PrefabSerializer::RevertOverrides(
            scene, original_root, prefab_path, source_guid, error, &report))
        {
            std::fprintf(stderr, "Prefab revert failed: %s\n", error.c_str());
            return 35;
        }
        root = scene.FindGameObjectByID(original_root);
        child = scene.FindGameObjectByID(original_child);
        if (root == nullptr || child == nullptr ||
            std::fabs(root->GetTransform().LocalPosition().x - 1.0f) > 0.00001f ||
            !root->IsPrefabRoot() || child->PrefabInstanceRoot() != original_root)
        {
            std::fprintf(stderr, "Prefab revert did not preserve Scene ObjectIDs/state\n");
            return 36;
        }

        const ReplayEngine::Core::ObjectID first = Serialization::PrefabSerializer::Instantiate(
            scene, prefab_path, error, &report, source_guid);
        const ReplayEngine::Core::ObjectID second = Serialization::PrefabSerializer::Instantiate(
            scene, prefab_path, error, &report, source_guid);
        ReplayEngine::Core::GameObject* first_root = scene.FindGameObjectByID(first);
        ReplayEngine::Core::GameObject* second_root = scene.FindGameObjectByID(second);
        if (!first.Valid() || !second.Valid() || first == second ||
            first_root == nullptr || second_root == nullptr ||
            !first_root->IsPrefabRoot() || !second_root->IsPrefabRoot() ||
            first_root->Children().size() != 1 || second_root->Children().size() != 1 ||
            first_root->Children().front()->GetComponent<
                ReplayEngine::Components::SpawnPointComponent>() == nullptr)
        {
            std::fprintf(stderr, "Repeated Prefab instantiation/remap failed: %s\n", error.c_str());
            return 37;
        }

        first_root->SetName("PrefabRootApplied");
        if (!Serialization::PrefabSerializer::ApplyOverrides(
            scene, first, prefab_path, source_guid, error))
        {
            std::fprintf(stderr, "Prefab apply failed: %s\n", error.c_str());
            return 38;
        }
        summary = Serialization::PrefabSerializer::InspectOverrides(
            scene, first, prefab_path, source_guid);
        if (summary.has_overrides || summary.missing_source)
        {
            std::fprintf(stderr, "Applied Prefab still reports overrides\n");
            return 39;
        }

        if (!Serialization::PrefabSerializer::Unpack(scene, second, error))
        {
            std::fprintf(stderr, "Prefab unpack failed: %s\n", error.c_str());
            return 40;
        }
        second_root = scene.FindGameObjectByID(second);
        if (second_root == nullptr || second_root->IsPrefabInstance() ||
            second_root->Children().front()->IsPrefabInstance())
        {
            std::fprintf(stderr, "Prefab unpack left instance metadata\n");
            return 41;
        }

        std::fprintf(stderr,
            "Prefab OK: recursive save, GUID/local IDs, override detect, revert with stable Scene IDs, repeated remap, apply/unpack OK\n");
        return 0;
    }

    int RunHeadlessLandscapeValidation(const char* command_line)
    {
        std::istringstream arguments(command_line != nullptr ? command_line : "");
        std::string command;
        if (!(arguments >> command) || command != "--validate-landscape") return -1;

        using namespace ReplayEngine::Landscape;
        LandscapeData data;
        if (!data.Initialize(65, 65, 1.0f))
        {
            std::fprintf(stderr, "Landscape initialization failed\n");
            return 20;
        }

        LandscapeRenderer renderer;
        LandscapeCollision collision;
        const int initial_render_chunks = renderer.UpdateDirtyChunks(data);
        const int initial_collision_chunks = collision.UpdateDirtyChunks(data);
        if (initial_render_chunks != 4 || initial_collision_chunks != 4)
        {
            std::fprintf(stderr, "Initial chunk build failed: render=%d collision=%d\n",
                initial_render_chunks, initial_collision_chunks);
            return 21;
        }

        LandscapeBrush brush;
        brush.radius = 3.0f;
        brush.strength = 2.0f;
        brush.falloff = 0.5f;
        LandscapeEditorTool tool;
        if (!tool.BeginStroke(data, LandscapeBrushMode::Raise, brush) ||
            !tool.ApplySample(8.0f, 8.0f, 1.0f))
        {
            std::fprintf(stderr, "Landscape brush stroke failed\n");
            return 22;
        }
        std::unique_ptr<LandscapeUndoCommand> stroke = tool.EndStroke();
        const float raised_height = data.HeightAt(8, 8);
        if (stroke == nullptr || stroke->ChangedSampleCount() == 0 || raised_height <= 0.0f)
        {
            std::fprintf(stderr, "Landscape stroke produced no undoable samples\n");
            return 23;
        }

        const int edited_render_chunks = renderer.UpdateDirtyChunks(data);
        const int edited_collision_chunks = collision.UpdateDirtyChunks(data);
        if (edited_render_chunks != 1 || edited_collision_chunks != 1)
        {
            std::fprintf(stderr, "Landscape dirty update was not localized: render=%d collision=%d\n",
                edited_render_chunks, edited_collision_chunks);
            return 24;
        }

        stroke->Undo(data);
        if (std::fabs(data.HeightAt(8, 8)) > 0.00001f)
        {
            std::fprintf(stderr, "Landscape undo failed\n");
            return 25;
        }
        stroke->Redo(data);
        if (std::fabs(data.HeightAt(8, 8) - raised_height) > 0.00001f)
        {
            std::fprintf(stderr, "Landscape redo failed\n");
            return 26;
        }

        const std::filesystem::path output_path = std::filesystem::path("Saved") /
            "Validation" / "LandscapeFoundation.replaylandscape";
        std::string error;
        if (!data.Save(output_path, error))
        {
            std::fprintf(stderr, "Landscape save failed: %s\n", error.c_str());
            return 27;
        }
        LandscapeData loaded;
        if (!LandscapeData::Load(output_path, loaded, error) ||
            loaded.Width() != data.Width() || loaded.Height() != data.Height() ||
            std::fabs(loaded.HeightAt(8, 8) - raised_height) > 0.00001f)
        {
            std::fprintf(stderr, "Landscape reload verification failed: %s\n", error.c_str());
            return 28;
        }

        std::fprintf(stderr,
            "Landscape OK: %zu samples, %zu chunks, %zu edited samples, localized 1-chunk rebuild, undo/redo/save/reload OK\n",
            data.SampleCount(), data.Chunks().size(), stroke->ChangedSampleCount());
        return 0;
    }

    int RunHeadlessSceneValidation(const char* command_line)
    {
        std::istringstream arguments(command_line != nullptr ? command_line : "");
        std::string command;
        std::string scene_path_text;
        if (!(arguments >> command) || command != "--validate-scene") return -1;
        if (!(arguments >> std::quoted(scene_path_text)) || scene_path_text.empty())
        {
            std::fprintf(stderr, "--validate-scene requires a path\n");
            return 2;
        }

        ReplayEngine::Core::RegisterBuiltInComponents();
        ReplayEngine::Assets::AssetDatabase assets;
        std::string asset_error;
        assets.Load(asset_error);

        namespace Serialization = ReplayEngine::Scene::Serialization;
        Serialization::SceneData source;
        std::string error;
        const std::filesystem::path scene_path(scene_path_text);
        if (!Serialization::SceneSerializer::LoadFromFile(source, scene_path, error))
        {
            std::fprintf(stderr, "Scene load failed: %s\n", error.c_str());
            return 3;
        }

        ReplayEngine::Scene::Scene scene;
        Serialization::SceneLoadReport load_report;
        Serialization::ApplySceneData(source, scene, load_report);
        const auto issues = ReplayEngine::Editor::SceneValidator::Validate(scene, &assets);
        int errors = 0;
        for (const ReplayEngine::Editor::ValidationIssue& issue : issues)
        {
            if (issue.severity == ReplayEngine::Editor::ValidationSeverity::Error) ++errors;
            std::fprintf(stderr, "[%s] %s: %s\n",
                issue.severity == ReplayEngine::Editor::ValidationSeverity::Error ? "ERROR" :
                issue.severity == ReplayEngine::Editor::ValidationSeverity::Warning ? "WARN" : "INFO",
                issue.code.c_str(), issue.message.c_str());
        }

        Serialization::SceneData captured;
        Serialization::CaptureScene(scene, captured);
        const std::filesystem::path roundtrip_path = std::filesystem::path("Saved") /
            "Validation" / (scene_path.stem().string() + ".roundtrip.replayscene");
        if (!Serialization::SceneSerializer::SaveToFile(captured, roundtrip_path, error))
        {
            std::fprintf(stderr, "Round-trip save failed: %s\n", error.c_str());
            return 4;
        }
        Serialization::SceneData roundtrip;
        if (!Serialization::SceneSerializer::LoadFromFile(roundtrip, roundtrip_path, error) ||
            roundtrip.objects.size() != captured.objects.size() ||
            roundtrip.version != Serialization::SceneData::current_version)
        {
            std::fprintf(stderr, "Round-trip verification failed: %s\n", error.c_str());
            return 5;
        }

        std::fprintf(stderr, "Validated %zu objects, %zu warnings, %d errors; round-trip v%d OK\n",
            source.objects.size(), issues.size() - static_cast<std::size_t>(errors), errors,
            roundtrip.version);
        return errors == 0 ? 0 : 6;
    }
}

LRESULT CALLBACK window_procedure(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	framework* p{ reinterpret_cast<framework*>(GetWindowLongPtr(hwnd, GWLP_USERDATA)) };
	return p ? p->handle_message(hwnd, msg, wparam, lparam) : DefWindowProc(hwnd, msg, wparam, lparam);
}

int WINAPI WinMain(_In_ HINSTANCE instance, _In_opt_  HINSTANCE prev_instance, _In_ LPSTR cmd_line, _In_ int cmd_show)
{
    const int validation_result = RunHeadlessSceneValidation(cmd_line);
    if (validation_result >= 0) return validation_result;
    const int landscape_validation_result = RunHeadlessLandscapeValidation(cmd_line);
    if (landscape_validation_result >= 0) return landscape_validation_result;
    const int prefab_validation_result = RunHeadlessPrefabValidation(cmd_line);
    if (prefab_validation_result >= 0) return prefab_validation_result;

    // WICの画像読み込みはCOMを使うため、エンジンの生存期間中は初期化状態を維持する。
    // シーン切り替え後もWICファクトリを確実に利用できるようにする。
	const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    // 相対パスの基準を実行ファイルの場所へ統一する。
    // Visual Studioの作業ディレクトリ設定に依存せず、直接起動でも同じ動作にする。
	std::array<wchar_t, 32768> executable_path{};
	const DWORD path_length = GetModuleFileNameW(nullptr, executable_path.data(),
		static_cast<DWORD>(executable_path.size()));
	if (path_length > 0 && path_length < executable_path.size())
	{
		std::wstring directory(executable_path.data(), path_length);
		const size_t separator = directory.find_last_of(L"\\/");
		if (separator != std::wstring::npos)
		{
			directory.resize(separator);
			const std::wstring packaged_resources = directory + L"\\resources";
			const DWORD attributes = GetFileAttributesW(packaged_resources.c_str());
			if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
			{
        // Visual Studioの配置は「プロジェクト/x64/構成/3dgp.exe」となる。
				directory += L"\\..\\..";
			}
			SetCurrentDirectoryW(directory.c_str());
		}
	}

	srand(static_cast<unsigned int>(time(nullptr)));

#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	WNDCLASSEXW wcex{};
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = window_procedure;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = instance;
	wcex.hIcon = 0;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = APPLICATION_NAME;
	wcex.hIconSm = 0;
	RegisterClassExW(&wcex);

	RECT rc{ 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT };
	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
	HWND hwnd = CreateWindowExW(0, APPLICATION_NAME, L"", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
		CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
		NULL, NULL, instance, NULL);
	ShowWindow(hwnd, cmd_show);

	framework framework(hwnd);
	SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&framework));
	const int exit_code = framework.run();
	if (SUCCEEDED(com_result)) CoUninitialize();
	return exit_code;
}
