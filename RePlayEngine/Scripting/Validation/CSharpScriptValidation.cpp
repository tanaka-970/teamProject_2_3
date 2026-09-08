#include "CSharpScriptValidation.h"

#include "../CSharp/CSharpProject.h"
#include "../CSharp/CSharpScriptBackend.h"
#include "../Core/ScriptComponent.h"
#include "../Core/ScriptRuntime.h"
#include "../../Assets/AssetDatabase.h"
#include "../../Components/Landscape/LandscapeComponent.h"
#include "../../Components/Physics/BoxColliderComponent.h"
#include "../../Components/Physics/RigidbodyComponent.h"
#include "../../Components/Physics/SphereColliderComponent.h"
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
#include <functional>
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
                Expect(condition, what, std::string());
            }

            // 失敗した理由まで出す版。
            //
            // 条件式だけを出しても「何が入っていたのか」が分からず、
            // 落ちた項目を再現するのに毎回デバッガが要る。
            // 期待値と実際の値をその場で並べる。
            void Expect(bool condition, const char* what, const std::string& detail)
            {
                const int code = next_code_++;
                ++total_;
                if (condition) return;
                ++failures_;
                if (first_failure_ == 0) first_failure_ = code;
                std::fprintf(stderr, "  [FAIL %d] %s\n", code, what);
                if (!detail.empty()) std::fputs(detail.c_str(), stderr);
            }

            void ExpectEqual(int actual, int expected, const char* what)
            {
                Expect(actual == expected, what,
                    "      expected: " + std::to_string(expected) +
                    "\n      actual:   " + std::to_string(actual) + "\n");
            }

            void ExpectClose(float actual, float expected, const char* what)
            {
                Expect(std::fabs(actual - expected) <= 0.0001f, what,
                    "      expected: " + std::to_string(expected) +
                    "\n      actual:   " + std::to_string(actual) + "\n");
            }

            void ExpectText(const std::string& actual, const std::string& expected,
                const char* what)
            {
                Expect(actual == expected, what,
                    "      expected: \"" + expected + "\"\n      actual:   \"" +
                    actual + "\"\n");
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
                "    // Legacy の Coroutine が「Component 無効中は進まない」ままかを見る。\n"
                "    public int LoopSteps = 0;\n"
                "    private System.Collections.IEnumerator LoopForever()\n"
                "    {\n"
                "        while (true) { yield return null; ++LoopSteps; }\n"
                "    }\n"
                "\n"
                "    public override void Start()\n"
                "    {\n"
                "        StartCoroutine(LoopForever());\n"
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

        // ---- MonoBehaviour 検証用の一時スクリプト ----------------------------
        //
        // 手で置いたファイルには依存しない。--validate-monobehaviour が
        // 実行のたびに書き出し、終わったら ScopedFileRestore が元へ戻す。
        constexpr const char* authored_guid_text =
            "a7c1e93b52f04d18b6c25f8a4d3e7b10";
        constexpr const char* observer_guid_text =
            "b8d2fa4c63015e29c7d36f9b5e4f8c21";
        constexpr const char* added_guid_text =
            "c9e3ab5d74126f3ad8e47fac6f5a9d32";
        constexpr const char* coroutine_guid_text =
            "d0f4bc6e85237a4be9f58abd7a6bae43";
        constexpr const char* adder_guid_text =
            "e1a5cd7f96348b5cfa069bce8b7cbf54";

        std::string MonoBehaviourSource()
        {
            return std::string(
                "using System.Collections;\n"
                "using ReplayEngine;\n"
                "\n"
                "namespace ValidationScripts;\n"
                "\n"
                "// --validate-monobehaviour が書き出す検証用スクリプト。\n"
                "// 検証が終わると元の内容へ戻す。手で編集しても失われない。\n"
                "[ReplayGuid(\"") + authored_guid_text + "\")]\n"
                "public class ValidationMonoBehaviour : MonoBehaviour\n"
                "{\n"
                "    [SerializeField] float moveSpeed = 5.0f;\n"
                "    [SerializeField] private float jumpPower = 8.0f;\n"
                "\n"
                "    public int AwakeCalls;\n"
                "    public int StartCalls;\n"
                "    public int UpdateCalls;\n"
                "    public int LateUpdateCalls;\n"
                "    public int FixedUpdateCalls;\n"
                "    public int EnableCalls;\n"
                "    public int DisableCalls;\n"
                "\n"
                "    [HideInInspector] public float LastDeltaTime;\n"
                "    [HideInInspector] public string ObserverName = \"\";\n"
                "\n"
                "    // Inspector 参照の round-trip。\n"
                "    public GameObject? RefObject;\n"
                "    public Rigidbody? RefBody;\n"
                "    public ValidationMonoBehaviourObserver? RefObserver;\n"
                "    public Component? RefComponent;\n"
                "\n"
                "    // 復元できたかを Native から読める形へ写した結果。\n"
                "    public string ResolvedObjectName = \"\";\n"
                "    public float ResolvedBodyMass = -1.0f;\n"
                "    public string ResolvedObserverLabel = \"\";\n"
                "    public string ResolvedComponentLabel = \"\";\n"
                "\n"
                "    public int WaitDone;\n"
                "    public int CollisionCalls;\n"
                "    public int TriggerCalls;\n"
                "    public int CollisionFrame = -1;\n"
                "    public int UpdateFrame = -1;\n"
                "    public int TriggerFrame = -1;\n"
                "    public int ColliderKind;\n"
                "    public int ColliderMissing = -1;\n"
                "    public int TriggerColliderKind;\n"
                "\n"
                "    public int AddedNonNull;\n"
                "    public int AddedSameInstance;\n"
                "    public int AddedFieldValue = -1;\n"
                "    public int AddedAwakeAtAdd = -1;\n"
                "    public int AddedAwakeLater = -1;\n"
                "\n"
                "    public int BodyDead = -1;\n"
                "    public int OwnerAlive = -1;\n"
                "    public int DeadMassRead;\n"
                "    public int TempObjectDead = -1;\n"
                "\n"
                "    public float TimeScaleSeen = -1.0f;\n"
                "\n"
                "    // 検証側から書き込む指示。Update で 1 回だけ実行して 0 へ戻す。\n"
                "    public int Command;\n"
                "\n"
                "    Rigidbody? body;\n"
                "    GameObject? tempObject;\n"
                "    ValidationAddedBehaviour? added;\n"
                "\n"
                "    void Awake()\n"
                "    {\n"
                "        ++AwakeCalls;\n"
                "        body = GetComponent<Rigidbody>();\n"
                "    }\n"
                "\n"
                "    void OnEnable() => ++EnableCalls;\n"
                "    void OnDisable() => ++DisableCalls;\n"
                "\n"
                "    void Start()\n"
                "    {\n"
                "        ++StartCalls;\n"
                "        Publish();\n"
                "    }\n"
                "\n"
                "    void Update()\n"
                "    {\n"
                "        ++UpdateCalls;\n"
                "        LastDeltaTime = Time.deltaTime;\n"
                "        TimeScaleSeen = Time.timeScale;\n"
                "        UpdateFrame = Time.frameCount;\n"
                "\n"
                "        var observer = GetComponent<ValidationMonoBehaviourObserver>();\n"
                "        ObserverName = observer != null ? observer.Label : \"\";\n"
                "\n"
                "        Publish();\n"
                "        if (Command != 0) { var command = Command; Command = 0; Run(command); }\n"
                "\n"
                "        if (Input.GetKeyDown(KeyCode.Space) && body != null)\n"
                "        {\n"
                "            body.AddForce(Vector3.Up * jumpPower);\n"
                "        }\n"
                "    }\n"
                "\n"
                "    void FixedUpdate() => ++FixedUpdateCalls;\n"
                "    void LateUpdate() => ++LateUpdateCalls;\n"
                "\n"
                "    void OnCollisionEnter(Collision collision)\n"
                "    {\n"
                "        ++CollisionCalls;\n"
                "        CollisionFrame = Time.frameCount;\n"
                "        var hit = collision.collider;\n"
                "        ColliderMissing = hit == null ? 1 : 0;\n"
                "        ColliderKind = KindOf(hit);\n"
                "    }\n"
                "\n"
                "    void OnTriggerEnter(Collider other)\n"
                "    {\n"
                "        ++TriggerCalls;\n"
                "        TriggerFrame = Time.frameCount;\n"
                "        TriggerColliderKind = KindOf(other);\n"
                "    }\n"
                "\n"
                "    // 同じ GameObject に付いた 2 つの Collider を型で見分ける。\n"
                "    // 名前では区別できないので、Box と Sphere を別の値にする。\n"
                "    static int KindOf(Collider? collider)\n"
                "    {\n"
                "        if (collider == null) return 0;\n"
                "        if (collider is BoxCollider) return 1;\n"
                "        if (collider is SphereCollider) return 2;\n"
                "        return 3;\n"
                "    }\n"
                "\n"
                "    void Publish()\n"
                "    {\n"
                "        ResolvedObjectName = RefObject != null ? RefObject.name : \"\";\n"
                "        ResolvedBodyMass = RefBody != null ? RefBody.mass : -1.0f;\n"
                "        ResolvedObserverLabel = RefObserver != null ? RefObserver.Label : \"\";\n"
                "        ResolvedComponentLabel =\n"
                "            RefComponent is ValidationMonoBehaviourObserver typed ? typed.Label : \"\";\n"
                "    }\n"
                "\n"
                "    void Run(int command)\n"
                "    {\n"
                "        switch (command)\n"
                "        {\n"
                "        case 1: StartCoroutine(WaitHalf()); break;\n"
                "        case 2: AddAndCheck(); break;\n"
                "        case 3: DestroyBody(); break;\n"
                "        case 4: RecordDead(); break;\n"
                "        case 5: AssignReferences(); break;\n"
                "        case 6: MakeAndDestroyTemp(); break;\n"
                "        case 7: TempObjectDead = tempObject == null ? 1 : 0; break;\n"
                "        case 8: AddedAwakeLater = added != null ? added.AwakeCalls : -1; break;\n"
                "        // Update の途中で親を切る。OnDisable は次の同期点まで来ないので、\n"
                "        // 「非 Active になったフレームの末尾」に子が 1 歩進まないかを見る。\n"
                "        case 9: GameObject.Find(\"Parent\")?.SetActive(false); break;\n"
                "        case 10: GameObject.Find(\"Parent\")?.SetActive(true); break;\n"
                "        }\n"
                "    }\n"
                "\n"
                "    IEnumerator WaitHalf()\n"
                "    {\n"
                "        yield return new WaitForSeconds(0.5f);\n"
                "        WaitDone = 1;\n"
                "    }\n"
                "\n"
                "    void AddAndCheck()\n"
                "    {\n"
                "        added = gameObject.AddComponent<ValidationAddedBehaviour>();\n"
                "        AddedNonNull = added != null ? 1 : 0;\n"
                "        if (added == null) return;\n"
                "        AddedAwakeAtAdd = added.AwakeCalls;\n"
                "        added.Value = 123;\n"
                "        var again = gameObject.GetComponent<ValidationAddedBehaviour>();\n"
                "        AddedSameInstance = ReferenceEquals(added, again) ? 1 : 0;\n"
                "        AddedFieldValue = again != null ? again.Value : -1;\n"
                "    }\n"
                "\n"
                "    void DestroyBody()\n"
                "    {\n"
                "        RefBody = GetComponent<Rigidbody>();\n"
                "        ReplayEngine.Object.Destroy(RefBody);\n"
                "    }\n"
                "\n"
                "    void RecordDead()\n"
                "    {\n"
                "        BodyDead = RefBody == null ? 1 : 0;\n"
                "        OwnerAlive = gameObject != null ? 1 : 0;\n"
                "        // 破棄済み Component のプロパティを読んでも落ちないこと。\n"
                "        if (RefBody != null) { var unused = RefBody.mass; }\n"
                "        DeadMassRead = 1;\n"
                "    }\n"
                "\n"
                "    void AssignReferences()\n"
                "    {\n"
                "        RefObject = GameObject.Find(\"RefTarget\");\n"
                "        RefBody = GetComponent<Rigidbody>();\n"
                "        var observers = gameObject.GetComponents<ValidationMonoBehaviourObserver>();\n"
                "        RefObserver = observers.Length > 1 ? observers[1] :\n"
                "            (observers.Length > 0 ? observers[0] : null);\n"
                "        RefComponent = RefObserver;\n"
                "        Publish();\n"
                "    }\n"
                "\n"
                "    void MakeAndDestroyTemp()\n"
                "    {\n"
                "        tempObject = GameObject.Create(\"ValidationTemp\");\n"
                "        ReplayEngine.Object.Destroy(tempObject);\n"
                "    }\n"
                "\n"
                "    public float StepDistance() => moveSpeed * Time.deltaTime;\n"
                "}\n"
                "\n"
                "// GetComponent<T>() が Managed Behaviour も同じ入口で引けることの確認用。\n"
                "// 同じ GameObject へ 2 つ付けて、参照が 1 つ目へ化けないかも見る。\n"
                "[ReplayGuid(\"" + observer_guid_text + "\")]\n"
                "public class ValidationMonoBehaviourObserver : MonoBehaviour\n"
                "{\n"
                "    public string Label = \"observer\";\n"
                "}\n"
                "\n"
                "// AddComponent<T>() の戻り値と Lifecycle を見るための型。\n"
                "[ReplayGuid(\"" + added_guid_text + "\")]\n"
                "public class ValidationAddedBehaviour : MonoBehaviour\n"
                "{\n"
                "    public int Value;\n"
                "    public int AwakeCalls;\n"
                "    public int StartCalls;\n"
                "    // Awake より先に Start が来たら 1 になる。来てはいけない。\n"
                "    public int StartBeforeAwake;\n"
                "    void Awake() => ++AwakeCalls;\n"
                "    void Start()\n"
                "    {\n"
                "        ++StartCalls;\n"
                "        if (AwakeCalls == 0) StartBeforeAwake = 1;\n"
                "    }\n"
                "}\n"
                "\n"
                "// Awake の中で AddComponent したときの順序を見る型。\n"
                "[ReplayGuid(\"" + adder_guid_text + "\")]\n"
                "public class ValidationAwakeAdderBehaviour : MonoBehaviour\n"
                "{\n"
                "    public int AddedNonNullInAwake;\n"
                "    public int NotStartedAtAdd;\n"
                "    public int AddedAwakeCalls = -1;\n"
                "    public int AddedStartCalls = -1;\n"
                "    public int AddedStartBeforeAwake = -1;\n"
                "\n"
                "    ValidationAddedBehaviour? child;\n"
                "\n"
                "    void Awake()\n"
                "    {\n"
                "        child = gameObject.AddComponent<ValidationAddedBehaviour>();\n"
                "        AddedNonNullInAwake = child != null ? 1 : 0;\n"
                "        NotStartedAtAdd =\n"
                "            child != null && child.AwakeCalls == 0 && child.StartCalls == 0 ? 1 : 0;\n"
                "    }\n"
                "\n"
                "    void Update()\n"
                "    {\n"
                "        if (child == null) return;\n"
                "        AddedAwakeCalls = child.AwakeCalls;\n"
                "        AddedStartCalls = child.StartCalls;\n"
                "        AddedStartBeforeAwake = child.StartBeforeAwake;\n"
                "    }\n"
                "}\n"
                "\n"
                "// Coroutine の停止条件を見るための型。\n"
                "// 毎フレーム 1 歩進むだけ。Update とは別に数える。\n"
                "[ReplayGuid(\"" + coroutine_guid_text + "\")]\n"
                "public class ValidationCoroutineBehaviour : MonoBehaviour\n"
                "{\n"
                "    public int Steps;\n"
                "    public int UpdateCalls;\n"
                "    public int StartCalls;\n"
                "\n"
                "    void Start() { ++StartCalls; StartCoroutine(Loop()); }\n"
                "    void Update() => ++UpdateCalls;\n"
                "\n"
                "    IEnumerator Loop()\n"
                "    {\n"
                "        while (true) { yield return null; ++Steps; }\n"
                "    }\n"
                "}\n";
        }

        std::string MonoBehaviourBoundarySource()
        {
            const std::string source = R"CS(using System.Collections;
using System.Collections.Generic;
using ReplayEngine;

namespace ValidationScripts;

[ReplayGuid("f3010000000000000000000000000001")]
public class R3Target : MonoBehaviour
{
    public string Label = "target";
    public int AwakeCalls;
    void Awake() => ++AwakeCalls;
}

[ReplayGuid("f3010000000000000000000000000002")]
public class R3Probe : MonoBehaviour
{
    [SerializeField] public R3Target? Target;
    [SerializeField] public List<R3Target?> Targets = new();
    [SerializeField] public R3Target?[] TargetArray = System.Array.Empty<R3Target?>();
    public R3Target? Fallback;
    public R3Target? PendingTarget;
    public R3Target? PendingKeep;
    public R3Target? ExpiredTarget;
    public R3Target? ParentSearchTarget;
    public List<R3Target?> PendingList = new();
    public R3Target?[] PendingArray = System.Array.Empty<R3Target?>();
    public List<R3Target?> EditedList = new();
    public R3Target?[] EditedArray = System.Array.Empty<R3Target?>();
    public bool Overwrite;
    public string AwakeTarget = "";
    public string StartTarget = "";
    public string AwakeFound = "";
    public string AwakeChild = "";
    public string AwakeList = "";
    public string AwakeArray = "";
    public string AwakePending = "";
    public string AwakeFindInactive = "";
    public string AwakeFindRelativePath = "";
    public string AwakeFindAbsolutePath = "";
    public string AwakeChildDefault = "";
    public string AwakeChildIncludingInactive = "";
    public string AwakeParentDefault = "";
    public string AwakeParentIncludingInactive = "";
    public string CurrentPending = "";
    public string CurrentKeep = "";
    public string CurrentExpired = "";
    public string CurrentList = "";
    public string CurrentArray = "";
    public string CurrentEditedList = "";
    public string CurrentEditedArray = "";

    static string LabelOf(R3Target? value) => value != null ? value.Label : "<null>";
    static string Labels(IEnumerable<R3Target?> values)
    {
        var labels = new List<string>();
        foreach (var value in values) labels.Add(LabelOf(value));
        return string.Join("|", labels);
    }

    void Awake()
    {
        AwakeTarget = LabelOf(Target);
        AwakeFound = LabelOf(GameObject.Find("R3Last")?.GetComponent<R3Target>());
        AwakeChild = LabelOf(GetComponentInChildren<R3Target>());
        AwakeList = Labels(Targets);
        AwakeArray = Labels(TargetArray);
        AwakePending = LabelOf(PendingTarget);
        AwakeFindInactive = GameObject.Find("R3PendingTarget") == null ? "<null>" : "found";
        AwakeFindRelativePath = GameObject.Find("R3PathRoot/R3PathChild")?.name ?? "<null>";
        AwakeFindAbsolutePath = GameObject.Find("/R3PathRoot/R3PathChild")?.name ?? "<null>";
        AwakeChildDefault = LabelOf(GetComponentInChildren<R3Target>());
        AwakeChildIncludingInactive = LabelOf(GetComponentInChildren<R3Target>(true));
        AwakeParentDefault = ParentSearchTarget?.GetComponentInParent<BoxCollider>() == null
            ? "<null>" : "found";
        AwakeParentIncludingInactive = ParentSearchTarget?.GetComponentInParent<BoxCollider>(true) == null
            ? "<null>" : "found";
        if (Overwrite)
        {
            PendingTarget = Fallback;
            if (EditedList.Count != 0) EditedList[0] = Fallback;
            if (EditedArray.Length != 0) EditedArray[0] = Fallback;
        }
    }
    void Start() => StartTarget = LabelOf(Target);
    void Update()
    {
        CurrentPending = LabelOf(PendingTarget);
        CurrentKeep = LabelOf(PendingKeep);
        CurrentExpired = LabelOf(ExpiredTarget);
        CurrentList = Labels(PendingList);
        CurrentArray = Labels(PendingArray);
        CurrentEditedList = Labels(EditedList);
        CurrentEditedArray = Labels(EditedArray);
    }
}

[ReplayGuid("f3010000000000000000000000000003")]
public class R3Delayed : MonoBehaviour
{
    public int AwakeCalls;
    public int CollisionCalls;
    public int TriggerCalls;
    public int CollisionBeforeAwake;
    public int RoutineBeforeAwake;
    public int RoutineSteps;
    public int Updates;
    public float FixedDelta = -1;
    public float FixedStep = -1;
    public float FrameDelta = -1;
    void Awake() => ++AwakeCalls;
    void Update() { ++Updates; FrameDelta = Time.deltaTime; }
    void FixedUpdate() { FixedDelta = Time.deltaTime; FixedStep = Time.fixedDeltaTime; }
    void OnCollisionEnter(Collision collision)
    {
        ++CollisionCalls;
        if (AwakeCalls == 0) ++CollisionBeforeAwake;
    }
    void OnTriggerEnter(Collider? other) => ++TriggerCalls;
    public IEnumerator Run()
    {
        if (AwakeCalls == 0) ++RoutineBeforeAwake;
        ++RoutineSteps;
        yield return null;
        ++RoutineSteps;
    }
}

[ReplayGuid("f3010000000000000000000000000004")]
public class R3Driver : MonoBehaviour
{
    public int Command;
    public int DeadImmediately;
    public int Added;
    R3Delayed? child;
    void Update()
    {
        var command = Command;
        Command = 0;
        if (command == 1)
        {
            child = gameObject.AddComponent<R3Delayed>();
            Added = child != null ? 1 : 0;
            if (child != null) child.StartCoroutine(child.Run());
        }
        if (command == 2)
        {
            ReplayEngine.Object.Destroy(child);
            DeadImmediately = child == null ? 1 : 0;
        }
    }
}

[ReplayGuid("f3010000000000000000000000000005")]
public class R3Legacy : ScriptBehaviour
{
    public int Steps;
    public int StepsAtUpdate;
    public int TimerDone;
    public int TimerAtUpdate;
    public float TweenValueSeen;
    public float TweenAtUpdate;
    public int ButtonCalls;
    public int CollisionCalls;
    public override void Start()
    {
        StartCoroutine(Loop());
        After(0.02f, () => TimerDone = 1);
        TweenValue(0, 1, 0.02f, value => TweenValueSeen = value);
    }
    IEnumerator Loop() { while (true) { ++Steps; yield return null; } }
    public override void Update(float deltaTime)
    {
        StepsAtUpdate = Steps;
        TimerAtUpdate = TimerDone;
        TweenAtUpdate = TweenValueSeen;
    }
    public override void OnButtonClicked(ButtonEventInfo button) => ++ButtonCalls;
    public override void OnCollisionEnter(CollisionInfo collision) => ++CollisionCalls;
}

[ReplayGuid("f3010000000000000000000000000006")]
public class R3Throwing : MonoBehaviour
{
    public R3Throwing() => throw new System.InvalidOperationException("R3 expected constructor failure");
}

[ReplayGuid("f3010000000000000000000000000007")]
public class R3AwakeThrows : MonoBehaviour
{
    public int StartCalls;
    public int UpdateCalls;
    void Awake() => throw new System.InvalidOperationException("R3 expected Awake failure");
    void Start() => ++StartCalls;
    void Update() => ++UpdateCalls;
}

[ReplayGuid("f3010000000000000000000000000008")]
public class R3CoroutineStart : MonoBehaviour
{
    public int AwakeRoutineEntered;
    public int AwakeRoutineAfterYield;
    public int StartEntered;
    public int AfterYield;
    void Awake() => StartCoroutine(AwakeRoutine());
    IEnumerator AwakeRoutine()
    {
        AwakeRoutineEntered = 1;
        yield return null;
        AwakeRoutineAfterYield = 1;
    }
    IEnumerator Start()
    {
        StartEntered = 1;
        yield return null;
        AfterYield = 1;
    }
}
)CS";
            std::string encoded = "\xEF\xBB\xBF";
            for (const char value : source)
            {
                if (value == '\r') continue;
                if (value == '\n') encoded += '\r';
                encoded += value;
            }
            return encoded;
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

        // framework が 1 フレームで通すのと同じ順序・同じ時間の配り方を模す。
        //
        //   RuntimeTime を作る -> Update -> LateUpdate
        //     -> （物理と接触の発生）-> Event 配送 -> Script への配送
        //
        // 接触イベントと Coroutine は最後の配送フェーズで進むので、
        // world.Update() を呼ぶだけでは進まない。
        //
        // unscaled と time_scale を分けて受け取るのは、
        // Coroutine が Update と同じゲーム時間で進むかを確かめるため。
        // before_dispatch は「物理が接触を積む位置」に相当する差し込み口。
        void StepFrame(ReplayEngine::Scene::Scene& world, ScriptRuntime& runtime,
            ReplayEngine::Runtime::RuntimeContext& context,
            float unscaled_delta_time, float time_scale = 1.0f,
            const std::function<void()>& before_dispatch = {})
        {
            const float scaled = unscaled_delta_time * time_scale;

            ReplayEngine::Runtime::RuntimeTime time;
            time.delta_time = scaled;
            time.unscaled_delta_time = unscaled_delta_time;
            time.fixed_delta_time = 0.02f;
            time.time_scale = time_scale;
            time.frame_index = context.Time().frame_index + 1;
            context.SetTime(time);

            world.Update(scaled);
            world.LateUpdate(scaled);
            if (before_dispatch) before_dispatch();
            context.Events().Dispatch(&context.Resolver());
            runtime.PumpScriptEvents();
        }

        ScriptComponent* AddManagedScript(Core::GameObject& object,
            const ScriptTypeDescriptor& descriptor)
        {
            auto* script = object.AddComponent<ScriptComponent>();
            if (script != nullptr) script->AssignScriptType(descriptor);
            return script;
        }

        void RunMonoBehaviourBoundaryValidation(Checker& check, ScriptRuntime& runtime,
            CSharp::CSharpScriptBackend& backend)
        {
            namespace Serialization = ReplayEngine::Scene::Serialization;
            using namespace ReplayEngine::Runtime;
            ScriptTypeID types[8];
            const ScriptTypeDescriptor* descriptors[8]{};
            bool ready = true;
            for (int index = 0; index < 8; ++index)
            {
                Reflection::TypeGUID::TryParse("f301000000000000000000000000000" +
                    std::to_string(index + 1), types[index]);
                descriptors[index] = FindDescriptor(runtime, types[index]);
                ready = ReloadSchema(runtime, types[index]) && descriptors[index] != nullptr && ready;
            }
            check.ExpectEqual(ready ? 1 : 0, 1, "W fixture 全8型の Catalog / Schema を用意できる");
            check.ExpectEqual(runtime.Catalog().FindSchema(types[0])->InstantiateWhenInactive() ? 1 : 0, 1,
                "W11 MonoBehaviour Schema は inactive でも instance を準備する");
            check.ExpectEqual(runtime.Catalog().FindSchema(types[4])->InstantiateWhenInactive() ? 1 : 0, 0,
                "W11 Legacy ScriptBehaviour Schema は従来の inactive 生成時期を維持する");
            if (!ready) return;

            const auto read = [](ScriptComponent* script, const char* name)
            {
                return script != nullptr ? script->ReadField(ScriptNames::MakeFieldSavedName(name))
                    : ScriptValue{};
            };
            const auto write = [](ScriptComponent* script, const char* name, ScriptValue value)
            {
                if (script != nullptr) script->WriteField(ScriptNames::MakeFieldSavedName(name), value);
            };
            const auto reference = [](ScriptComponent* script)
            {
                Reflection::ComponentReference value;
                if (script != nullptr)
                {
                    value.owner = script->Owner()->ID();
                    value.component = script->StableID();
                }
                return ScriptValue::MakeComponentReference(value);
            };
            const auto find_script = [](Core::GameObject* object, ScriptTypeID type)
            {
                if (object != nullptr)
                    for (std::size_t index = 0; index < object->ComponentCount(); ++index)
                    {
                        auto* script = dynamic_cast<ScriptComponent*>(object->ComponentAt(index));
                        if (script != nullptr && script->ScriptType() == type) return script;
                    }
                return static_cast<ScriptComponent*>(nullptr);
            };
            const auto close_world = [&](ReplayEngine::Scene::Scene& world)
            {
                runtime.OnWorldUnloading(world);
                world.Clear();
                runtime.OnWorldUnloaded(world);
            };
            const auto publish = [](RuntimeContext& context, const ObjectHandle& owner, auto type)
            {
                EventRecord event;
                event.type = type;
                event.source = owner;
                event.target = owner;
                Core::GameObject* object = nullptr;
                context.Resolver().TryResolve(owner, object);
                auto* collider = object != nullptr ? object->GetComponent<Components::BoxColliderComponent>() : nullptr;
                event.payload.Set("other_valid", Reflection::PropertyValue::MakeBool(true));
                event.payload.Set("other_collider", Reflection::PropertyValue::MakeInt(
                    collider != nullptr ? static_cast<int>(collider->GetColliderID()) : 0));
                context.Events().Publish(std::move(event));
            };

            // W1-W3/W5: 前方の Probe から45個後方の実体と保存値を Awake で読む。
            ReplayEngine::Scene::Scene world("R3References");
            world.Services().SetScripts(&runtime);
            RuntimeContext context(world);
            world.Services().SetRuntime(&context);
            auto* fallback = AddManagedScript(*world.CreateGameObject("R3Fallback"), *descriptors[0]);
            write(fallback, "Label", ScriptValue::MakeString("fallback"));
            auto* carrier = world.CreateGameObject("R3Carrier");
            auto* probe = AddManagedScript(*carrier, *descriptors[1]);
            ScriptComponent* last = nullptr;
            for (int index = 0; index < 45; ++index)
            {
                auto* target = world.CreateGameObject(index == 44 ? "R3Last" : "R3Padding");
                last = AddManagedScript(*target, *descriptors[0]);
                if (index == 44) target->SetParent(carrier);
            }
            write(last, "Label", ScriptValue::MakeString("last"));
            write(probe, "Target", reference(last));
            const auto references = ScriptValue::MakeArray(Reflection::PropertyType::ComponentReference,
                { reference(last), reference(fallback), reference(last), reference(nullptr) });
            write(probe, "Targets", references);
            write(probe, "TargetArray", references);
            auto* path_root = world.CreateGameObject("R3PathRoot");
            auto* path_child = world.CreateGameObject("R3PathChild");
            path_child->SetParent(path_root);
            runtime.OnWorldActivating(world);
            world.Start();
            check.ExpectText(read(probe, "AwakeTarget").AsString(), "last",
                "W1 後方の serialized MonoBehaviour 参照と保存値が Awake で復元済み");
            check.ExpectText(read(probe, "AwakeFound").AsString(), "last",
                "W2 Awake の Find/GetComponent が後方の Managed instance を取得できる");
            check.ExpectText(read(probe, "AwakeChild").AsString(), "last",
                "W2 Awake の GetComponentInChildren が後方の Managed instance を取得できる");
            check.ExpectText(read(probe, "AwakeFindRelativePath").AsString(), "R3PathChild",
                "W12 GameObject.Find は相対 Hierarchy path を解決する");
            check.ExpectText(read(probe, "AwakeFindAbsolutePath").AsString(), "R3PathChild",
                "W12 GameObject.Find は / 始まりの root path を解決する");
            check.ExpectText(read(probe, "StartTarget").AsString(), "last",
                "W3 47 Script の Scene で最後の Script への参照を失わない");
            const std::string expected_collection = "last|fallback|last|<null>";
            check.ExpectText(read(probe, "AwakeList").AsString(), expected_collection,
                "W5 List<MonoBehaviour> の全要素が Awake より前に復元される");
            check.ExpectText(read(probe, "AwakeArray").AsString(), expected_collection,
                "W5 MonoBehaviour[] の全要素が Awake より前に復元される");

            const Core::ObjectID carrier_id = carrier->ID();
            Serialization::SceneData data;
            Serialization::CaptureScene(world, data);
            const auto scene_path = std::filesystem::path("Saved/Validation/CSharp/MonoBehaviourR3.replayscene");
            ScopedFileRestore restore_scene(scene_path);
            std::string error;
            check.ExpectEqual(Serialization::SceneSerializer::SaveToFile(data, scene_path, error) ? 1 : 0,
                1, "W5 collection 参照を Scene ファイルへ保存できる");
            Serialization::SceneData loaded;
            check.ExpectEqual(Serialization::SceneSerializer::LoadFromFile(loaded, scene_path, error) ? 1 : 0,
                1, "W5 collection 参照を Scene ファイルから読み込める");
            close_world(world);
            ReplayEngine::Scene::Scene restored("R3Restored");
            restored.Services().SetScripts(&runtime);
            RuntimeContext restored_context(restored);
            restored.Services().SetRuntime(&restored_context);
            Serialization::SceneLoadReport report;
            check.ExpectEqual(Serialization::ApplySceneData(loaded, restored, report) ? 1 : 0, 1,
                "W5 collection を持つ別 World を構築できる");
            runtime.OnWorldActivating(restored);
            restored.Start();
            probe = find_script(restored.FindGameObjectByID(carrier_id), types[1]);
            check.ExpectText(read(probe, "AwakeTarget").AsString(), "last",
                "W1 Scene round-trip 後も Awake で参照が解決済み");
            check.ExpectText(read(probe, "AwakeList").AsString(), expected_collection,
                "W5 List の順序・重複・null が Scene round-trip する");
            check.ExpectText(read(probe, "AwakeArray").AsString(), expected_collection,
                "W5 配列の順序・重複・null が Scene round-trip する");
            close_world(restored);

            // 新 MonoBehaviour は inactive でも instance/field を先に用意する。
            // Legacy だけ従来の遅延生成を維持し、Authoring API の inactive 検索規則も見る。
            ReplayEngine::Scene::Scene pending_world("R3Pending");
            pending_world.Services().SetScripts(&runtime);
            RuntimeContext pending_context(pending_world);
            pending_world.Services().SetRuntime(&pending_context);
            fallback = AddManagedScript(*pending_world.CreateGameObject("R3Fallback"), *descriptors[0]);
            write(fallback, "Label", ScriptValue::MakeString("fallback"));
            probe = AddManagedScript(*pending_world.CreateGameObject("R3Probe"), *descriptors[1]);
            auto* pending_object = pending_world.CreateGameObject("R3PendingTarget");
            auto* pending_target = AddManagedScript(*pending_object, *descriptors[0]);
            pending_object->SetParent(probe->Owner());
            pending_object->SetEnabled(false);
            write(pending_target, "Label", ScriptValue::MakeString("late"));
            auto* inactive_legacy_object = pending_world.CreateGameObject("R3InactiveLegacy");
            auto* inactive_legacy = AddManagedScript(*inactive_legacy_object, *descriptors[4]);
            inactive_legacy_object->SetEnabled(false);
            auto* inactive_parent = pending_world.CreateGameObject("R3InactiveParent");
            inactive_parent->AddComponent<Components::BoxColliderComponent>();
            inactive_parent->SetEnabled(false);
            auto* parent_target_object = pending_world.CreateGameObject("R3ParentTarget");
            parent_target_object->SetParent(inactive_parent);
            auto* parent_target = AddManagedScript(*parent_target_object, *descriptors[0]);
            write(parent_target, "Label", ScriptValue::MakeString("parent-target"));
            write(probe, "Fallback", reference(fallback));
            write(probe, "PendingTarget", reference(pending_target));
            write(probe, "ParentSearchTarget", reference(parent_target));
            write(probe, "Overwrite", ScriptValue::MakeBool(true));
            const auto pending_values = ScriptValue::MakeArray(Reflection::PropertyType::ComponentReference,
                { reference(pending_target), reference(fallback), reference(nullptr) });
            for (const char* field : { "PendingList", "PendingArray", "EditedList", "EditedArray" })
                write(probe, field, pending_values);
            runtime.OnWorldActivating(pending_world);
            pending_world.Start();
            check.ExpectEqual(pending_target->HasInstance() ? 1 : 0, 1,
                "W11 非 Active の MonoBehaviour も instance は Awake 前に存在する");
            check.ExpectEqual(read(pending_target, "AwakeCalls").AsInt(), 0,
                "W11 非 Active の MonoBehaviour は instance があっても Awake は遅延する");
            check.ExpectEqual(inactive_legacy->HasInstance() ? 1 : 0, 0,
                "W11 非 Active の Legacy ScriptBehaviour は従来どおり未生成");
            check.ExpectText(read(probe, "AwakePending").AsString(), "late",
                "W11 active 側 Awake から inactive MonoBehaviour の serialized 参照を読める");
            check.ExpectText(read(probe, "AwakeFindInactive").AsString(), "<null>",
                "W12 GameObject.Find は inactive GameObject を返さない");
            check.ExpectText(read(probe, "AwakeChildDefault").AsString(), "<null>",
                "W13 GetComponentInChildren 既定は inactive child を除外する");
            check.ExpectText(read(probe, "AwakeChildIncludingInactive").AsString(), "late",
                "W13 includeInactive=true なら inactive child も検索する");
            check.ExpectText(read(probe, "AwakeParentDefault").AsString(), "<null>",
                "W13 GetComponentInParent 既定は inactive parent を除外する");
            check.ExpectText(read(probe, "AwakeParentIncludingInactive").AsString(), "found",
                "W13 GetComponentInParent(true) は inactive parent を検索する");
            pending_object->SetEnabled(true);
            StepFrame(pending_world, runtime, pending_context, 0.016f);
            check.ExpectText(read(probe, "CurrentPending").AsString(), "fallback",
                "W4 Awake で代入した fallback を遅延参照が上書きしない");
            check.ExpectText(read(probe, "CurrentList").AsString(), "late|fallback|<null>",
                "W5 Pending List の未生成要素も有効化後に復元される");
            check.ExpectText(read(probe, "CurrentArray").AsString(), "late|fallback|<null>",
                "W5 Pending 配列の未生成要素も有効化後に復元される");
            check.ExpectText(read(probe, "CurrentEditedList").AsString(), "fallback|fallback|<null>",
                "W4 Pending List の要素を Awake で編集しても上書きされない");
            check.ExpectText(read(probe, "CurrentEditedArray").AsString(), "fallback|fallback|<null>",
                "W4 Pending 配列の要素を Awake で編集しても上書きされない");

            auto* slow_object = pending_world.CreateGameObject("R3Slow");
            auto* slow = AddManagedScript(*slow_object, *descriptors[0]);
            slow_object->SetEnabled(false);
            write(slow, "Label", ScriptValue::MakeString("slow"));
            write(probe, "PendingKeep", reference(slow));
            for (int index = 0; index < 100; ++index)
                runtime.Invoke(probe->InstanceHandle(), ScriptCallback::Update, ScriptArguments::DeltaTime(0.016f));
            slow_object->SetEnabled(true);
            StepFrame(pending_world, runtime, pending_context, 0.016f);
            check.ExpectText(read(probe, "CurrentKeep").AsString(), "slow",
                "W3 同一フレームに100 callback が来ても Pending を打ち切らない");
            auto* expired_object = pending_world.CreateGameObject("R3Expired");
            auto* expired = AddManagedScript(*expired_object, *descriptors[0]);
            expired_object->SetEnabled(false);
            write(expired, "Label", ScriptValue::MakeString("expired"));
            write(probe, "ExpiredTarget", reference(expired));
            for (int index = 0; index < 64; ++index)
                StepFrame(pending_world, runtime, pending_context, 0.016f);
            expired_object->SetEnabled(true);
            StepFrame(pending_world, runtime, pending_context, 0.016f);
            check.ExpectText(read(probe, "CurrentExpired").AsString(), "expired",
                "W11 inactive MonoBehaviour 参照は64フレーム経過でも失われない");
            close_world(pending_world);

            // Component の Awake を個別に駆動し、4フェーズに解決されない Pending を検証する。
            ReplayEngine::Scene::Scene delayed_world("R3DeferredReferences");
            delayed_world.Services().SetScripts(&runtime);
            RuntimeContext delayed_context(delayed_world);
            delayed_world.Services().SetRuntime(&delayed_context);
            fallback = AddManagedScript(*delayed_world.CreateGameObject("R3Fallback"), *descriptors[0]);
            probe = AddManagedScript(*delayed_world.CreateGameObject("R3DeferredProbe"), *descriptors[1]);
            auto* deferred = AddManagedScript(*delayed_world.CreateGameObject("R3DeferredTarget"), *descriptors[0]);
            write(fallback, "Label", ScriptValue::MakeString("fallback"));
            write(deferred, "Label", ScriptValue::MakeString("deferred"));
            write(probe, "Fallback", reference(fallback));
            write(probe, "PendingTarget", reference(deferred));
            write(probe, "Overwrite", ScriptValue::MakeBool(true));
            const auto deferred_values = ScriptValue::MakeArray(Reflection::PropertyType::ComponentReference,
                { reference(deferred), reference(fallback), reference(nullptr) });
            for (const char* field : { "PendingList", "PendingArray", "EditedList", "EditedArray" })
                write(probe, field, deferred_values);
            runtime.OnWorldActivating(delayed_world);
            static_cast<Core::Component*>(fallback)->OnRuntimeAwake();
            static_cast<Core::Component*>(probe)->OnRuntimeAwake();
            check.ExpectEqual(deferred->HasInstance() ? 1 : 0, 0,
                "W4 Pending 再試行の対象は実際に Managed instance が未生成");
            check.ExpectText(read(probe, "AwakePending").AsString(), "<null>",
                "W4 個別 Awake は未解決参照を書き換える境界を通る");
            for (int index = 0; index < 100; ++index)
                runtime.Invoke(probe->InstanceHandle(), ScriptCallback::Update, ScriptArguments::DeltaTime(0.016f));
            check.ExpectText(read(probe, "CurrentList").AsString(), "<null>|fallback|<null>",
                "W5 100 callback 後も遅延対象が未生成の List は未解決のまま");
            static_cast<Core::Component*>(deferred)->OnRuntimeAwake();
            runtime.Invoke(probe->InstanceHandle(), ScriptCallback::Update, ScriptArguments::DeltaTime(0.016f));
            check.ExpectText(read(probe, "CurrentPending").AsString(), "fallback",
                "W4 実際の Pending も Awake の scalar 代入を上書きしない");
            check.ExpectText(read(probe, "CurrentList").AsString(), "deferred|fallback|<null>",
                "W3/W5 100 callback を越えた Pending List が解決する");
            check.ExpectText(read(probe, "CurrentArray").AsString(), "deferred|fallback|<null>",
                "W3/W5 100 callback を越えた Pending 配列が解決する");
            check.ExpectText(read(probe, "CurrentEditedList").AsString(), "fallback|fallback|<null>",
                "W4 実際の Pending List も Awake の要素編集を上書きしない");
            check.ExpectText(read(probe, "CurrentEditedArray").AsString(), "fallback|fallback|<null>",
                "W4 実際の Pending 配列も Awake の要素編集を上書きしない");
            auto* timeout_target = AddManagedScript(*delayed_world.CreateGameObject("R3TimeoutTarget"), *descriptors[0]);
            write(timeout_target, "Label", ScriptValue::MakeString("too-late"));
            write(probe, "ExpiredTarget", reference(timeout_target));
            for (int index = 0; index < 64; ++index)
            {
                RuntimeTime time = delayed_context.Time();
                ++time.frame_index;
                delayed_context.SetTime(time);
                runtime.PumpScriptEvents();
            }
            check.ExpectEqual(timeout_target->HasInstance() ? 1 : 0, 0,
                "W3 64フレームの打ち切り境界まで参照先を未生成に保つ");
            static_cast<Core::Component*>(timeout_target)->OnRuntimeAwake();
            runtime.Invoke(probe->InstanceHandle(), ScriptCallback::Update, ScriptArguments::DeltaTime(0.016f));
            check.ExpectText(read(probe, "CurrentExpired").AsString(), "<null>",
                "W3 実際の Pending は64フレーム経過で打ち切る");
            close_world(delayed_world);

            ReplayEngine::Scene::Scene event_world("R3Events");
            event_world.Services().SetScripts(&runtime);
            RuntimeContext event_context(event_world);
            event_world.Services().SetRuntime(&event_context);
            auto* driver_object = event_world.CreateGameObject("R3Driver");
            driver_object->AddComponent<Components::BoxColliderComponent>();
            auto* driver = AddManagedScript(*driver_object, *descriptors[3]);
            auto* legacy_object = event_world.CreateGameObject("R3Legacy");
            auto* legacy = AddManagedScript(*legacy_object, *descriptors[4]);
            auto* awake_throw_object = event_world.CreateGameObject("R3AwakeThrows");
            auto* awake_throw = AddManagedScript(*awake_throw_object, *descriptors[6]);
            auto* coroutine_start_object = event_world.CreateGameObject("R3CoroutineStart");
            auto* coroutine_start = AddManagedScript(*coroutine_start_object, *descriptors[7]);
            const auto driver_handle = event_context.Resolver().MakeHandle(driver_object);
            const auto legacy_handle = event_context.Resolver().MakeHandle(legacy_object);
            runtime.OnWorldActivating(event_world);
            event_world.Start();
            check.ExpectEqual(read(awake_throw, "StartCalls").AsInt(), 0,
                "W14 Awake 例外後は Start へ進まない");
            check.ExpectEqual(awake_throw->Enabled() ? 1 : 0, 0,
                "W14 Awake 例外で MonoBehaviour を disable する");
            check.ExpectEqual(read(coroutine_start, "AwakeRoutineEntered").AsInt(), 1,
                "W15 MonoBehaviour.StartCoroutine は Awake 中でも最初の yield まで即時実行する");
            check.ExpectEqual(read(coroutine_start, "AwakeRoutineAfterYield").AsInt(), 0,
                "W15 Awake で開始した Coroutine は yield 後を次フレームまで待つ");
            check.ExpectEqual(read(coroutine_start, "StartEntered").AsInt(), 1,
                "W15 IEnumerator Start は Start 同期点で最初の yield まで実行する");
            check.ExpectEqual(read(coroutine_start, "AfterYield").AsInt(), 0,
                "W15 IEnumerator Start は最初の yield で次フレームまで待つ");
            write(driver, "Command", ScriptValue::MakeInt(1));
            StepFrame(event_world, runtime, event_context, 0.04f, 0.5f,
                [&] { publish(event_context, driver_handle, EngineEvents::CollisionEnter); });
            auto* delayed = find_script(driver_object, types[2]);
            check.ExpectEqual(read(driver, "Added").AsInt(), 1, "W6/W7 Update 内の AddComponent が成功する");
            check.ExpectEqual(read(delayed, "AwakeCalls").AsInt(), 0, "W6/W7 追加フレームでは Awake は未実行");
            check.ExpectEqual(read(delayed, "CollisionCalls").AsInt(), 0, "W6 Awake 前に Collision を配送しない");
            check.ExpectEqual(read(delayed, "RoutineSteps").AsInt(), 0, "W7 Awake 前に Coroutine を前進させない");
            check.ExpectEqual(read(legacy, "StepsAtUpdate").AsInt(), 1,
                "W10 Legacy Coroutine は同フレームの Update より前に進む");
            check.ExpectEqual(read(legacy, "TimerAtUpdate").AsInt(), 1,
                "W10 Legacy Timer は scaled delta で Update より前に進む");
            check.ExpectClose(read(legacy, "TweenAtUpdate").AsFloat(), 1.0f,
                "W10 Legacy Tween は scaled delta で Update より前に進む");
            check.ExpectEqual(read(awake_throw, "UpdateCalls").AsInt(), 0,
                "W14 Awake 例外で Update も実行しない");
            check.ExpectEqual(read(coroutine_start, "StartEntered").AsInt(), 1,
                "W15 IEnumerator Start を同じフレームで二重に開始しない");
            check.ExpectEqual(read(coroutine_start, "AwakeRoutineAfterYield").AsInt(), 1,
                "W15 Awake で開始した Coroutine は次の Script pump で継続する");
            check.ExpectEqual(read(coroutine_start, "AfterYield").AsInt(), 1,
                "W15 IEnumerator Start は次の Script pump で yield 後を継続する");
            StepFrame(event_world, runtime, event_context, 0.016f);
            check.ExpectEqual(read(delayed, "AwakeCalls").AsInt(), 1, "W6/W7 次の同期点で Awake が1回走る");
            check.ExpectEqual(read(delayed, "CollisionCalls").AsInt(), 1, "W6 待機中の Collision は Awake 後に配送する");
            check.ExpectEqual(read(delayed, "CollisionBeforeAwake").AsInt(), 0, "W6 Collision 内から見ても Awake 済み");
            check.ExpectEqual(read(delayed, "RoutineSteps").AsInt(), 1, "W7 Awake 後の Coroutine は1歩だけ進む");
            check.ExpectEqual(read(delayed, "RoutineBeforeAwake").AsInt(), 0, "W7 Coroutine 内から見ても Awake 済み");
            check.ExpectEqual(read(legacy, "StepsAtUpdate").AsInt(), 2, "W10 Legacy Coroutine を末尾で二重に進めない");
            check.ExpectEqual(read(coroutine_start, "AfterYield").AsInt(), 1,
                "W15 IEnumerator Start は完了後に二重実行しない");

            if (delayed != nullptr) delayed->SetEnabled(false);
            const int updates = read(delayed, "Updates").AsInt();
            for (int index = 0; index < 3; ++index)
                StepFrame(event_world, runtime, event_context, 0.016f, 1.0f, [&]
                {
                    publish(event_context, driver_handle, EngineEvents::CollisionEnter);
                    publish(event_context, driver_handle, EngineEvents::TriggerEnter);
                });
            check.ExpectEqual(read(delayed, "CollisionCalls").AsInt(), 4, "W8 disabled MonoBehaviour に Collision を配送する");
            check.ExpectEqual(read(delayed, "TriggerCalls").AsInt(), 3, "W8 disabled MonoBehaviour に Trigger を配送する");
            check.ExpectEqual(read(delayed, "Updates").AsInt(), updates, "W8 disabled 中の Update は止めたまま");
            const int collisions = read(delayed, "CollisionCalls").AsInt();
            if (delayed != nullptr) delayed->SetEnabled(true);
            StepFrame(event_world, runtime, event_context, 0.016f);
            check.ExpectEqual(read(delayed, "CollisionCalls").AsInt(), collisions,
                "W9 re-enable で過去の Collision をまとめて配送しない");

            legacy->SetEnabled(false);
            for (int index = 0; index < 3; ++index)
                StepFrame(event_world, runtime, event_context, 0.016f, 1.0f,
                    [&] { publish(event_context, legacy_handle, EngineEvents::ButtonClicked); });
            legacy->SetEnabled(true);
            StepFrame(event_world, runtime, event_context, 0.016f);
            check.ExpectEqual(read(legacy, "ButtonCalls").AsInt(), 0,
                "W9 disabled 中の Engine イベントは捨て re-enable で配送しない");
            StepFrame(event_world, runtime, event_context, 0.016f, 1.0f,
                [&] { publish(event_context, legacy_handle, EngineEvents::ButtonClicked); });
            check.ExpectEqual(read(legacy, "ButtonCalls").AsInt(), 1,
                "W9 re-enable 後の新しい Engine イベントは配送する");
            const int inactive_collisions = read(delayed, "CollisionCalls").AsInt();
            driver_object->SetEnabled(false);
            StepFrame(event_world, runtime, event_context, 0.016f, 1.0f,
                [&] { publish(event_context, driver_handle, EngineEvents::CollisionEnter); });
            check.ExpectEqual(read(delayed, "CollisionCalls").AsInt(), inactive_collisions, "W9 非 Active 中の Collision は配送しない");
            driver_object->SetEnabled(true);
            StepFrame(event_world, runtime, event_context, 0.016f);
            check.ExpectEqual(read(delayed, "CollisionCalls").AsInt(), inactive_collisions, "W9 再 Active で過去の Collision を配送しない");

            auto* dormant_object = event_world.CreateGameObject("R3DormantAdded");
            dormant_object->SetEnabled(false);
            const auto dormant_handle = event_context.Resolver().MakeHandle(dormant_object);
            ComponentHandle dormant_component;
            check.ExpectEqual(static_cast<int>(event_context.AddScriptComponent(dormant_handle,
                types[2].high, types[2].low, dormant_component)), static_cast<int>(RuntimeStatus::Ok),
                "W6 非 Active の GameObject への AddComponent も instance を返す");
            auto* dormant = find_script(dormant_object, types[2]);
            StepFrame(event_world, runtime, event_context, 0.016f, 1.0f,
                [&] { publish(event_context, dormant_handle, EngineEvents::CollisionEnter); });
            check.ExpectEqual(read(dormant, "AwakeCalls").AsInt(), 0,
                "W6 非 Active の間は追加した instance の Awake を遅らせる");
            dormant_object->SetEnabled(true);
            StepFrame(event_world, runtime, event_context, 0.016f);
            check.ExpectEqual(read(dormant, "CollisionCalls").AsInt(), 0,
                "W9 Awake 前かつ非 Active 中の接触も初回 Active へ持ち越さない");
            StepFrame(event_world, runtime, event_context, 0.016f, 1.0f,
                [&] { publish(event_context, dormant_handle, EngineEvents::CollisionEnter); });
            check.ExpectEqual(read(dormant, "CollisionCalls").AsInt(), 1,
                "W9 初回 Active 後の新しい接触は配送する");

            RuntimeTime time = event_context.Time();
            time.delta_time = 0.033f;
            time.fixed_delta_time = 0.02f;
            event_context.SetTime(time);
            event_world.FixedUpdate(0.02f);
            check.ExpectClose(read(delayed, "FixedDelta").AsFloat(), 0.02f, "W11 FixedUpdate の Time.deltaTime は fixed delta");
            check.ExpectClose(read(delayed, "FixedStep").AsFloat(), 0.02f, "W11 Time.fixedDeltaTime は RuntimeTime の値を維持");
            event_world.FixedUpdate(0.015f);
            check.ExpectClose(read(delayed, "FixedDelta").AsFloat(), 0.015f, "W11 FixedUpdate の delta は callback 引数から取る");
            check.ExpectClose(read(delayed, "FixedStep").AsFloat(), 0.02f, "W11 callback 引数が違っても fixedDeltaTime は変更しない");
            event_world.Update(0.033f);
            check.ExpectClose(read(delayed, "FrameDelta").AsFloat(), 0.033f, "W11 Update では通常の Time.deltaTime へ戻る");

            write(driver, "Command", ScriptValue::MakeInt(2));
            StepFrame(event_world, runtime, event_context, 0.016f);
            check.ExpectEqual(read(driver, "DeadImmediately").AsInt(), 1, "W12 Destroy(MonoBehaviour) の直後に fake-null になる");

            const int components_before = static_cast<int>(driver_object->ComponentCount());
            const int instances_before = static_cast<int>(backend.LiveInstanceCount());
            ComponentHandle failed = event_context.Resolver().MakeHandle(driver);
            const auto status = event_context.AddScriptComponent(driver_handle, types[5].high, types[5].low, failed);
            check.ExpectEqual(static_cast<int>(status), static_cast<int>(RuntimeStatus::UnsupportedOperation),
                "W13 コンストラクタ例外による instance 生成失敗を Status で返す");
            check.ExpectEqual(failed.IsEmpty() ? 1 : 0, 1, "W13 失敗した AddComponent の out は空");
            auto* failed_script = find_script(driver_object, types[5]);
            check.ExpectEqual(failed_script == nullptr || failed_script->PendingDestroy() ? 1 : 0, 1,
                "W13 失敗した ScriptComponent は直ちに破棄予約される");
            StepFrame(event_world, runtime, event_context, 0.016f);
            check.ExpectEqual(static_cast<int>(driver_object->ComponentCount()), components_before,
                "W13 同期点の後に半端な ScriptComponent が残らない");
            check.ExpectEqual(static_cast<int>(backend.LiveInstanceCount()), instances_before,
                "W13 失敗した AddComponent の Managed instance が漏れない");
            close_world(event_world);
            check.ExpectEqual(static_cast<int>(backend.LiveInstanceCount()), 0, "W fixture 全 World の instance を解放できる");
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
        StepFrame(world, *runtime, runtime_context, 0.016f);
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
            StepFrame(world, *runtime, runtime_context, 0.016f);
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

    // 起動時のビルド失敗からの復旧。
    //
    // ホットリロードの失敗（上の suite で確認済み）とは意味が違う。
    // ホットリロードは「直前の Assembly を維持する」で足りるが、
    // 起動直後はまだ何もロードしていないため、維持する相手がいない。
    // ここで Assembly が 1 つも無いまま Play へ入ると、C# の型が全部
    // 「C# Assembly is not loaded.」になり、移動も UI ボタンも動かなくなる。
    //
    // 【別プロセスに分けてある理由】
    //   hostfxr_initialize_for_runtime_config は 1 プロセスに 1 回しか通らない。
    //   同じプロセスで Backend を作り直すと、確かめたい経路の手前で
    //   0x80008081 で止まってしまい、起動の分岐を通せない。
    //   確かめたいのは「プロセスの最初の Initialize()」なので、
    //   その 1 回だけを持つ検証としてここへ独立させる。
    int RunCSharpStartupRecoveryValidation()
    {
        namespace CSharp = ReplayEngine::Scripting::CSharp;

        Checker check(880);
        Core::RegisterBuiltInComponents();

        const std::filesystem::path root = std::filesystem::current_path();
        const std::filesystem::path scripts = CSharp::CSharpProject::ScriptsRoot(root);
        const std::filesystem::path broken_source =
            scripts / "ValidationStartupBrokenBehaviour.cs";
        ScopedFileRestore restore_broken(broken_source);

        // 復旧に使う「前回のビルド結果」が無ければ、まず 1 回作る。
        // ここは壊れたソースを置く前なので普通に成功する。
        const std::filesystem::path assembly =
            CSharp::CSharpProject::GameScriptsAssemblyPath(root);
        std::error_code exists_error;
        if (!std::filesystem::exists(assembly, exists_error) || exists_error)
        {
            const CSharp::CSharpBuildResult seed =
                CSharp::CSharpProject::BuildGameScripts(root);
            check.Expect(seed.succeeded, "復旧元にする C# Assembly を用意できる");
        }
        check.Expect(std::filesystem::exists(assembly, exists_error) && !exists_error,
            "ディスクに前回の C# Assembly がある");

        check.Expect(WriteText(broken_source,
            "using ReplayEngine;\nnamespace ValidationScripts;\n"
            "public sealed class ValidationStartupBrokenBehaviour : ScriptBehaviour "
            "{ syntax error }\n"),
            "起動時ビルドを失敗させる C# ソースを書ける");

        auto runtime = std::make_unique<ScriptRuntime>();
        auto instance = std::make_unique<CSharp::CSharpScriptBackend>(root);
        CSharp::CSharpScriptBackend* backend = instance.get();
        runtime->InstallBackend(std::move(instance));
        runtime->Initialize();

        check.Expect(!backend->LastBuildResult().succeeded,
            "起動時の C# ビルドが実際に失敗している");
        check.Expect(backend->AssemblyLoaded(),
            "ビルドが失敗しても、ディスクに残った Assembly で C# は動く");
        check.Expect(backend->StartupUsedExistingAssembly(),
            "古い Assembly で復旧したことを呼び出し側が判定できる");
        check.Expect(!backend->StartupDiagnostic().empty(),
            "起動時のビルド失敗の理由が診断として残る");

        runtime->Shutdown();

        std::error_code remove_error;
        std::filesystem::remove(broken_source, remove_error);
        return check.Report("csharp-startup-recovery");
    }

    // 新しい Authoring API（MonoBehaviour）の検証。
    //
    // Legacy の ScriptBehaviour 検証とは別関数にしてある。
    // 同じ関数へ足すと、既存の項目が新 API の失敗で巻き添えになり、
    // どちらが壊れたのか分からなくなるため。
    int RunMonoBehaviourValidation()
    {
        namespace Assets = ReplayEngine::Assets;
        namespace CSharp = ReplayEngine::Scripting::CSharp;
        namespace Serialization = ReplayEngine::Scene::Serialization;
        using ReplayEngine::Runtime::ObjectHandle;
        using ReplayEngine::Runtime::RuntimeContext;

        Checker check(1000);
        Core::RegisterBuiltInComponents();

        const std::filesystem::path root = std::filesystem::current_path();
        const std::filesystem::path scripts = CSharp::CSharpProject::ScriptsRoot(root);
        const std::filesystem::path authored_source =
            scripts / "ValidationMonoBehaviour.cs";
        const std::filesystem::path boundary_source =
            scripts / "ValidationMonoBehaviourR3.cs";
        const std::filesystem::path legacy_source =
            scripts / "ValidationCSharpBehaviour.cs";
        const std::filesystem::path validation_folder =
            std::filesystem::path("Saved") / "Validation" / "CSharp";
        const std::filesystem::path validation_db =
            validation_folder / "AssetDatabaseMonoBehaviour.replaydb";
        const std::filesystem::path scene_path =
            validation_folder / "MonoBehaviourRoundTrip.replayscene";
        std::error_code folder_error;
        std::filesystem::create_directories(validation_folder, folder_error);

        // 検証に要る .cs はここで書き出す。手で置いたファイルには依存しない。
        // 元の内容は ScopedFileRestore が必ず戻す。失敗しても残さない。
        ScopedFileRestore restore_authored(authored_source);
        ScopedFileRestore restore_legacy(legacy_source);
        ScopedFileRestore restore_boundary(boundary_source);
        check.Expect(WriteText(authored_source, MonoBehaviourSource()),
            "検証用 MonoBehaviour の .cs を書き出せる");
        check.Expect(WriteText(legacy_source, ValidBehaviourSource()),
            "検証用 Legacy ScriptBehaviour の .cs を書き出せる");
        check.ExpectEqual(WriteText(boundary_source, MonoBehaviourBoundarySource()) ? 1 : 0, 1,
            "W fixture 自己完結した境界検証スクリプトを書き出せる");

        ScriptTypeID authored_type;
        ScriptTypeID observer_type;
        ScriptTypeID added_type;
        ScriptTypeID coroutine_type;
        ScriptTypeID adder_type;
        ScriptTypeID legacy_type;
        Reflection::TypeGUID::TryParse(authored_guid_text, authored_type);
        Reflection::TypeGUID::TryParse(observer_guid_text, observer_type);
        Reflection::TypeGUID::TryParse(added_guid_text, added_type);
        Reflection::TypeGUID::TryParse(coroutine_guid_text, coroutine_type);
        Reflection::TypeGUID::TryParse(adder_guid_text, adder_type);
        Reflection::TypeGUID::TryParse(validation_guid_text, legacy_type);

        std::string error;
        Assets::AssetDatabase database(validation_db);
        database.Load(error);

        auto runtime = std::make_unique<ScriptRuntime>();
        auto instance = std::make_unique<CSharp::CSharpScriptBackend>(root);
        CSharp::CSharpScriptBackend* backend = instance.get();
        runtime->InstallBackend(std::move(instance));
        check.Expect(runtime->Initialize(), "C# Backend が起動する");

        CSharp::CSharpBuildResult build;
        check.Expect(backend->CompileAndReload(&build),
            "MonoBehaviour を含む C# がビルドできる");
        if (!build.succeeded && !build.output_text.empty())
            std::fprintf(stderr, "%s\n", build.output_text.c_str());

        check.Expect(CSharp::CSharpProject::RefreshCatalog(root, database,
            runtime->Catalog(), error), "Catalog を更新できる");

        // ---- 型の発見 ---------------------------------------------------------
        const ScriptTypeDescriptor* authored = FindDescriptor(*runtime, authored_type);
        const ScriptTypeDescriptor* observer = FindDescriptor(*runtime, observer_type);
        const ScriptTypeDescriptor* added = FindDescriptor(*runtime, added_type);
        const ScriptTypeDescriptor* coroutine = FindDescriptor(*runtime, coroutine_type);
        const ScriptTypeDescriptor* adder = FindDescriptor(*runtime, adder_type);
        check.Expect(authored != nullptr,
            "MonoBehaviour 派生が Add Component の一覧へ載る");
        check.Expect(observer != nullptr,
            "同じファイルの 2 つ目の MonoBehaviour も載る");
        check.Expect(added != nullptr && coroutine != nullptr && adder != nullptr,
            "検証用の MonoBehaviour がすべて載る");
        check.Expect(authored != nullptr &&
            authored->category == ScriptCategoryName(ScriptLanguage::CSharp),
            "MonoBehaviour も C# として分類される");

        // ---- Inspector schema -------------------------------------------------
        check.Expect(ReloadSchema(*runtime, authored_type),
            "MonoBehaviour の Inspector schema を取得できる");
        ScriptFieldSchemaRef schema = runtime->Catalog().FindSchema(authored_type);
        check.Expect(schema && schema->FindBySavedName("field.moveSpeed") != nullptr,
            "[SerializeField] の private field が Inspector へ出る");
        check.Expect(schema && schema->FindBySavedName("field.jumpPower") != nullptr,
            "[SerializeField] private float も出る");
        check.Expect(schema && schema->FindBySavedName("field.AwakeCalls") != nullptr,
            "public field が Inspector へ出る");
        const ScriptFieldDefinition* hidden =
            schema ? schema->FindBySavedName("field.LastDeltaTime") : nullptr;
        check.Expect(hidden != nullptr && !hidden->visible_in_inspector,
            "[HideInInspector] が MonoBehaviour でも効く");
        check.Expect(hidden != nullptr && hidden->serializable,
            "[HideInInspector] でも保存対象は維持する");
        check.Expect(ReloadSchema(*runtime, observer_type),
            "2 つ目の MonoBehaviour の schema も取得できる");
        check.Expect(ReloadSchema(*runtime, added_type) &&
            ReloadSchema(*runtime, coroutine_type) && ReloadSchema(*runtime, adder_type),
            "検証用 MonoBehaviour の schema をすべて取得できる");
        check.Expect(ReloadSchema(*runtime, legacy_type),
            "Legacy の schema も今までどおり取得できる");

        // Public 参照が Inspector の型として出るか（V9/V10/V11/V13 の前提）。
        const ScriptFieldDefinition* ref_object =
            schema ? schema->FindBySavedName("field.RefObject") : nullptr;
        const ScriptFieldDefinition* ref_body =
            schema ? schema->FindBySavedName("field.RefBody") : nullptr;
        const ScriptFieldDefinition* ref_observer =
            schema ? schema->FindBySavedName("field.RefObserver") : nullptr;
        const ScriptFieldDefinition* ref_component =
            schema ? schema->FindBySavedName("field.RefComponent") : nullptr;
        check.Expect(ref_object != nullptr &&
            ref_object->type == Reflection::PropertyType::ObjectReference,
            "public GameObject が ObjectReference として Inspector へ出る");
        check.Expect(ref_body != nullptr &&
            ref_body->type == Reflection::PropertyType::ComponentReference,
            "public Rigidbody が ComponentReference として Inspector へ出る");
        check.Expect(ref_observer != nullptr &&
            ref_observer->type == Reflection::PropertyType::ComponentReference,
            "public MonoBehaviour 派生も ComponentReference として出る");
        check.Expect(ref_component != nullptr &&
            ref_component->type == Reflection::PropertyType::ComponentReference,
            "基底型 Component の field も ComponentReference として出る");

        // ---- Scene を組む -----------------------------------------------------
        ReplayEngine::Scene::Scene world("MonoBehaviourValidationScene");
        world.Services().SetScripts(runtime.get());

        Core::GameObject* carrier = world.CreateGameObject("Carrier");
        Core::GameObject* ref_target = world.CreateGameObject("RefTarget");
        Core::GameObject* hitter = world.CreateGameObject("Hitter");
        Core::GameObject* parent = world.CreateGameObject("Parent");
        Core::GameObject* child = world.CreateGameObject("Child");
        Core::GameObject* solo = world.CreateGameObject("Solo");
        Core::GameObject* adder_object = world.CreateGameObject("Adder");
        Core::GameObject* legacy_object = world.CreateGameObject("LegacyCarrier");
        check.Expect(carrier != nullptr && ref_target != nullptr && hitter != nullptr &&
            parent != nullptr && child != nullptr && solo != nullptr &&
            adder_object != nullptr && legacy_object != nullptr,
            "検証用 GameObject を作れる");
        if (child != nullptr && parent != nullptr) child->SetParent(parent);

        // 相手側に Collider を 2 つ。型を変えて、どちらに当たったかを見分ける。
        Components::BoxColliderComponent* box = hitter != nullptr
            ? hitter->AddComponent<Components::BoxColliderComponent>() : nullptr;
        Components::SphereColliderComponent* sphere = hitter != nullptr
            ? hitter->AddComponent<Components::SphereColliderComponent>() : nullptr;
        check.Expect(box != nullptr && sphere != nullptr,
            "相手 GameObject へ Collider を 2 つ付けられる");

        Components::RigidbodyComponent* body = carrier != nullptr
            ? carrier->AddComponent<Components::RigidbodyComponent>() : nullptr;
        check.Expect(body != nullptr, "Rigidbody を付けられる");

        ScriptComponent* script = (carrier != nullptr && authored != nullptr)
            ? AddManagedScript(*carrier, *authored) : nullptr;
        ScriptComponent* observer_a = (carrier != nullptr && observer != nullptr)
            ? AddManagedScript(*carrier, *observer) : nullptr;
        ScriptComponent* observer_b = (carrier != nullptr && observer != nullptr)
            ? AddManagedScript(*carrier, *observer) : nullptr;
        check.Expect(script != nullptr && observer_a != nullptr && observer_b != nullptr,
            "同じ GameObject へ同じ型の MonoBehaviour を 2 つ付けられる");

        ScriptComponent* child_script = (child != nullptr && coroutine != nullptr)
            ? AddManagedScript(*child, *coroutine) : nullptr;
        ScriptComponent* solo_script = (solo != nullptr && coroutine != nullptr)
            ? AddManagedScript(*solo, *coroutine) : nullptr;
        ScriptComponent* adder_script = (adder_object != nullptr && adder != nullptr)
            ? AddManagedScript(*adder_object, *adder) : nullptr;
        ScriptComponent* legacy_script = legacy_object != nullptr
            ? AddManagedScript(*legacy_object,
                *FindDescriptor(*runtime, legacy_type)) : nullptr;
        check.Expect(child_script != nullptr && solo_script != nullptr &&
            adder_script != nullptr && legacy_script != nullptr,
            "Coroutine / Adder / Legacy のスクリプトを配置できる");

        if (script != nullptr)
            script->WriteField("field.moveSpeed", ScriptValue::MakeFloat(7.5f));
        if (observer_b != nullptr)
            observer_b->WriteField("field.Label", ScriptValue::MakeString("second"));
        if (body != nullptr) body->mass = 2.5f;

        RuntimeContext runtime_context(world);
        world.Services().SetRuntime(&runtime_context);

        runtime->OnWorldActivating(world);
        world.Start();
        check.Expect(script == nullptr || script->HasInstance(),
            "Play 開始で MonoBehaviour のインスタンスが作られる");

        StepFrame(world, *runtime, runtime_context, 0.016f);

        const auto read = [&](ScriptComponent* target, const char* field)
        {
            return target != nullptr
                ? target->ReadField(ScriptNames::MakeFieldSavedName(field)) : ScriptValue{};
        };
        const bool running = script != nullptr && script->HasInstance();

        // ---- 既存のライフサイクル ---------------------------------------------
        check.Expect(running && read(script, "AwakeCalls").AsInt() == 1,
            "void Awake() が 1 回呼ばれる");
        check.Expect(running && read(script, "EnableCalls").AsInt() >= 1,
            "void OnEnable() が呼ばれる");
        check.Expect(running && read(script, "StartCalls").AsInt() == 1,
            "void Start() が 1 回呼ばれる");
        check.Expect(running && read(script, "UpdateCalls").AsInt() >= 1,
            "void Update() が呼ばれる");
        check.Expect(running && read(script, "LateUpdateCalls").AsInt() >= 1,
            "void LateUpdate() が呼ばれる");
        check.Expect(running && Close(read(script, "LastDeltaTime").AsFloat(), 0.016f),
            "Time.deltaTime が callback 中に正しい値になる");
        check.Expect(running && Close(read(script, "moveSpeed").AsFloat(), 7.5f),
            "Inspector の値が MonoBehaviour のフィールドへ入る");
        check.Expect(running && read(script, "ObserverName").AsString() == "observer",
            "GetComponent<MonoBehaviour 派生>() が同じ API で引ける");

        // ---- V1 Awake 中 AddComponent -----------------------------------------
        StepFrame(world, *runtime, runtime_context, 0.016f);
        check.ExpectEqual(read(adder_script, "AddedNonNullInAwake").AsInt(), 1,
            "V1 Awake 中の AddComponent<T>() が非 null を返す");
        check.ExpectEqual(read(adder_script, "NotStartedAtAdd").AsInt(), 1,
            "V1 追加した直後は Awake も Start も走っていない");
        check.ExpectEqual(read(adder_script, "AddedAwakeCalls").AsInt(), 1,
            "V1 Awake 中に足した Component の Awake が 1 回走る");
        check.ExpectEqual(read(adder_script, "AddedStartCalls").AsInt(), 1,
            "V1 Awake 中に足した Component の Start が走る");
        check.ExpectEqual(read(adder_script, "AddedStartBeforeAwake").AsInt(), 0,
            "V1 Start が Awake より先に走らない");

        // ---- V21 Time.timeScale ------------------------------------------------
        StepFrame(world, *runtime, runtime_context, 0.02f, 0.5f);
        check.ExpectClose(read(script, "TimeScaleSeen").AsFloat(), 0.5f,
            "V21 Time.timeScale が設定値を返す");
        check.ExpectClose(read(script, "LastDeltaTime").AsFloat(), 0.01f,
            "V21 Time.deltaTime が timeScale 済みの値になる");

        // ---- V4 Coroutine が scaled time で進む --------------------------------
        //
        // WaitForSeconds(0.5) を timeScale 0.5 / 実時間 0.25 秒で待つ。
        // scaled なら 0.125 秒ずつ進むので 4 歩。unscaled なら 2 歩で終わる。
        // 途中の確認は「まだ終わっていないこと」。ここが 1 になったら回帰。
        if (script != nullptr) script->WriteField("field.Command", ScriptValue::MakeInt(1));
        StepFrame(world, *runtime, runtime_context, 0.25f, 0.5f);   // Command 実行 + 1 歩目
        StepFrame(world, *runtime, runtime_context, 0.25f, 0.5f);
        StepFrame(world, *runtime, runtime_context, 0.25f, 0.5f);
        check.ExpectEqual(read(script, "WaitDone").AsInt(), 0,
            "V4 WaitForSeconds が unscaled time で先に終わらない");
        StepFrame(world, *runtime, runtime_context, 0.25f, 0.5f);
        StepFrame(world, *runtime, runtime_context, 0.25f, 0.5f);
        check.ExpectEqual(read(script, "WaitDone").AsInt(), 1,
            "V4 WaitForSeconds が scaled time で終わる");

        // ---- V5 timeScale = 0 で止まる ----------------------------------------
        if (script != nullptr)
        {
            script->WriteField("field.WaitDone", ScriptValue::MakeInt(0));
            script->WriteField("field.Command", ScriptValue::MakeInt(1));
        }
        StepFrame(world, *runtime, runtime_context, 0.25f, 0.0f);
        for (int index = 0; index < 20; ++index)
            StepFrame(world, *runtime, runtime_context, 0.25f, 0.0f);
        check.ExpectEqual(read(script, "WaitDone").AsInt(), 0,
            "V5 timeScale = 0 で WaitForSeconds が進まない");

        // 止めた Coroutine が残らないよう、ここで最後まで進めておく。
        for (int index = 0; index < 6; ++index)
            StepFrame(world, *runtime, runtime_context, 0.25f, 1.0f);

        // ---- V6 親を非 Active にすると子の Coroutine が止まる -------------------
        //
        // 親を切るのは Update の途中から。Scene の同期点の外で切らないと、
        // 「OnDisable が来る前にフレーム末尾の Pump が 1 歩進める」経路を通らない。
        const int child_steps_before = read(child_script, "Steps").AsInt();
        check.Expect(child_steps_before > 0, "V6 子の Coroutine が動いている");
        if (script != nullptr) script->WriteField("field.Command", ScriptValue::MakeInt(9));
        StepFrame(world, *runtime, runtime_context, 0.016f);
        const int child_steps_after_disable = read(child_script, "Steps").AsInt();
        check.Expect(child_steps_after_disable == child_steps_before,
            "V6 親を非 Active にしたフレームで子の Coroutine が 1 歩も余分に進まない");
        for (int index = 0; index < 3; ++index)
            StepFrame(world, *runtime, runtime_context, 0.016f);
        check.Expect(read(child_script, "Steps").AsInt() == child_steps_before,
            "V6 非 Active のあいだ子の Coroutine は止まったまま");

        if (script != nullptr) script->WriteField("field.Command", ScriptValue::MakeInt(10));
        for (int index = 0; index < 3; ++index)
            StepFrame(world, *runtime, runtime_context, 0.016f);
        check.Expect(read(child_script, "Steps").AsInt() == child_steps_before,
            "V6 再 Active にしても前の Coroutine の途中からは再開しない");
        check.ExpectEqual(read(child_script, "StartCalls").AsInt(), 1,
            "V6 再 Active で Start が二度は呼ばれない");

        // ---- V7 enabled = false は Update だけ止める ---------------------------
        const int solo_steps_before = read(solo_script, "Steps").AsInt();
        const int solo_updates_before = read(solo_script, "UpdateCalls").AsInt();
        if (solo_script != nullptr) solo_script->SetEnabled(false);
        for (int index = 0; index < 3; ++index)
            StepFrame(world, *runtime, runtime_context, 0.016f);
        check.Expect(read(solo_script, "UpdateCalls").AsInt() == solo_updates_before,
            "V7 enabled = false で Update が止まる");
        check.Expect(read(solo_script, "Steps").AsInt() > solo_steps_before,
            "V7 enabled = false でも Coroutine は進む");
        if (solo_script != nullptr) solo_script->SetEnabled(true);

        // ---- V8 Legacy ScriptBehaviour の Coroutine は従来どおり ---------------
        const int legacy_steps_before = read(legacy_script, "LoopSteps").AsInt();
        check.Expect(legacy_steps_before > 0,
            "V8 Legacy ScriptBehaviour の Coroutine が動いている");
        if (legacy_script != nullptr) legacy_script->SetEnabled(false);
        for (int index = 0; index < 3; ++index)
            StepFrame(world, *runtime, runtime_context, 0.016f);
        check.Expect(read(legacy_script, "LoopSteps").AsInt() == legacy_steps_before,
            "V8 Legacy は Component 無効中に Coroutine が進まない");
        if (legacy_script != nullptr) legacy_script->SetEnabled(true);
        for (int index = 0; index < 2; ++index)
            StepFrame(world, *runtime, runtime_context, 0.016f);
        check.Expect(read(legacy_script, "LoopSteps").AsInt() > legacy_steps_before,
            "V8 Legacy は再度有効にすると Coroutine が進む");
        check.Expect(legacy_script != nullptr && legacy_script->HasInstance(),
            "V22 Legacy ScriptBehaviour が同じ World で今までどおり動く");

        // ---- V2 / V3 / V19 / V20 接触の同フレーム配送と Collider の特定 --------
        const ObjectHandle carrier_handle = carrier != nullptr
            ? runtime_context.Resolver().MakeHandle(carrier) : ObjectHandle::None();
        const ObjectHandle hitter_handle = hitter != nullptr
            ? runtime_context.Resolver().MakeHandle(hitter) : ObjectHandle::None();

        const auto publish_contact = [&](bool trigger, std::uint32_t collider_id)
        {
            ReplayEngine::Runtime::EventRecord record;
            record.type = trigger ? ReplayEngine::Runtime::EngineEvents::TriggerEnter
                : ReplayEngine::Runtime::EngineEvents::CollisionEnter;
            record.type_name = trigger ? "TriggerEnter" : "CollisionEnter";
            record.source = carrier_handle;
            record.target = hitter_handle;
            record.payload.Set("self_collider", Reflection::PropertyValue::MakeInt(0));
            record.payload.Set("other_collider",
                Reflection::PropertyValue::MakeInt(static_cast<int>(collider_id)));
            record.payload.Set("other_valid", Reflection::PropertyValue::MakeBool(true));
            runtime_context.Events().Publish(std::move(record));
        };

        const std::uint32_t sphere_id = sphere != nullptr
            ? static_cast<std::uint32_t>(sphere->GetColliderID()) : 0;
        const std::uint32_t box_id = box != nullptr
            ? static_cast<std::uint32_t>(box->GetColliderID()) : 0;
        check.Expect(sphere_id != 0 && box_id != 0 && sphere_id != box_id,
            "Collider ごとに別の実行時 ID が振られている");

        StepFrame(world, *runtime, runtime_context, 0.016f, 1.0f,
            [&] { publish_contact(false, sphere_id); });
        check.ExpectEqual(read(script, "CollisionCalls").AsInt(), 1,
            "V2 OnCollisionEnter が届く");
        check.ExpectEqual(read(script, "CollisionFrame").AsInt(),
            read(script, "UpdateFrame").AsInt(),
            "V2 Collision が Update と同じフレームで届く");
        check.ExpectEqual(read(script, "ColliderKind").AsInt(), 2,
            "V19 ID が指す SphereCollider がそのまま渡る");
        check.ExpectEqual(read(script, "ColliderMissing").AsInt(), 0,
            "V19 Collider が null にならない");

        StepFrame(world, *runtime, runtime_context, 0.016f, 1.0f,
            [&] { publish_contact(true, box_id); });
        check.ExpectEqual(read(script, "TriggerCalls").AsInt(), 1,
            "V3 OnTriggerEnter が届く");
        check.ExpectEqual(read(script, "TriggerFrame").AsInt(),
            read(script, "UpdateFrame").AsInt(),
            "V3 Trigger が Update と同じフレームで届く");
        check.ExpectEqual(read(script, "TriggerColliderKind").AsInt(), 1,
            "V19 Trigger も ID が指す BoxCollider がそのまま渡る");

        // 存在しない ID。別の Collider へ化けてはいけない。
        StepFrame(world, *runtime, runtime_context, 0.016f, 1.0f,
            [&] { publish_contact(false, 0x7FFFFFFEu); });
        check.ExpectEqual(read(script, "CollisionCalls").AsInt(), 2,
            "V20 ID が解決できなくても Collision 自体は届く");
        check.ExpectEqual(read(script, "ColliderMissing").AsInt(), 1,
            "V20 解決できない ColliderID では null になる");
        check.ExpectEqual(read(script, "ColliderKind").AsInt(), 0,
            "V20 別の Collider へ化けない");

        // ---- V14 / V15 AddComponent<T>() --------------------------------------
        if (script != nullptr) script->WriteField("field.Command", ScriptValue::MakeInt(2));
        StepFrame(world, *runtime, runtime_context, 0.016f);
        check.ExpectEqual(read(script, "AddedNonNull").AsInt(), 1,
            "V14 AddComponent<MonoBehaviour 派生>() が直後に非 null を返す");
        check.ExpectEqual(read(script, "AddedAwakeAtAdd").AsInt(), 0,
            "V14 追加したその場では Awake を呼ばない");
        check.ExpectEqual(read(script, "AddedSameInstance").AsInt(), 1,
            "V15 GetComponent<T>() が同じ instance を返す");
        check.ExpectEqual(read(script, "AddedFieldValue").AsInt(), 123,
            "V15 追加直後に書いた値が読み戻せる");
        if (script != nullptr) script->WriteField("field.Command", ScriptValue::MakeInt(8));
        StepFrame(world, *runtime, runtime_context, 0.016f);
        StepFrame(world, *runtime, runtime_context, 0.016f);
        check.ExpectEqual(read(script, "AddedAwakeLater").AsInt(), 1,
            "V14 Awake は Scene の同期点で 1 回だけ走る");

        // ---- V9 / V10 / V11 / V12 / V13 参照を入れて保存形式まで確かめる -------
        if (script != nullptr) script->WriteField("field.Command", ScriptValue::MakeInt(5));
        StepFrame(world, *runtime, runtime_context, 0.016f);
        check.ExpectText(read(script, "ResolvedObjectName").AsString(), "RefTarget",
            "V9 GameObject 参照を script から代入できる");
        check.ExpectText(read(script, "ResolvedObserverLabel").AsString(), "second",
            "V12 同じ型の 2 つ目の MonoBehaviour を指せる");
        check.ExpectText(read(script, "ResolvedComponentLabel").AsString(), "second",
            "V13 基底型 Component の field へ MonoBehaviour を入れられる");

        const ScriptValue saved_object = read(script, "RefObject");
        const ScriptValue saved_body = read(script, "RefBody");
        const ScriptValue saved_observer = read(script, "RefObserver");
        const ScriptValue saved_component = read(script, "RefComponent");
        check.Expect(saved_object.AsObjectReference().Valid() && ref_target != nullptr &&
            saved_object.AsObjectReference() == ref_target->ID(),
            "V9 GameObject 参照が ObjectID として保存される");
        check.Expect(saved_body.AsComponentReference().IsAssigned() && body != nullptr &&
            saved_body.AsComponentReference().component == body->StableID(),
            "V10 Native Component 参照が Stable ID として保存される");
        check.Expect(saved_observer.AsComponentReference().IsAssigned() &&
            observer_b != nullptr &&
            saved_observer.AsComponentReference().component == observer_b->StableID(),
            "V12 MonoBehaviour 参照は 2 つ目の ScriptComponent の Stable ID を保存する");
        check.Expect(saved_component.AsComponentReference().IsAssigned() &&
            observer_b != nullptr &&
            saved_component.AsComponentReference().component == observer_b->StableID(),
            "V13 基底型 Component の field も同じ Stable ID を保存する");

        // ---- Scene を保存して読み直す（V9-V13 の復元側） ------------------------
        Serialization::SceneData data;
        Serialization::CaptureScene(world, data);
        check.Expect(Serialization::SceneSerializer::SaveToFile(data, scene_path, error),
            "参照を持つ Scene を保存できる");
        Serialization::SceneData loaded;
        check.Expect(Serialization::SceneSerializer::LoadFromFile(loaded, scene_path, error),
            "保存した Scene を読み直せる");

        // ---- V17 Component だけを破棄する -------------------------------------
        if (script != nullptr) script->WriteField("field.Command", ScriptValue::MakeInt(3));
        StepFrame(world, *runtime, runtime_context, 0.016f);
        if (script != nullptr) script->WriteField("field.Command", ScriptValue::MakeInt(4));
        StepFrame(world, *runtime, runtime_context, 0.016f);
        check.ExpectEqual(read(script, "BodyDead").AsInt(), 1,
            "V17 破棄した Component は null 扱いになる");
        check.ExpectEqual(read(script, "OwnerAlive").AsInt(), 1,
            "V17 GameObject 側は生きたまま");
        check.ExpectEqual(read(script, "DeadMassRead").AsInt(), 1,
            "V17 破棄済み Component のプロパティを読んでも落ちない");

        // ---- V16 破棄した GameObject ------------------------------------------
        if (script != nullptr) script->WriteField("field.Command", ScriptValue::MakeInt(6));
        StepFrame(world, *runtime, runtime_context, 0.016f);
        if (script != nullptr) script->WriteField("field.Command", ScriptValue::MakeInt(7));
        StepFrame(world, *runtime, runtime_context, 0.016f);
        check.ExpectEqual(read(script, "TempObjectDead").AsInt(), 1,
            "V16 破棄した GameObject は null 扱いになる");

        const Core::ObjectID carrier_id = carrier != nullptr
            ? carrier->ID() : Core::ObjectID::Invalid();
        const std::uint32_t observer_b_stable = observer_b != nullptr
            ? observer_b->StableID() : 0;

        // 旧 World の Handle。新しい World で解決できてはいけない（V18）。
        const ObjectHandle stale_handle = carrier_handle;

        runtime->OnWorldUnloading(world);
        world.Clear();
        runtime->OnWorldUnloaded(world);
        check.Expect(runtime->LastLeakedInstanceCount() == 0 &&
            backend->LiveInstanceCount() == 0,
            "Play 停止で MonoBehaviour のインスタンスが解放される");

        ReplayEngine::Scene::Scene restored("MonoBehaviourRestoredScene");
        restored.Services().SetScripts(runtime.get());
        Serialization::SceneLoadReport report;
        check.Expect(Serialization::ApplySceneData(loaded, restored, report),
            "SceneData から MonoBehaviour を復元できる");

        RuntimeContext restored_context(restored);
        restored.Services().SetRuntime(&restored_context);
        runtime->OnWorldActivating(restored);
        restored.Start();
        StepFrame(restored, *runtime, restored_context, 0.016f);

        Core::GameObject* restored_carrier = restored.FindGameObjectByID(carrier_id);
        ScriptComponent* restored_script = nullptr;
        if (restored_carrier != nullptr)
        {
            for (std::size_t index = 0; index < restored_carrier->ComponentCount(); ++index)
            {
                auto* candidate = dynamic_cast<ScriptComponent*>(
                    restored_carrier->ComponentAt(index));
                if (candidate == nullptr) continue;
                if (candidate->ScriptType() == authored_type) restored_script = candidate;
            }
        }
        check.Expect(restored_script != nullptr && restored_script->HasInstance(),
            "復元した Scene でも MonoBehaviour が動き出す");
        check.ExpectText(read(restored_script, "ResolvedObjectName").AsString(), "RefTarget",
            "V9 Scene 再ロード後も GameObject 参照が同じ相手を指す");
        check.ExpectClose(read(restored_script, "ResolvedBodyMass").AsFloat(), 2.5f,
            "V10 Scene 再ロード後も Rigidbody 参照が同じ相手を指す");
        check.ExpectText(read(restored_script, "ResolvedObserverLabel").AsString(), "second",
            "V11/V12 Scene 再ロード後も 2 つ目の MonoBehaviour へ戻る");
        check.ExpectText(read(restored_script, "ResolvedComponentLabel").AsString(), "second",
            "V13 Scene 再ロード後も Component field の MonoBehaviour へ戻る");
        check.Expect(read(restored_script, "RefObserver")
            .AsComponentReference().component == observer_b_stable,
            "V12 復元後も同じ Stable ID を保存し直す");

        // ---- V18 古い World の Handle -----------------------------------------
        Core::GameObject* stale_resolved = nullptr;
        const ReplayEngine::Runtime::RuntimeStatus stale_status =
            restored_context.Resolver().TryResolve(stale_handle, stale_resolved);
        check.Expect(stale_status != ReplayEngine::Runtime::RuntimeStatus::Ok &&
            stale_resolved == nullptr,
            "V18 前の World の Handle は新しい World で解決されない");

        // ---- 後始末 -----------------------------------------------------------
        runtime->OnWorldUnloading(restored);
        restored.Clear();
        runtime->OnWorldUnloaded(restored);
        check.Expect(runtime->LastLeakedInstanceCount() == 0 &&
            backend->LiveInstanceCount() == 0,
            "復元した World を捨ててもインスタンスが残らない");

        RunMonoBehaviourBoundaryValidation(check, *runtime, *backend);

        runtime->Shutdown();
        std::error_code scene_remove_error;
        std::filesystem::remove(scene_path, scene_remove_error);
        return check.Report("monobehaviour");
    }

    // C# から地形を制御できるかの検証。
    //
    // 形を変える実装は既存の LandscapeData / LandscapeEditorTool のまま。
    // ここで確かめるのは「C# の Public API からその実装へ届くか」だけ。
    int RunLandscapeScriptValidation()
    {
        namespace Assets = ReplayEngine::Assets;
        namespace CSharp = ReplayEngine::Scripting::CSharp;

        Checker check(1100);
        Core::RegisterBuiltInComponents();

        const std::filesystem::path root = std::filesystem::current_path();
        const std::filesystem::path validation_folder =
            std::filesystem::path("Saved") / "Validation" / "CSharp";
        std::error_code folder_error;
        std::filesystem::create_directories(validation_folder, folder_error);

        ScriptTypeID landscape_type;
        Reflection::TypeGUID::TryParse("c9e7fa5b241638ac0d15be7392f4a86d", landscape_type);

        std::string error;
        Assets::AssetDatabase database(validation_folder / "AssetDatabaseLandscape.replaydb");
        database.Load(error);

        auto runtime = std::make_unique<ScriptRuntime>();
        auto instance = std::make_unique<CSharp::CSharpScriptBackend>(root);
        CSharp::CSharpScriptBackend* backend = instance.get();
        runtime->InstallBackend(std::move(instance));
        check.Expect(runtime->Initialize(), "C# Backend が起動する");

        CSharp::CSharpBuildResult build;
        check.Expect(backend->CompileAndReload(&build), "検証用 C# がビルドできる");
        if (!build.succeeded && !build.output_text.empty())
            std::fprintf(stderr, "%s\n", build.output_text.c_str());
        check.Expect(CSharp::CSharpProject::RefreshCatalog(root, database,
            runtime->Catalog(), error), "Catalog を更新できる");

        const ScriptTypeDescriptor* descriptor = FindDescriptor(*runtime, landscape_type);
        check.Expect(descriptor != nullptr, "地形検証用の MonoBehaviour が見つかる");
        check.Expect(ReloadSchema(*runtime, landscape_type), "schema を取得できる");

        // ---- Scene と地形 -----------------------------------------------
        ReplayEngine::Scene::Scene world("LandscapeScriptValidationScene");
        world.Services().SetScripts(runtime.get());
        Core::GameObject* ground = world.CreateGameObject("Ground");
        check.Expect(ground != nullptr, "地形用の GameObject を作れる");

        auto* landscape = ground != nullptr
            ? ground->AddComponent<Components::LandscapeComponent>() : nullptr;
        check.Expect(landscape != nullptr, "LandscapeComponent を足せる");

        // 既存の生成をそのまま使う。ここで独自の地形を作らない。
        const int resolution = 33;
        const float cell = 2.0f;
        check.Expect(landscape != nullptr &&
            landscape->GenerateFlat(resolution, resolution, cell, 0.0f),
            "既存の GenerateFlat で平地を作れる");

        const std::uint64_t revision_before =
            landscape != nullptr ? landscape->Data().Revision() : 0;

        ScriptComponent* script = (ground != nullptr && descriptor != nullptr)
            ? AddManagedScript(*ground, *descriptor) : nullptr;
        check.Expect(script != nullptr, "同じ GameObject へ MonoBehaviour を足せる");

        ReplayEngine::Runtime::RuntimeContext runtime_context(world);
        ReplayEngine::Runtime::RuntimeTime time;
        time.delta_time = 0.016f;
        time.unscaled_delta_time = 0.016f;
        time.fixed_delta_time = 0.02f;
        runtime_context.SetTime(time);
        world.Services().SetRuntime(&runtime_context);

        runtime->OnWorldActivating(world);
        world.Start();
        StepFrame(world, *runtime, runtime_context, 0.016f);

        check.Expect(script != nullptr && script->HasInstance(),
            "地形検証用のインスタンスが作られる");

        if (script != nullptr && script->HasInstance())
        {
            const auto read = [&](const char* field)
            {
                return script->ReadField(ScriptNames::MakeFieldSavedName(field));
            };

            // ---- 形の情報 -------------------------------------------------
            check.Expect(read("Found").AsBool(),
                "GetComponent<Landscape>() が地形を返す");
            check.Expect(read("Width").AsInt() == resolution &&
                read("Height").AsInt() == resolution,
                "C# から格子の大きさを読める");
            check.Expect(Close(read("CellSize").AsFloat(), cell),
                "C# から格子 1 マスの距離を読める");

            // ---- 高さの読み書き -------------------------------------------
            check.Expect(Close(read("HeightBeforeSet").AsFloat(), 0.0f),
                "平地の高さは 0 から始まる");
            check.Expect(Close(read("HeightAfterSet").AsFloat(), 4.25f),
                "C# の SetHeight が地形へ届く");

            // 隣は 0 のままなので、その中間は 0 と 4.25 の間になる。
            const float sampled = read("SampledBetween").AsFloat();
            check.Expect(sampled > 0.05f && sampled < 4.25f,
                "SampleHeight が格子の間を補間する");

            // ---- 彫刻 -----------------------------------------------------
            check.Expect(read("SculptApplied").AsBool(),
                "C# の Sculpt が既存の LandscapeEditorTool を動かす");
            check.Expect(read("PeakAfterSculpt").AsFloat() > 0.1f,
                "彫刻で地形の高さが実際に上がる");

            // ---- レイ -----------------------------------------------------
            check.Expect(read("RaycastHit").AsBool(),
                "C# から地形へレイを当てられる");
            check.Expect(read("RaycastHeight").AsFloat() > 0.1f,
                "レイが彫った直後の形に当たる");
        }
        else
        {
            for (int index = 0; index < 10; ++index)
                check.Expect(false, "地形検証用のインスタンスが動かなかった");
        }

        // ---- 変更が Revision へ乗るか -------------------------------------
        //
        // 描画メッシュと衝突形状は Revision を見て作り直される。
        // ここが上がっていれば、見た目と当たり判定も追従する。
        const std::uint64_t revision_after =
            landscape != nullptr ? landscape->Data().Revision() : 0;
        check.Expect(revision_after > revision_before,
            "C# からの変更で Revision が進む（描画と衝突が追従する）");

        runtime->OnWorldUnloading(world);
        world.Clear();
        runtime->OnWorldUnloaded(world);
        runtime->Shutdown();
        return check.Report("landscape-script");
    }
}
