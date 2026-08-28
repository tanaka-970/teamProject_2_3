#include "CSharpScriptValidation.h"

#include "../CSharp/CSharpProject.h"
#include "../CSharp/CSharpScriptBackend.h"
#include "../Core/ScriptComponent.h"
#include "../Core/ScriptRuntime.h"
#include "../../Assets/AssetDatabase.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Object/Registry/BuiltInComponents.h"
#include "../../Reflection/Registry/PropertyRegistry.h"
#include "../../Runtime/API/RuntimeContext.h"
#include "../../Runtime/Events/EventBus.h"
#include "../../Scene/Runtime/Scene.h"
#include "../../Scene/Serialization/PrefabSerializer.h"
#include "../../Scene/Serialization/SceneData.h"
#include "../../Scene/Serialization/SceneSerializer.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

namespace ReplayEngine::Scripting::Validation
{
    namespace
    {
        constexpr const char* validation_guid_text =
            "c5a9c4a3d7914bb5a0b64b68de81d7f1";
        constexpr const char* secondary_validation_guid_text =
            "d6b0d5b4e8024cc6b1c75c79ef92e802";
        constexpr const char* validation_class_name = "ValidationCSharpBehaviour";
        constexpr const char* validation_namespace = "ValidationScripts";

        class Checker final
        {
        public:
            explicit Checker(int first_code) : next_code_(first_code) {}

            void Expect(bool condition, const char* what)
            {
                const int code = next_code_++;
                ++total_;
                if (condition) return;
                ++failures_;
                if (first_failure_ == 0) first_failure_ = code;
                std::fprintf(stderr, "  [FAIL %d] %s\n", code, what);
            }

            int Report(const char* title) const
            {
                if (first_failure_ == 0)
                {
                    std::fprintf(stderr, "%s OK: %d checks passed\n", title, total_);
                    return 0;
                }
                std::fprintf(stderr, "%s FAILED: %d/%d checks failed (first=%d)\n",
                    title, failures_, total_, first_failure_);
                return first_failure_;
            }

        private:
            int next_code_ = 0;
            int first_failure_ = 0;
            int total_ = 0;
            int failures_ = 0;
        };

        std::string ReadText(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream) return std::string();
            return std::string((std::istreambuf_iterator<char>(stream)),
                std::istreambuf_iterator<char>());
        }

        bool WriteText(const std::filesystem::path& path, const std::string& text)
        {
            std::error_code error;
            std::filesystem::create_directories(path.parent_path(), error);
            if (error) return false;
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            if (!stream) return false;
            stream << text;
            return static_cast<bool>(stream);
        }

        class ScopedFileRestore final
        {
        public:
            explicit ScopedFileRestore(std::filesystem::path path)
                : path_(std::move(path))
            {
                std::error_code error;
                existed_ = std::filesystem::exists(path_, error) && !error;
                if (existed_) original_ = ReadText(path_);
            }

            ~ScopedFileRestore()
            {
                std::error_code error;
                if (existed_) WriteText(path_, original_);
                else std::filesystem::remove(path_, error);
            }

            ScopedFileRestore(const ScopedFileRestore&) = delete;
            ScopedFileRestore& operator=(const ScopedFileRestore&) = delete;

        private:
            std::filesystem::path path_;
            std::string original_;
            bool existed_ = false;
        };

        std::string ValidBehaviourSource(const std::string& class_name = validation_class_name)
        {
            return std::string(
                "using ReplayEngine;\n"
                "\n"
                "namespace ") + validation_namespace + ";\n"
                "\n"
                "public enum ValidationMode { First = 0, Second = 1, Third = 2 }\n"
                "[System.Serializable] public struct ValidationSettings\n"
                "{\n"
                "    public int Lives;\n"
                "    public Vector3 Spawn;\n"
                "}\n"
                "\n"
                "[ReplayGuid(\"" + validation_guid_text + "\")]\n"
                "public sealed class " + class_name + " : ScriptBehaviour\n"
                "{\n"
                "    public float Speed = 2.5f;\n"
                "    public int Counter = 7;\n"
                "    public ObjectReference Target;\n"
                "    public ComponentReference TargetComponent;\n"
                "    public string LastEventType = string.Empty;\n"
                "    public int ApiChecks = 0;\n"
                "    public int RuntimeChecks = 0;\n"
                "    public int TypedEventChecks = 0;\n"
                "    [Range(0.0, 10.0)] public float RangedValue = 1.0f;\n"
                "    [Tooltip(\"説明文\")] [Header(\"見出し\")] public int Described = 3;\n"
                "    [HideInInspector] public int Hidden = 5;\n"
                "    [AssetType(\"Image\")] public AssetReference Picture;\n"
                "    public AssetReference<SceneAsset> NextScene = new(\"typed-scene-guid\");\n"
                "    public ValidationMode Mode = ValidationMode.Second;\n"
                "    public int[] Scores = new[] { 1, 2, 3 };\n"
                "    public System.Collections.Generic.List<string> Tags = new() { \"alpha\", \"日本語\" };\n"
                "    public System.Collections.Generic.Dictionary<string, float> Tuning = new() { [\"speed\"] = 2.5f };\n"
                "    public ValidationSettings Settings = new() { Lives = 3, Spawn = new Vector3(1, 2, 3) };\n"
                "    public AnimationCurve Curve = AnimationCurve.Linear(0, 0, 1, 1);\n"
                "    private EventSubscription subscription;\n"
                "\n"
                "    public override void Awake()\n"
                "    {\n"
                "        Counter += 1;\n"
                "        var result = SubscribeEvent(\"a1000000000000000000000000000006\");\n"
                "        if (result.Succeeded) subscription = result.Value;\n"
                "        if (Runtime.InputUnavailable() != RuntimeStatus.ServiceUnavailable) Counter = -100;\n"
                "        if (Runtime.AudioUnavailable() != RuntimeStatus.ServiceUnavailable) Counter = -101;\n"
                "        if (Runtime.RuntimeUIUnavailable() != RuntimeStatus.ServiceUnavailable) Counter = -102;\n"
                "        if (Runtime.SaveGameUnavailable() != RuntimeStatus.ServiceUnavailable) Counter = -103;\n"
                "        ApiChecks = RunComponentApiChecks();\n"
                "        StartCoroutine(CountUp());\n"
                "        After(0.0f, () => { RuntimeChecks += 1; });\n"
                "        TweenValue(0.0f, 10.0f, 0.0f, v => { if (v >= 10.0f) RuntimeChecks += 1; });\n"
                "        RuntimeChecks += RunRuntimeApiChecks();\n"
                "    }\n"
                "\n"
                "    private System.Collections.IEnumerator CountUp()\n"
                "    {\n"
                "        RuntimeChecks += 1;\n"
                "        yield return null;\n"
                "    }\n"
                "\n"
                "    // v11 で足した入力 / Scene / イベント定数を確かめる。\n"
                "    private int RunRuntimeApiChecks()\n"
                "    {\n"
                "        var passed = 0;\n"
                "        // Input Service 未接続でも例外にならず false を返す。\n"
                "        if (!Input.GetKey(Key.A)) ++passed;\n"
                "        if (!Input.GamepadConnected()) ++passed;\n"
                "        if (Input.MouseScrollDelta == 0.0f) ++passed;\n"
                "        if (Runtime.InputHeld(InputActions.Jump).Status == RuntimeStatus.ServiceUnavailable) ++passed;\n"
                "        if (Input.GetAxis(new InputAxisId(\"\")).Status == RuntimeStatus.InvalidArgument) ++passed;\n"
                "        if (EngineEventIds.CollisionEnter.Length == 32) ++passed;\n"
                "        if (EngineEventIds.ButtonClicked.Length == 32) ++passed;\n"
                "        if (Vector3.Cross(Vector3.Right, Vector3.Up).Z > 0.99f) ++passed;\n"
                "        var turned = Quaternion.AngleAxis(MathF.PI * 0.5f, Vector3.Up) * Vector3.Forward;\n"
                "        if (turned.X > 0.99f) ++passed;\n"
                "        if (Runtime.OverlapSphere(Vector3.Zero, 1.0f).Status ==\n"
                "            RuntimeStatus.ServiceUnavailable) ++passed;\n"
                "        if (Runtime.CurrentSceneGuid().Status != RuntimeStatus.Ok ||\n"
                "            Runtime.CurrentSceneGuid().Value != null) ++passed;\n"
                "        var spawn = Runtime.InstantiateDeferred(\"missing\",\n"
                "            new Vector3(0.0f, 0.0f, 0.0f), new Vector3(0.0f, 0.0f, 0.0f),\n"
                "            new Vector3(1.0f, 1.0f, 1.0f));\n"
                "        if (!spawn.Succeeded) ++passed;\n"
                "        if (!Runtime.TakeSpawnResult(0).Succeeded) ++passed;\n"
                "        var transition = Runtime.SceneTransition;\n"
                "        if (transition.Succeeded && !transition.Value.InProgress &&\n"
                "            transition.Value.Status == RuntimeStatus.ServiceUnavailable) ++passed;\n"
                "        return passed;\n"
                "    }\n"
                "\n"
                "    // v10 で足した型付き Component API を実行時に確かめる。\n"
                "    private int RunComponentApiChecks()\n"
                "    {\n"
                "        var passed = 0;\n"
                "        var ownBehaviour = GetBehaviour<ValidationCSharpBehaviour>();\n"
                "        if (ownBehaviour.Succeeded && object.ReferenceEquals(ownBehaviour.Value, this)) ++passed;\n"
                "        if (Runtime.ComponentTypeId(\"CameraComponent\").Value != 0) ++passed;\n"
                "        var cameraType = Runtime.ComponentTypeInfo<CameraComponent>();\n"
                "        if (cameraType.Succeeded && cameraType.Value.TypeId != 0 &&\n"
                "            cameraType.Value.NativeTypeName == \"CameraComponent\") ++passed;\n"
                "        Transform.LocalPosition = new Vector3(1.0f, 2.0f, 3.0f);\n"
                "        if (Transform.LocalPosition.Y == 2.0f) ++passed;\n"
                "        var axes = Transform.Forward;\n"
                "        if (axes.Z > 0.99f) ++passed;\n"
                "        var made = Runtime.CreateGameObject(\"ApiProbe\");\n"
                "        if (!made.Succeeded) return passed;\n"
                "        var addedCamera = Runtime.AddComponent<CameraComponent>(made.Value);\n"
                "        if (addedCamera.Succeeded) ++passed;\n"
                "        var camera = addedCamera.Value;\n"
                "        camera.FieldOfView = 42.0f;\n"
                "        if (camera.FieldOfView == 42.0f) ++passed;\n"
                "        if (Runtime.TryGetComponent<CameraComponent>(made.Value, out var found) &&\n"
                "            found.FieldOfView == 42.0f) ++passed;\n"
                "        if (!Runtime.HasComponent<RigidbodyComponent>(made.Value)) ++passed;\n"
                "        var body = Runtime.AddComponentOrDefault<RigidbodyComponent>(made.Value);\n"
                "        if (body.IsValid && body.SetVelocity(new Vector3(0.0f, 5.0f, 0.0f)) ==\n"
                "            RuntimeStatus.Ok && body.Velocity.Y == 5.0f) ++passed;\n"
                "        if (body.IsValid && body.AddForce(new Vector3(0.0f, 1.0f, 0.0f)) ==\n"
                "            RuntimeStatus.Ok) ++passed;\n"
                "        var runtimeTransform = Runtime.Transform(made.Value);\n"
                "        runtimeTransform.Position = new Vector3(4.0f, 5.0f, 6.0f);\n"
                "        if (runtimeTransform.Position.Z == 6.0f) ++passed;\n"
                "        var animator = Runtime.AddComponentOrDefault<AnimatorComponent>(made.Value);\n"
                "        if (animator.IsValid) ++passed;\n"
                "        if (animator.IsValid && animator.Pause() == RuntimeStatus.Ok &&\n"
                "            animator.Resume() == RuntimeStatus.Ok && animator.Stop() == RuntimeStatus.Ok) ++passed;\n"
                "        var particles = Runtime.AddComponentOrDefault<ParticleEmitterComponent>(made.Value);\n"
                "        if (particles.IsValid) ++passed;\n"
                "        if (particles.IsValid && particles.Emit(32) == RuntimeStatus.Ok &&\n"
                "            particles.Stop() == RuntimeStatus.Ok && particles.Play() == RuntimeStatus.Ok &&\n"
                "            particles.Clear() == RuntimeStatus.Ok) ++passed;\n"
                "        var audio = Runtime.AddComponentOrDefault<AudioSourceComponent>(made.Value);\n"
                "        if (audio.IsValid) ++passed;\n"
                "        if (audio.IsValid && audio.Play() == RuntimeStatus.ServiceUnavailable &&\n"
                "            audio.Stop() == RuntimeStatus.Ok) ++passed;\n"
                "        var image = Runtime.AddComponentOrDefault<UIImageComponent>(made.Value);\n"
                "        if (image.IsValid) ++passed;\n"
                "        image.Sprite = \"validation-image-guid\";\n"
                "        if (image.Sprite == \"validation-image-guid\") ++passed;\n"
                "        var slider = Runtime.AddComponentOrDefault<UISliderComponent>(made.Value);\n"
                "        if (slider.IsValid) ++passed;\n"
                "        slider.Minimum = 0.0f; slider.Maximum = 10.0f; slider.Value = 25.0f;\n"
                "        if (slider.Value == 10.0f && slider.NormalizedValue == 1.0f) ++passed;\n"
                "        var imageReference = image.Accessor.Reference();\n"
                "        if (imageReference.Succeeded) slider.FillImage = imageReference.Value;\n"
                "        var resolvedImage = slider.FillImage.Resolve();\n"
                "        if (resolvedImage.Succeeded && resolvedImage.Value.TypeId == image.Handle.TypeId) ++passed;\n"
                "        return passed;\n"
                "    }\n"
                "\n"
                "    public override void OnWorldChanged(SceneEventInfo scene)\n"
                "    {\n"
                "        if (scene.SceneAssetGuid == \"typed-event\" &&\n"
                "            scene.WorldInstance == ulong.MaxValue - 7) ++TypedEventChecks;\n"
                "    }\n"
                "\n"
                "    public override void Update(float deltaTime)\n"
                "    {\n"
                "        var result = PollEvent(subscription);\n"
                "        if (result.Succeeded && !string.IsNullOrEmpty(result.Value.TypeGuid))\n"
                "        {\n"
                "            LastEventType = result.Value.TypeGuid;\n"
                "        }\n"
                "    }\n"
                "}\n";
        }

        std::string MultipleBehaviourSource()
        {
            return ValidBehaviourSource() +
                "\n[ReplayGuid(\"" + std::string(secondary_validation_guid_text) + "\")]\n"
                "public sealed class SecondaryValidationBehaviour : ScriptBehaviour\n"
                "{\n"
                "    public int Value = 2;\n"
                "}\n";
        }

        bool Close(float a, float b)
        {
            return std::fabs(a - b) <= 0.0001f;
        }

        const ScriptTypeDescriptor* FindDescriptor(ScriptRuntime& runtime,
            ScriptTypeID type_id)
        {
            return runtime.Catalog().Find(type_id);
        }

        bool ReloadSchema(ScriptRuntime& runtime, ScriptTypeID type_id)
        {
            runtime.RequestSchemaReload(type_id);
            runtime.ApplyPendingSchemaSwaps(0.0f);
            const bool loaded = static_cast<bool>(runtime.Catalog().FindSchema(type_id));
            if (!loaded)
            {
                const ScriptErrorRecord* error = runtime.Errors().Latest();
                if (error != nullptr)
                    std::fprintf(stderr, "  schema reload: %s\n", error->message.c_str());
            }
            return loaded;
        }

        ScriptComponent* AddManagedScript(Core::GameObject& object,
            const ScriptTypeDescriptor& descriptor)
        {
            auto* script = object.AddComponent<ScriptComponent>();
            if (script != nullptr) script->AssignScriptType(descriptor);
            return script;
        }
    }

    int RunCSharpScriptValidation()
    {
        namespace Assets = ReplayEngine::Assets;
        namespace CSharp = ReplayEngine::Scripting::CSharp;
        namespace Reflection = ReplayEngine::Reflection;
        namespace Serialization = ReplayEngine::Scene::Serialization;

        Checker check(800);
        Core::RegisterBuiltInComponents();

        const std::filesystem::path root = std::filesystem::current_path();
        const std::filesystem::path scripts = CSharp::CSharpProject::ScriptsRoot(root);
        const std::filesystem::path validation_source =
            scripts / "ValidationCSharpBehaviour.cs";
        const std::filesystem::path broken_source =
            scripts / "ValidationBrokenBehaviour.cs";
        const std::filesystem::path validation_folder =
            std::filesystem::path("Saved") / "Validation" / "CSharp";
        const std::filesystem::path validation_db =
            validation_folder / "AssetDatabase.replaydb";
        const std::filesystem::path scene_path =
            validation_folder / "CSharpSceneRoundTrip.replayscene";
        const std::filesystem::path prefab_path =
            validation_folder / "CSharpPrefabRoundTrip.replayprefab";

        std::error_code folder_error;
        std::filesystem::create_directories(validation_folder, folder_error);
        std::ofstream trace(validation_folder / "csharp_validation.log",
            std::ios::binary | std::ios::trunc);
        const auto log = [&trace](const std::string& message)
        {
            if (!trace) return;
            trace << message << '\n';
            trace.flush();
        };
        const auto log_build = [&log](const char* title,
            const CSharp::CSharpBuildResult& result)
        {
            log(std::string(title) + " succeeded=" +
                (result.succeeded ? "true" : "false") +
                " exit=" + std::to_string(result.exit_code));
            if (!result.output_text.empty()) log(result.output_text);
        };

        ScopedFileRestore restore_validation(validation_source);
        ScopedFileRestore restore_broken(broken_source);

        std::string error;
        const bool project_ready = CSharp::CSharpProject::EnsureProjectFiles(root, error);
        check.Expect(project_ready,
            "C# project/solution files are generated");
        if (!project_ready) log("EnsureProjectFiles: " + error);
        check.Expect(std::filesystem::exists(CSharp::CSharpProject::GameScriptsProjectPath(root)),
            "SDK-style game .csproj exists");
        check.Expect(std::filesystem::exists(CSharp::CSharpProject::GameScriptsSolutionPath(root)),
            ".sln exists for Visual Studio");

        CSharp::CSharpBehaviourInfo created;
        const bool created_behaviour = CSharp::CSharpProject::CreateBehaviour(
            root, "ValidationCreatedBehaviour", "ValidationScripts", created, error);
        check.Expect(created_behaviour && std::filesystem::exists(created.source_path),
            "C# Behaviour creation API writes a .cs file");
        if (created_behaviour)
        {
            CSharp::CSharpBehaviourInfo read_created;
            check.Expect(CSharp::CSharpProject::TryReadBehaviourInfo(
                created.source_path, read_created),
                "created C# Behaviour carries a ReplayGuid");
            std::error_code remove_error;
            std::filesystem::remove(created.source_path, remove_error);
        }

        check.Expect(WriteText(validation_source, ValidBehaviourSource()),
            "validation C# Behaviour source is written");

        CSharp::CSharpBehaviourInfo info;
        check.Expect(CSharp::CSharpProject::TryReadBehaviourInfo(validation_source, info),
            "C# Behaviour metadata can be read");
        check.Expect(info.type_guid == validation_guid_text,
            "ReplayGuid is read as the persistent Type GUID");

        const std::string renamed_source = ValidBehaviourSource("RenamedCSharpBehaviour");
        check.Expect(WriteText(validation_source, renamed_source),
            "class rename test source is written");
        CSharp::CSharpBehaviourInfo renamed;
        check.Expect(CSharp::CSharpProject::TryReadBehaviourInfo(validation_source, renamed) &&
            renamed.type_guid == validation_guid_text,
            "class rename does not regenerate Type GUID");

        check.Expect(WriteText(validation_source, MultipleBehaviourSource()),
            "multiple C# Behaviours can share one source file");
        const std::vector<CSharp::CSharpBehaviourInfo> multiple_entries =
            CSharp::CSharpProject::DiscoverBehaviours(root);
        std::size_t same_file_entries = 0;
        bool secondary_found = false;
        for (const CSharp::CSharpBehaviourInfo& entry : multiple_entries)
        {
            if (entry.source_path != validation_source) continue;
            ++same_file_entries;
            if (entry.type_guid == secondary_validation_guid_text &&
                entry.class_name == "SecondaryValidationBehaviour")
                secondary_found = true;
        }
        check.Expect(same_file_entries == 2 && secondary_found,
            "all ReplayGuid Behaviours in one C# file are discovered");
        check.Expect(WriteText(validation_source, ValidBehaviourSource()),
            "validation C# Behaviour source is restored after rename test");

        Assets::AssetDatabase database(validation_db);
        database.Load(error);

        auto runtime = std::make_unique<ScriptRuntime>();
        auto backend_instance = std::make_unique<CSharp::CSharpScriptBackend>(root);
        CSharp::CSharpScriptBackend* backend = backend_instance.get();
        runtime->InstallBackend(std::move(backend_instance));
        const bool runtime_initialized = runtime->Initialize();
        check.Expect(runtime_initialized, "C# ScriptRuntime/backend initializes hostfxr/CoreCLR");
        if (!runtime_initialized && backend != nullptr)
        {
            log("Initialize: " + backend->LastErrorMessage());
            log_build("Initialize build", backend->LastBuildResult());
        }

        backend = dynamic_cast<CSharp::CSharpScriptBackend*>(
            runtime->Backend(ScriptLanguage::CSharp));
        check.Expect(backend != nullptr, "C# backend is installed");

        CSharp::CSharpBuildResult build;
        const bool compile_success = backend != nullptr && backend->CompileAndReload(&build);
        check.Expect(compile_success, "C# compile succeeds");
        if (!compile_success)
        {
            if (backend != nullptr) log("Compile: " + backend->LastErrorMessage());
            log_build("Compile build", build);
        }
        check.Expect(compile_success && std::filesystem::exists(build.output_assembly),
            "compiled C# Assembly exists");

        check.Expect(CSharp::CSharpProject::RefreshCatalog(root, database,
            runtime->Catalog(), error),
            "C# catalog refresh registers ReplayGuid types");

        ScriptTypeID validation_type;
        Reflection::TypeGUID::TryParse(validation_guid_text, validation_type);
        const ScriptTypeDescriptor* descriptor = FindDescriptor(*runtime, validation_type);
        check.Expect(descriptor != nullptr, "C# Behaviour appears in Add Component catalog");
        check.Expect(descriptor != nullptr &&
            descriptor->category == ScriptCategoryName(ScriptLanguage::CSharp),
            "C# Behaviour is categorized for Add Component");
        check.Expect(descriptor != nullptr && descriptor->type_id == validation_type,
            "catalog uses ReplayGuid Type GUID");

        check.Expect(ReloadSchema(*runtime, validation_type),
            "C# serializable fields are described as Inspector schema");
        ScriptFieldSchemaRef schema = runtime->Catalog().FindSchema(validation_type);
        check.Expect(schema && schema->FindBySavedName("field.Speed") != nullptr,
            "float field appears in Inspector schema");
        check.Expect(schema && schema->FindBySavedName("field.Target") != nullptr,
            "ObjectReference field appears in Inspector schema");
        check.Expect(schema && schema->FindBySavedName("field.TargetComponent") != nullptr,
            "ComponentReference field appears in Inspector schema");

        // ---- Inspector 属性と追加した Field 型 -------------------------------
        {
            const ScriptFieldDefinition* ranged =
                schema ? schema->FindBySavedName("field.RangedValue") : nullptr;
            check.Expect(ranged != nullptr && ranged->has_range &&
                ranged->minimum == 0.0 && ranged->maximum == 10.0,
                "[Range] が編集範囲として schema へ載る");

            const ScriptFieldDefinition* described =
                schema ? schema->FindBySavedName("field.Described") : nullptr;
            check.Expect(described != nullptr && !described->tooltip.empty(),
                "[Tooltip] が schema へ載る");
            check.Expect(described != nullptr && !described->category.empty(),
                "[Header] が折り畳み見出しとして schema へ載る");

            const ScriptFieldDefinition* hidden =
                schema ? schema->FindBySavedName("field.Hidden") : nullptr;
            check.Expect(hidden != nullptr && !hidden->visible_in_inspector,
                "[HideInInspector] が Inspector 非表示として載る");
            check.Expect(hidden != nullptr && hidden->serializable,
                "[HideInInspector] でも保存対象は維持する");

            const ScriptFieldDefinition* picture =
                schema ? schema->FindBySavedName("field.Picture") : nullptr;
            check.Expect(picture != nullptr &&
                picture->type == Reflection::PropertyType::AssetReference,
                "AssetReference フィールドが Asset 型として載る");
            check.Expect(picture != nullptr && picture->asset_type == "Image",
                "[AssetType] が Picker の絞り込みとして載る");

            const ScriptFieldDefinition* next_scene =
                schema ? schema->FindBySavedName("field.NextScene") : nullptr;
            check.Expect(next_scene != nullptr &&
                next_scene->type == Reflection::PropertyType::AssetReference &&
                next_scene->asset_type == "Scene" &&
                next_scene->default_value.AsString() == "typed-scene-guid",
                "型付きAssetReferenceが種別と既定GUIDをschemaへ載せる");

            const ScriptFieldDefinition* mode =
                schema ? schema->FindBySavedName("field.Mode") : nullptr;
            check.Expect(mode != nullptr && mode->type == Reflection::PropertyType::Enum,
                "enum フィールドが Enum 型として載る");
            check.Expect(mode != nullptr && mode->enum_labels.size() == 3 &&
                mode->enum_labels[1] == "Second",
                "enum のラベルが並び順どおりに載る");
            check.Expect(mode != nullptr && mode->default_value.AsInt() == 1,
                "enum の既定値が数値として載る");

            const ScriptFieldDefinition* scores =
                schema ? schema->FindBySavedName("field.Scores") : nullptr;
            check.Expect(scores != nullptr && scores->type == Reflection::PropertyType::Array &&
                scores->array_element_type == Reflection::PropertyType::Int &&
                scores->default_value.ArrayElements().size() == 3,
                "配列フィールドが要素型と既定値を保って schema へ載る");
            const ScriptFieldDefinition* tags =
                schema ? schema->FindBySavedName("field.Tags") : nullptr;
            check.Expect(tags != nullptr && tags->type == Reflection::PropertyType::Array &&
                tags->array_element_type == Reflection::PropertyType::String &&
                tags->default_value.ArrayElements().size() == 2 &&
                tags->default_value.ArrayElements()[1].AsString() == "日本語",
                "List と UTF-8 文字列が配列として schema へ載る");
            const ScriptFieldDefinition* tuning =
                schema ? schema->FindBySavedName("field.Tuning") : nullptr;
            const ScriptFieldDefinition* settings =
                schema ? schema->FindBySavedName("field.Settings") : nullptr;
            const ScriptFieldDefinition* curve =
                schema ? schema->FindBySavedName("field.Curve") : nullptr;
            check.Expect(tuning != nullptr && settings != nullptr && curve != nullptr &&
                tuning->type == Reflection::PropertyType::String &&
                settings->type == Reflection::PropertyType::String &&
                curve->type == Reflection::PropertyType::String,
                "Dictionary・Serializable struct・AnimationCurve が JSON 保存型として載る");
        }

        if (backend != nullptr && schema)
        {
            ScriptInstanceRequest request;
            request.type_id = validation_type;
            const ScriptInstanceHandle instance = backend->CreateInstance(request);
            check.Expect(instance != invalid_script_instance_handle,
                "managed C# instance can be created without native pointers");

            const bool set_speed = backend->SetField(instance, "field.Speed",
                ScriptValue::MakeFloat(9.25f));
            ScriptValue pulled_speed;
            const bool got_speed = backend->GetField(instance, "field.Speed", pulled_speed);
            check.Expect(set_speed && got_speed && Close(pulled_speed.AsFloat(), 9.25f),
                "float field can be pushed and pulled");

            const Core::ObjectID target_id(42);
            const bool set_object = backend->SetField(instance, "field.Target",
                ScriptValue::MakeObjectReference(target_id));
            ScriptValue pulled_object;
            const bool got_object = backend->GetField(instance, "field.Target", pulled_object);
            check.Expect(set_object && got_object &&
                pulled_object.AsObjectReference() == target_id,
                "ObjectReference field can be pushed and pulled");

            Reflection::ComponentReference component_reference;
            component_reference.owner = Core::ObjectID(43);
            component_reference.component = 77;
            const bool set_component = backend->SetField(instance, "field.TargetComponent",
                ScriptValue::MakeComponentReference(component_reference));
            ScriptValue pulled_component;
            const bool got_component = backend->GetField(instance,
                "field.TargetComponent", pulled_component);
            check.Expect(set_component && got_component &&
                pulled_component.AsComponentReference() == component_reference,
                "ComponentReference field can be pushed and pulled");

            const bool set_scene_reference = backend->SetField(instance,
                "field.NextScene", ScriptValue::MakeAssetReference("scene-guid-updated"));
            ScriptValue pulled_scene_reference;
            const bool got_scene_reference = backend->GetField(instance,
                "field.NextScene", pulled_scene_reference);
            const bool scene_reference_ok = set_scene_reference && got_scene_reference &&
                pulled_scene_reference.AsString() == "scene-guid-updated";
            if (!scene_reference_ok)
            {
                // 失敗時だけ実値を出す。set/get のどちらで落ちたかを切り分ける。
                std::fprintf(stderr, "  [DIAG] NextScene set=%d get=%d value=%s\n",
                    set_scene_reference ? 1 : 0, got_scene_reference ? 1 : 0,
                    pulled_scene_reference.AsString().c_str());
            }
            check.Expect(scene_reference_ok,
                "型付きAssetReferenceをmanaged instanceと双方向に同期できる");

            std::vector<ScriptValue> score_values;
            score_values.push_back(ScriptValue::MakeInt(7));
            score_values.push_back(ScriptValue::MakeInt(9));
            const bool set_scores = backend->SetField(instance, "field.Scores",
                ScriptValue::MakeArray(Reflection::PropertyType::Int,
                    std::move(score_values)));
            ScriptValue pulled_scores;
            const bool got_scores = backend->GetField(instance, "field.Scores", pulled_scores);
            check.Expect(set_scores && got_scores && pulled_scores.IsArray() &&
                pulled_scores.ArrayElements().size() == 2 &&
                pulled_scores.ArrayElements()[1].AsInt() == 9,
                "配列フィールドを managed instance と双方向に同期できる");

            const std::string settings_json =
                "{\"Lives\":8,\"Spawn\":{\"X\":4,\"Y\":5,\"Z\":6}}";
            const bool set_settings = backend->SetField(instance, "field.Settings",
                ScriptValue::MakeString(settings_json));
            ScriptValue pulled_settings;
            const bool got_settings = backend->GetField(instance,
                "field.Settings", pulled_settings);
            check.Expect(set_settings && got_settings &&
                pulled_settings.AsString().find("\"Lives\":8") != std::string::npos,
                "Serializable struct の JSON を managed instance と双方向に同期できる");

            backend->DestroyInstance(instance);
            check.Expect(backend->LiveInstanceCount() == 0,
                "managed instance is released after DestroyInstance");
        }

        check.Expect(WriteText(broken_source,
            "using ReplayEngine;\nnamespace ValidationScripts;\npublic sealed class ValidationBrokenBehaviour : ScriptBehaviour { syntax error }\n"),
            "broken C# source is written");
        CSharp::CSharpBuildResult failed_build;
        const bool failed_reload = backend != nullptr &&
            backend->CompileAndReload(&failed_build);
        check.Expect(!failed_reload && !failed_build.succeeded,
            "C# compile failure is reported");
        log_build("Intentional failed build", failed_build);
        check.Expect(backend != nullptr && backend->AssemblyLoaded(),
            "old Assembly remains loaded after compile failure");
        check.Expect(backend != nullptr && backend->CanInstantiate(validation_type),
            "old TypeState remains usable after compile failure");
        std::error_code remove_error;
        std::filesystem::remove(broken_source, remove_error);

        const bool recompile_success = backend != nullptr &&
            backend->CompileAndReload(&build);
        check.Expect(recompile_success,
            "C# compile succeeds again after fixing errors");
        if (!recompile_success)
        {
            if (backend != nullptr) log("Recompile: " + backend->LastErrorMessage());
            log_build("Recompile build", build);
        }
        bool reloads_ok = backend != nullptr;
        if (backend != nullptr)
        {
            for (int index = 0; index < 100; ++index)
            {
                if (!backend->ReloadLastBuiltAssembly())
                {
                    reloads_ok = false;
                    break;
                }
            }
        }
        check.Expect(reloads_ok, "Assembly reload succeeds 100 times");
        check.Expect(ReloadSchema(*runtime, validation_type),
            "schema can be restored after repeated reloads");

        ReplayEngine::Scene::Scene world("CSharpValidationScene");
        world.Services().SetScripts(runtime.get());
        Core::GameObject* carrier = world.CreateGameObject("Carrier");
        Core::GameObject* target = world.CreateGameObject("Target");
        check.Expect(carrier != nullptr && target != nullptr,
            "validation Scene objects can be created");

        ScriptComponent* script = (carrier != nullptr && descriptor != nullptr)
            ? AddManagedScript(*carrier, *descriptor) : nullptr;
        check.Expect(script != nullptr, "C# ScriptComponent can be added to a Scene");
        if (script != nullptr && target != nullptr)
        {
            script->WriteField("field.Speed", ScriptValue::MakeFloat(4.5f));
            script->WriteField("field.Target",
                ScriptValue::MakeObjectReference(target->ID()));
            Reflection::ComponentReference reference;
            reference.owner = carrier->ID();
            reference.component = script->StableID();
            script->WriteField("field.TargetComponent",
                ScriptValue::MakeComponentReference(reference));
            std::vector<ScriptValue> scores;
            scores.push_back(ScriptValue::MakeInt(11));
            scores.push_back(ScriptValue::MakeInt(22));
            script->WriteField("field.Scores", ScriptValue::MakeArray(
                Reflection::PropertyType::Int, std::move(scores)));
            script->WriteField("field.Settings", ScriptValue::MakeString(
                "{\"Lives\":6,\"Spawn\":{\"X\":7,\"Y\":8,\"Z\":9}}"));
        }

        Serialization::SceneData data;
        Serialization::CaptureScene(world, data);
        check.Expect(!data.objects.empty(), "Scene captures C# ScriptComponent data");
        check.Expect(Serialization::SceneSerializer::SaveToFile(data, scene_path, error),
            "Scene with C# Behaviour saves to disk");
        Serialization::SceneData loaded_data;
        check.Expect(Serialization::SceneSerializer::LoadFromFile(
            loaded_data, scene_path, error),
            "Scene with C# Behaviour reloads from disk");

        ReplayEngine::Scene::Scene restored("CSharpRestoredScene");
        restored.Services().SetScripts(runtime.get());
        Serialization::SceneLoadReport report;
        check.Expect(Serialization::ApplySceneData(loaded_data, restored, report),
            "SceneData restores C# ScriptComponent");
        Core::GameObject* restored_carrier = carrier != nullptr
            ? restored.FindGameObjectByID(carrier->ID()) : nullptr;
        ScriptComponent* restored_script = restored_carrier != nullptr
            ? restored_carrier->GetComponent<ScriptComponent>() : nullptr;
        check.Expect(restored_script != nullptr &&
            restored_script->ScriptType() == validation_type,
            "restored C# ScriptComponent keeps Type GUID");
        check.Expect(restored_script != nullptr &&
            Close(restored_script->ReadField("field.Speed").AsFloat(), 4.5f),
            "restored Scene keeps float field value");
        check.Expect(restored_script != nullptr &&
            restored_script->ReadField("field.Target").AsObjectReference().Valid(),
            "restored Scene keeps ObjectReference value");
        check.Expect(restored_script != nullptr &&
            restored_script->ReadField("field.TargetComponent")
                .AsComponentReference().IsAssigned(),
            "restored Scene keeps ComponentReference value");
        check.Expect(restored_script != nullptr &&
            restored_script->ReadField("field.Scores").ArrayElements().size() == 2 &&
            restored_script->ReadField("field.Scores").ArrayElements()[1].AsInt() == 22,
            "restored Scene keeps array field values");
        check.Expect(restored_script != nullptr &&
            restored_script->ReadField("field.Settings").AsString().find("\"Lives\":6") !=
                std::string::npos,
            "restored Scene keeps structured JSON field values");

        if (carrier != nullptr)
        {
            check.Expect(Serialization::PrefabSerializer::Save(
                world, carrier->ID(), prefab_path, error),
                "Prefab with C# Behaviour saves");
            ReplayEngine::Scene::Scene prefab_world("CSharpPrefabWorld");
            prefab_world.Services().SetScripts(runtime.get());
            Serialization::SceneLoadReport prefab_report;
            const Core::ObjectID prefab_root =
                Serialization::PrefabSerializer::Instantiate(prefab_world,
                    prefab_path, error, &prefab_report, "validation-prefab-guid");
            Core::GameObject* prefab_object =
                prefab_world.FindGameObjectByID(prefab_root);
            ScriptComponent* prefab_script = prefab_object != nullptr
                ? prefab_object->GetComponent<ScriptComponent>() : nullptr;
            check.Expect(prefab_script != nullptr &&
                prefab_script->ScriptType() == validation_type,
                "Prefab Instantiate restores C# ScriptComponent");
            check.Expect(prefab_script != nullptr &&
                Close(prefab_script->ReadField("field.Speed").AsFloat(), 4.5f),
                "Prefab Instantiate keeps C# field values");
            check.Expect(prefab_script != nullptr &&
                prefab_script->ReadField("field.Scores").ArrayElements().size() == 2,
                "Prefab Instantiate keeps C# array field values");
        }

        ReplayEngine::Scene::Scene missing_world("CSharpMissingScene");
        missing_world.Services().SetScripts(nullptr);
        Core::GameObject* missing_object = missing_world.CreateGameObject("Missing");
        ScriptComponent* missing_script = missing_object != nullptr
            ? missing_object->AddComponent<ScriptComponent>() : nullptr;
        Reflection::PropertyBag missing_input;
        missing_input.Set(ScriptNames::language,
            ScriptValue::MakeEnum(static_cast<int>(ScriptLanguage::CSharp)));
        missing_input.Set(ScriptNames::asset,
            ScriptValue::MakeString("missing-asset-guid"));
        missing_input.Set(ScriptNames::class_name,
            ScriptValue::MakeString("Missing.Namespace.Type"));
        missing_input.Set(ScriptNames::type_id,
            ScriptValue::MakeString(validation_type.ToString()));
        missing_input.Set("field.Speed", ScriptValue::MakeFloat(12.0f));
        if (missing_script != nullptr)
        {
            Reflection::PropertyRegistry::Apply(*missing_script, missing_input);
            Reflection::PropertyBag missing_output;
            Reflection::PropertyRegistry::Capture(*missing_script, missing_output);
            const Reflection::PropertyValue* kept_type =
                missing_output.Find(ScriptNames::type_id);
            const Reflection::PropertyValue* kept_speed =
                missing_output.Find("field.Speed");
            check.Expect(kept_type != nullptr &&
                kept_type->AsString() == validation_type.ToString(),
                "Missing C# Behaviour keeps Type GUID");
            check.Expect(kept_speed != nullptr && Close(kept_speed->AsFloat(), 12.0f),
                "Missing C# Behaviour keeps serialized field data");
        }
        else
        {
            check.Expect(false, "Missing C# ScriptComponent can be created");
            check.Expect(false, "Missing C# Behaviour keeps Type GUID");
            check.Expect(false, "Missing C# Behaviour keeps serialized field data");
        }

        // Behaviour から Runtime API を触れるよう、Play の直前だけ Context を繋ぐ。
        // ここより前で繋ぐと EditorOnly Component が Scene 復元から外れ、
        // 上の直列化チェックの前提が変わってしまう。
        ReplayEngine::Runtime::RuntimeContext runtime_context(world);
        world.Services().SetRuntime(&runtime_context);

        runtime->OnWorldActivating(world);
        world.Start();
        world.Update(0.016f);
        check.Expect(script == nullptr || script->HasInstance(),
            "Play start creates managed instance through Behaviour lifecycle");

        // v10 の型付き Component API を、実際に動いている managed インスタンスから確かめる。
        // ReadField は Play 中なら managed 側の今の値を返す。
        if (script != nullptr && script->HasInstance())
        {
            const ScriptValue api_checks =
                script->ReadField(ScriptNames::MakeFieldSavedName("ApiChecks"));
            if (api_checks.AsInt() != 23)
            {
                // 失敗時だけ実値を出す。何個目で止まったかの手がかりにする。
                std::fprintf(stderr, "  [DIAG] ApiChecks=%lld (expected 23)\n",
                    static_cast<long long>(api_checks.AsInt()));
            }
            check.Expect(api_checks.AsInt() == 23,
                "typed Component API works from a running C# behaviour");

            // Coroutine 1 + Timer 1 + Tween 1 + 生入力/Scene/Math/Physics 14 = 17。
            const ScriptValue runtime_checks =
                script->ReadField(ScriptNames::MakeFieldSavedName("RuntimeChecks"));
            check.Expect(runtime_checks.AsInt() == 17,
                "coroutine / timer / tween / input / scene API run from C#");

            Runtime::EventRecord typed_event;
            typed_event.type = Runtime::EngineEvents::WorldChanged;
            typed_event.type_name = "WorldChanged";
            typed_event.payload.Set("scene_guid",
                Reflection::PropertyValue::MakeString("typed-event"));
            typed_event.payload.Set("world_instance",
                Reflection::PropertyValue::MakeUInt64(
                    (std::numeric_limits<std::uint64_t>::max)() - 7));
            Runtime::EventBus::Global().Publish(std::move(typed_event));
            Runtime::EventBus::Global().Dispatch(nullptr);
            world.Update(0.016f);
            const ScriptValue typed_event_checks =
                script->ReadField(ScriptNames::MakeFieldSavedName("TypedEventChecks"));
            check.Expect(typed_event_checks.AsInt() == 1,
                "Global Event と UInt64 payload が型付き C# callback へ届く");
        }
        else
        {
            check.Expect(false, "typed Component API works from a running C# behaviour");
        }
        runtime->OnWorldUnloading(world);
        world.Clear();
        runtime->OnWorldUnloaded(world);
        check.Expect(runtime->LastLeakedInstanceCount() == 0 &&
            (backend == nullptr || backend->LiveInstanceCount() == 0),
            "Play stop destroys managed instances");

        runtime->Shutdown();
        return check.Report("csharp-scripting");
    }
}
