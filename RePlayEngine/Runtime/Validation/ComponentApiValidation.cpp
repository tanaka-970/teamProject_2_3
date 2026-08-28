// v10 で足した C# 向け API の実行検証。
//
// 検証するのは C++ 側の RuntimeContext と Native API 表のヘッダー。
// C# の wrapper はこの API をそのまま呼ぶだけなので、ここが通れば経路は通る。

#include "BehaviourValidationInternal.h"

#include "../../Components/Camera/CameraComponent.h"
#include "../../Components/Audio/AudioSourceComponent.h"
#include "../../Components/Physics/RigidbodyComponent.h"
#include "../../Components/Rendering/AnimatorComponent.h"
#include "../../Components/Rendering/LightComponents.h"
#include "../../Components/Rendering/MeshRendererComponent.h"
#include "../../Components/Rendering/ParticleEmitterComponent.h"
#include "../../Components/UI/CanvasComponent.h"
#include "../../Components/UI/UIImageComponent.h"
#include "../../Components/UI/RectTransformComponent.h"
#include "../../Components/UI/UISliderComponent.h"
#include "../../Object/Registry/BuiltInComponents.h"
#include "../../Reflection/Property/PropertyValue.h"
#include "../../Scripting/CSharp/CSharpScriptBackendNativeInternal.h"
#include "../../UI/UILayout.h"

#include <cmath>

namespace ReplayEngine::Runtime::Validation
{
    using namespace Detail::BehaviourValidation;

    namespace
    {
        bool Near(float value, float expected, float tolerance = 1.0e-3f) noexcept
        {
            return std::fabs(value - expected) <= tolerance;
        }

        // 生デバイス入力の検証用。決まった値だけを返す。
        class StubInputService final : public Scene::IInputService
        {
        public:
            bool Held(std::string_view, int) const noexcept override { return false; }
            bool Pressed(std::string_view, int) const noexcept override { return false; }
            bool Released(std::string_view, int) const noexcept override { return false; }
            float Axis(std::string_view, int) const noexcept override { return 0.0f; }
            float PointerDeltaX() const noexcept override { return 0.0f; }
            float PointerDeltaY() const noexcept override { return 0.0f; }

            bool KeyHeld(int key) const noexcept override { return key == 'A'; }
            bool MouseButtonHeld(int button) const noexcept override { return button == 0; }
            float PointerX() const noexcept override { return 320.0f; }
            float PointerY() const noexcept override { return 240.0f; }
            float WheelDelta() const noexcept override { return 2.0f; }
            bool GamepadConnected(int slot) const noexcept override { return slot == 0; }
            bool GamepadButtonHeld(int slot, int button) const noexcept override
            {
                return slot == 0 && button == 0x1000;
            }
            float GamepadAxisValue(int slot, int axis) const noexcept override
            {
                return slot == 0 && axis == 0 ? 0.5f : 0.0f;
            }
            bool SetGamepadVibration(int slot, float, float) noexcept override
            {
                return slot == 0;
            }
        };
    }

    int RunComponentApiValidation()
    {
        Core::RegisterBuiltInComponents();
        Checker check(900);

        Scene::Scene world("ComponentApiWorld");
        RuntimeContext runtime(world);
        world.Services().SetRuntime(&runtime);

        // ---- Native API 表の自己記述ヘッダー --------------------------------
        {
            namespace Detail = Scripting::CSharp::Detail;
            const Detail::NativeApiTable table = Detail::MakeNativeApiTable();
            check.Expect(table.header.abi_version == Detail::kNativeApiAbiVersion,
                "Native API 表が現在の ABI 版を名乗る");
            check.Expect(table.header.struct_size == sizeof(Detail::NativeApiTable),
                "Native API 表が自分の大きさを申告する");
            const std::uint32_t expected_entries = static_cast<std::uint32_t>(
                (sizeof(Detail::NativeApiTable) - sizeof(Detail::NativeApiHeader)) /
                sizeof(void*));
            check.Expect(table.header.entry_count == expected_entries,
                "Native API 表の関数本数が大きさと合っている");
            check.Expect(table.component_type_id != nullptr &&
                table.get_component_property_double != nullptr &&
                table.rigidbody_add_force != nullptr &&
                table.subscribe_event_scoped != nullptr &&
                table.component_command != nullptr &&
                table.component_type_info != nullptr &&
                table.get_scene_transition_state != nullptr,
                "Component と型付き Event の callback が表へ入っている");

            char camera_type_info[1024]{};
            check.Expect(table.component_type_info("CameraComponent", camera_type_info,
                static_cast<int>(sizeof(camera_type_info))) == 0 &&
                std::string(camera_type_info).find("CameraComponent\t") == 0,
                "Component 型メタデータを Native API から取得できる");
        }

        // ---- 型名から ComponentTypeID を引く -------------------------------
        check.Expect(runtime.FindComponentTypeId("CameraComponent") ==
            Components::CameraComponent::StaticTypeID(),
            "型名から Camera の TypeID を引ける");
        check.Expect(runtime.FindComponentTypeId("RigidbodyComponent") ==
            Components::RigidbodyComponent::StaticTypeID(),
            "型名から Rigidbody の TypeID を引ける");
        check.Expect(runtime.FindComponentTypeId("NoSuchComponent") ==
            Core::invalid_component_type_id,
            "未登録の型名は無効 ID を返す");

        ObjectHandle actor;
        check.Expect(Succeeded(runtime.CreateGameObject("Actor", actor)),
            "検証用 GameObject を作れる");

        // ---- 汎用プロパティ: Camera ----------------------------------------
        ComponentHandle camera;
        check.Expect(Succeeded(runtime.AddComponent(actor,
            Components::CameraComponent::StaticTypeID(), camera)),
            "Camera を型 ID で追加できる");

        std::string type_name;
        check.Expect(Succeeded(runtime.GetComponentTypeName(camera, type_name)) &&
            type_name == "CameraComponent",
            "Component Handle から型名を引ける");

        check.Expect(Succeeded(runtime.SetComponentProperty(camera,
            "field_of_view_degrees", Reflection::PropertyValue::MakeFloat(35.0f))),
            "Camera の FOV を名前で書ける");
        Reflection::PropertyValue read;
        check.Expect(Succeeded(runtime.GetComponentProperty(camera,
            "field_of_view_degrees", read)) && Near(static_cast<float>(read.AsDouble()), 35.0f),
            "Camera の FOV を名前で読める");

        check.Expect(Succeeded(runtime.SetComponentProperty(camera,
            "near_clip", Reflection::PropertyValue::MakeFloat(0.25f))) &&
            Succeeded(runtime.GetComponentProperty(camera, "near_clip", read)) &&
            Near(static_cast<float>(read.AsDouble()), 0.25f),
            "Camera の Near Clip を往復できる");

        check.Expect(Failed(runtime.GetComponentProperty(camera, "no_such_property", read)),
            "知らないプロパティ名は失敗する");

        // ---- 汎用プロパティ: Light（色 = Vector4 系） ------------------------
        ObjectHandle light_object;
        runtime.CreateGameObject("Light", light_object);
        ComponentHandle light;
        check.Expect(Succeeded(runtime.AddComponent(light_object,
            Components::DirectionalLightComponent::StaticTypeID(), light)),
            "Directional Light を追加できる");
        check.Expect(Succeeded(runtime.SetComponentProperty(light, "color",
            Reflection::PropertyValue::MakeColor({ 0.25f, 0.5f, 0.75f, 1.0f }))) &&
            Succeeded(runtime.GetComponentProperty(light, "color", read)) &&
            Near(read.AsVector4().y, 0.5f),
            "Light の色を往復できる");
        check.Expect(Succeeded(runtime.SetComponentProperty(light, "intensity",
            Reflection::PropertyValue::MakeFloat(4.0f))) &&
            Succeeded(runtime.GetComponentProperty(light, "intensity", read)) &&
            Near(static_cast<float>(read.AsDouble()), 4.0f),
            "Light の強さを往復できる");

        // ---- 汎用プロパティ: Renderer（bool） --------------------------------
        ObjectHandle mesh_object;
        runtime.CreateGameObject("Mesh", mesh_object);
        ComponentHandle renderer;
        check.Expect(Succeeded(runtime.AddComponent(mesh_object,
            Components::MeshRendererComponent::StaticTypeID(), renderer)),
            "Mesh Renderer を追加できる");
        check.Expect(Succeeded(runtime.SetComponentProperty(renderer, "visible",
            Reflection::PropertyValue::MakeBool(false))) &&
            Succeeded(runtime.GetComponentProperty(renderer, "visible", read)) &&
            !read.AsBool(),
            "Renderer の表示フラグを往復できる");
        check.Expect(Succeeded(runtime.SetComponentProperty(renderer, "cast_shadow",
            Reflection::PropertyValue::MakeBool(false))) &&
            Succeeded(runtime.GetComponentProperty(renderer, "cast_shadow", read)) &&
            !read.AsBool(),
            "Renderer の Cast Shadow を往復できる");

        // ---- World Transform ------------------------------------------------
        ObjectHandle parent;
        runtime.CreateGameObject("Parent", parent);
        runtime.SetLocalPosition(parent, { 10.0f, 0.0f, 0.0f });
        runtime.SetParent(actor, parent, false);

        check.Expect(Succeeded(runtime.SetWorldPosition(actor, { 1.0f, 2.0f, 3.0f })),
            "ワールド座標を設定できる");
        DirectX::XMFLOAT3 world_position{};
        check.Expect(Succeeded(runtime.GetWorldPosition(actor, world_position)) &&
            Near(world_position.x, 1.0f) && Near(world_position.y, 2.0f) &&
            Near(world_position.z, 3.0f),
            "親がいてもワールド座標が往復する");

        DirectX::XMFLOAT3 local_position{};
        check.Expect(Succeeded(runtime.GetLocalPosition(actor, local_position)) &&
            Near(local_position.x, -9.0f),
            "ワールド座標の設定がローカルへ正しく落ちる");

        DirectX::XMFLOAT3 world_scale{};
        check.Expect(Succeeded(runtime.SetWorldScale(actor, { 2.0f, 2.0f, 2.0f })) &&
            Succeeded(runtime.GetWorldScale(actor, world_scale)) &&
            Near(world_scale.x, 2.0f),
            "ワールド拡縮を往復できる");

        DirectX::XMFLOAT4 rotation{};
        check.Expect(Succeeded(runtime.GetWorldRotationQuaternion(actor, rotation)),
            "ワールド回転を四元数で取得できる");

        // ローカル -> ワールド四元数 -> ローカルで元の角度へ戻ること。
        {
            ObjectHandle solo;
            runtime.CreateGameObject("Solo", solo);
            runtime.SetLocalRotationEuler(solo, { 0.0f, 0.5f, 0.0f });
            DirectX::XMFLOAT4 solo_rotation{};
            runtime.GetWorldRotationQuaternion(solo, solo_rotation);
            runtime.SetWorldRotationQuaternion(solo, solo_rotation);
            DirectX::XMFLOAT3 solo_euler{};
            runtime.GetLocalRotationEuler(solo, solo_euler);
            check.Expect(Near(solo_euler.y, 0.5f, 1.0e-3f),
                "回転がローカル・ワールド間で往復する");
        }
        check.Expect(Failed(runtime.SetWorldRotationQuaternion(actor,
            { 0.0f, 0.0f, 0.0f, 0.0f })),
            "長さ 0 の四元数は拒否する");

        DirectX::XMFLOAT3 forward{}, right{}, up{};
        check.Expect(Succeeded(runtime.GetWorldAxes(actor, forward, right, up)) &&
            Near(std::sqrt(forward.x * forward.x + forward.y * forward.y +
                forward.z * forward.z), 1.0f),
            "前後左右上の軸が単位ベクトルで返る");

        // 原点から +X を見れば forward はほぼ +X になる。
        runtime.SetWorldPosition(actor, { 0.0f, 0.0f, 0.0f });
        check.Expect(Succeeded(runtime.LookAt(actor, { 5.0f, 0.0f, 0.0f },
            { 0.0f, 1.0f, 0.0f })),
            "LookAt で目標を向ける");
        check.Expect(Succeeded(runtime.GetWorldAxes(actor, forward, right, up)) &&
            Near(forward.x, 1.0f, 1.0e-2f),
            "LookAt 後の前方向が目標を向いている");
        check.Expect(Failed(runtime.LookAt(actor, { 0.0f, 0.0f, 0.0f },
            { 0.0f, 1.0f, 0.0f })),
            "自分と同じ位置は LookAt できない");

        // ---- Rigidbody -------------------------------------------------------
        ObjectHandle body_object;
        runtime.CreateGameObject("Body", body_object);
        ComponentHandle body;
        check.Expect(Succeeded(runtime.AddComponent(body_object,
            Components::RigidbodyComponent::StaticTypeID(), body)),
            "Rigidbody を追加できる");

        check.Expect(Succeeded(runtime.RigidbodySetLinearVelocity(body,
            { 3.0f, 0.0f, 0.0f })),
            "速度を設定できる");
        DirectX::XMFLOAT3 velocity{};
        check.Expect(Succeeded(runtime.RigidbodyGetLinearVelocity(body, velocity)) &&
            Near(velocity.x, 3.0f),
            "設定した速度を読み戻せる");
        check.Expect(Succeeded(runtime.RigidbodySetAngularVelocity(body,
            { 0.0f, 1.5f, 0.0f })) &&
            Succeeded(runtime.RigidbodyGetAngularVelocity(body, velocity)) &&
            Near(velocity.y, 1.5f),
            "角速度を往復できる");

        check.Expect(Succeeded(runtime.RigidbodyAddForce(body, { 0.0f, 100.0f, 0.0f })),
            "力を積める");
        check.Expect(Succeeded(runtime.RigidbodyAddTorque(body, { 0.0f, 0.0f, 5.0f })),
            "トルクを積める");
        check.Expect(Succeeded(runtime.RigidbodyClearForces(body)),
            "積んだ力を捨てられる");
        check.Expect(Succeeded(runtime.RigidbodyTeleport(body, { 4.0f, 5.0f, 6.0f },
            { 0.0f, 0.0f, 0.0f })),
            "Rigidbody を Teleport できる");
        DirectX::XMFLOAT3 teleported{};
        check.Expect(Succeeded(runtime.GetLocalPosition(body_object, teleported)) &&
            Near(teleported.y, 5.0f),
            "Teleport 後の座標が反映される");

        check.Expect(Failed(runtime.RigidbodyAddForce(body,
            { std::nanf(""), 0.0f, 0.0f })),
            "NaN の力は拒否する");
        check.Expect(Failed(runtime.RigidbodyAddForce(camera, { 0.0f, 1.0f, 0.0f })),
            "Rigidbody 以外の Handle は型違いで弾く");

        // 読み取り専用プロパティは汎用 API から書けない。
        check.Expect(Failed(runtime.SetComponentProperty(body, "linear_velocity",
            Reflection::PropertyValue::MakeVector3({ 9.0f, 0.0f, 0.0f }))),
            "読み取り専用プロパティは汎用 API では書けない");
        check.Expect(Succeeded(runtime.GetComponentProperty(body, "linear_velocity", read)) &&
            Near(read.AsVector3().x, 3.0f),
            "読み取り専用でも読むことはできる");

        // ---- Animator / Audio / Particle 実行命令 ---------------------------
        {
            ObjectHandle effects_object;
            runtime.CreateGameObject("RuntimeEffects", effects_object);

            ComponentHandle animator_handle;
            check.Expect(Succeeded(runtime.AddComponent(effects_object,
                Components::AnimatorComponent::StaticTypeID(), animator_handle)),
                "Animator を Runtime API から追加できる");
            Core::GameObject* effects = world.FindGameObjectByID(effects_object.object);
            auto* animator = effects != nullptr
                ? effects->GetComponent<Components::AnimatorComponent>() : nullptr;
            if (animator != nullptr)
            {
                animator->SetStateCount(2);
                animator->states[0].name = "Idle";
                animator->states[0].clip = 1;
                animator->states[1].name = "Run";
                animator->states[1].clip = 2;
                animator->OnStart();
            }
            check.Expect(animator != nullptr && Succeeded(runtime.InvokeComponentCommand(
                animator_handle, ComponentCommand::AnimatorPlayState, "Run", 0.1f,
                0.5f)) && animator->CurrentStateName() == "Run" &&
                Near(animator->AnimationTime(), 0.5f),
                "Animator の State・ブレンド・開始時刻を命令できる");
            check.Expect(Succeeded(runtime.InvokeComponentCommand(animator_handle,
                ComponentCommand::AnimatorSetBool, "grounded", 0.0f, 0.0f, 1)) &&
                animator != nullptr && animator->GetBool("grounded"),
                "Animator の Bool Parameter を設定できる");
            check.Expect(Succeeded(runtime.InvokeComponentCommand(animator_handle,
                ComponentCommand::AnimatorSetFloat, "speed", 3.5f)) &&
                animator != nullptr && Near(animator->GetFloat("speed"), 3.5f),
                "Animator の Float Parameter を設定できる");

            ComponentHandle particle_handle;
            check.Expect(Succeeded(runtime.AddComponent(effects_object,
                Components::ParticleEmitterComponent::StaticTypeID(), particle_handle)),
                "Particle Emitter を Runtime API から追加できる");
            auto* particle = effects != nullptr
                ? effects->GetComponent<Components::ParticleEmitterComponent>() : nullptr;
            check.Expect(particle != nullptr && Succeeded(runtime.InvokeComponentCommand(
                particle_handle, ComponentCommand::ParticleEmit, std::string(),
                0.0f, 0.0f, 64)) && particle->ConsumeBurst() == 64,
                "Particle の Burst 発生数が描画側へ渡る");
            check.Expect(particle != nullptr && Succeeded(runtime.InvokeComponentCommand(
                particle_handle, ComponentCommand::ParticleClear)) &&
                particle->ConsumeClearRequest(),
                "Particle の Clear 要求が描画側へ渡る");

            ComponentHandle audio_handle;
            check.Expect(Succeeded(runtime.AddComponent(effects_object,
                Components::AudioSourceComponent::StaticTypeID(), audio_handle)),
                "Audio Source を Runtime API から追加できる");
            check.Expect(runtime.InvokeComponentCommand(audio_handle,
                ComponentCommand::AudioPlay) == RuntimeStatus::ServiceUnavailable,
                "Audio Service 未接続の Play は成功扱いにしない");
            check.Expect(Succeeded(runtime.InvokeComponentCommand(audio_handle,
                ComponentCommand::AudioStop)),
                "Audio Stop は未再生でも安全に完了する");
        }

        // ---- UI Slider / ComponentReference / AssetReference ----------------
        {
            ObjectHandle canvas_object;
            ObjectHandle slider_object;
            runtime.CreateGameObject("Canvas", canvas_object);
            runtime.CreateGameObject("Slider", slider_object);
            ComponentHandle canvas;
            ComponentHandle image;
            ComponentHandle slider_handle;
            check.Expect(Succeeded(runtime.AddComponent(canvas_object,
                Components::CanvasComponent::StaticTypeID(), canvas)) &&
                Succeeded(runtime.AddComponent(slider_object,
                    Components::UIImageComponent::StaticTypeID(), image)) &&
                Succeeded(runtime.AddComponent(slider_object,
                    Components::UISliderComponent::StaticTypeID(), slider_handle)) &&
                Succeeded(runtime.SetParent(slider_object, canvas_object, false)),
                "Slider と描画用 Image を Canvas 配下へ作れる");

            Core::GameObject* slider_owner = world.FindGameObjectByID(slider_object.object);
            auto* slider = slider_owner != nullptr
                ? slider_owner->GetComponent<Components::UISliderComponent>() : nullptr;
            auto* image_component = slider_owner != nullptr
                ? slider_owner->GetComponent<Components::UIImageComponent>() : nullptr;
            Reflection::ComponentReference image_reference;
            if (image_component != nullptr)
            {
                image_reference.owner = slider_owner->ID();
                image_reference.component = image_component->StableID();
            }
            check.Expect(slider != nullptr && image_component != nullptr &&
                Succeeded(runtime.SetComponentProperty(slider_handle, "fill_image",
                    Reflection::PropertyValue::MakeComponentReference(image_reference))) &&
                Succeeded(runtime.GetComponentProperty(slider_handle, "fill_image", read)) &&
                read.AsComponentReference() == image_reference,
                "Slider の ComponentReference を汎用 API で往復できる");

            namespace CSharpDetail = Scripting::CSharp::Detail;
            RuntimeContext* previous_context = CSharpDetail::g_runtime_context;
            CSharpDetail::g_runtime_context = &runtime;
            const CSharpDetail::NativeApiTable table = CSharpDetail::MakeNativeApiTable();
            std::uint64_t reference_owner = 0;
            std::uint32_t reference_component = 0;
            ComponentHandle resolved_reference;
            check.Expect(table.get_component_property_component_reference != nullptr &&
                table.get_component_property_component_reference(slider_handle, "fill_image",
                    &reference_owner, &reference_component) == 0 &&
                reference_owner == slider_object.object.Value() &&
                reference_component == image_reference.component,
                "ComponentReference を Native ABI から読める");
            check.Expect(table.component_to_reference != nullptr &&
                table.resolve_component_reference != nullptr &&
                table.component_to_reference(image, &reference_owner,
                    &reference_component) == 0 &&
                table.resolve_component_reference(reference_owner, reference_component,
                    &resolved_reference) == 0 && resolved_reference == image,
                "ComponentHandle と保存参照を相互変換できる");

            check.Expect(table.set_component_property_string(image, "sprite",
                "validation-image-guid") == 0,
                "AssetReference を文字列 API から設定できる");
            char sprite_guid[128]{};
            check.Expect(table.get_component_property_string(image, "sprite", sprite_guid,
                static_cast<int>(sizeof(sprite_guid))) == 0 &&
                std::string(sprite_guid) == "validation-image-guid",
                "AssetReference を文字列 API から取得できる");
            CSharpDetail::g_runtime_context = previous_context;

            if (slider != nullptr && image_component != nullptr)
            {
                slider->minimum = 0.0f;
                slider->maximum = 10.0f;
                slider->SetValue(2.0f);
                int slider_events = 0;
                Runtime::ScopedSubscription slider_token = runtime.Events().Subscribe(
                    Runtime::EngineEvents::SliderValueChanged,
                    [&slider_events](const Runtime::EventRecord& event)
                    {
                        if (event.payload.Find("slider_component") != nullptr) ++slider_events;
                    });
                UI::UILayout::Resolve(world, 1920.0f, 1080.0f);
                UI::UILayout::UpdateButtons(world, 1920.0f, 1080.0f,
                    985.0f, 540.0f, true, true, false, 0.0f, false, false);
                UI::UILayout::UpdateButtons(world, 1920.0f, 1080.0f,
                    985.0f, 540.0f, false, false, true, 0.0f, false, false);
                runtime.Events().Dispatch(&runtime.Resolver());
                check.Expect(Near(slider->value, 7.5f, 0.05f) &&
                    Near(image_component->fill_amount, 0.75f, 0.01f),
                    "Pointer 操作が Slider 値と Image の Fill へ反映される");
                check.Expect(slider_events == 1,
                    "Slider の値変更イベントが一度だけ届く");
            }
            else
            {
                check.Expect(false, "Pointer 操作が Slider 値と Image の Fill へ反映される");
                check.Expect(false, "Slider の値変更イベントが一度だけ届く");
            }
        }


        // ---- v11 生デバイス入力 ---------------------------------------------
        //
        // 実体のサービスは Editor 側にしかないので、ここでは
        // 「未接続なら ServiceUnavailable を返す」ことと引数検証を確かめる。
        {
            bool flag = false;
            float value = 0.0f;
            check.Expect(runtime.InputKeyHeld(static_cast<int>('A'), flag) ==
                RuntimeStatus::ServiceUnavailable,
                "Input Service 未接続なら生キーは ServiceUnavailable");
            check.Expect(runtime.InputWheelDelta(value) ==
                RuntimeStatus::ServiceUnavailable,
                "Input Service 未接続ならホイールは ServiceUnavailable");

            StubInputService input;
            runtime.SetInputService(&input);

            check.Expect(Succeeded(runtime.InputKeyHeld(static_cast<int>('A'), flag)) && flag,
                "押されているキーを読める");
            check.Expect(Succeeded(runtime.InputKeyHeld(static_cast<int>('B'), flag)) && !flag,
                "押されていないキーは false");
            check.Expect(Failed(runtime.InputKeyHeld(0, flag)),
                "範囲外のキーコードは弾く");
            check.Expect(Failed(runtime.InputKeyHeld(999, flag)),
                "255 を超えるキーコードは弾く");

            check.Expect(Succeeded(runtime.InputMouseButtonHeld(0, flag)) && flag,
                "左ボタンを読める");
            check.Expect(Failed(runtime.InputMouseButtonHeld(9, flag)),
                "存在しないマウスボタンは弾く");

            float x = 0.0f, y = 0.0f;
            check.Expect(Succeeded(runtime.InputPointerPosition(x, y)) &&
                Near(x, 320.0f) && Near(y, 240.0f),
                "ポインタ座標を読める");
            check.Expect(Succeeded(runtime.InputWheelDelta(value)) && Near(value, 2.0f),
                "ホイール量を読める");

            check.Expect(Succeeded(runtime.InputGamepadConnected(0, flag)) && flag,
                "ゲームパッドの接続を読める");
            check.Expect(Succeeded(runtime.InputGamepadButtonHeld(0, 0x1000, flag)) && flag,
                "ゲームパッドのボタンを読める");
            check.Expect(Failed(runtime.InputGamepadButtonHeld(0, 0, flag)),
                "ボタン 0 は弾く");
            check.Expect(Succeeded(runtime.InputGamepadAxis(0, 0, value)) &&
                Near(value, 0.5f),
                "スティックの値を読める");
            check.Expect(Failed(runtime.InputGamepadAxis(0, 9, value)),
                "存在しない軸は弾く");
            check.Expect(Failed(runtime.InputGamepadAxis(-1, 0, value)),
                "負のプレイヤー番号は弾く");
            check.Expect(Succeeded(runtime.InputSetGamepadVibration(0, 0.5f, 0.5f)),
                "振動を設定できる");
            check.Expect(Failed(runtime.InputSetGamepadVibration(0,
                std::nanf(""), 0.0f)),
                "NaN の振動量は弾く");

            runtime.SetInputService(nullptr);
        }

        // ---- v11 遅延生成の結果引き取り --------------------------------------
        {
            RuntimeContext::SpawnRequestID request = 0;
            // Instantiator 未接続なので要求自体が通らない。
            check.Expect(Failed(runtime.InstantiatePrefabDeferredTracked("guid",
                {}, {}, { 1.0f, 1.0f, 1.0f }, ObjectHandle::None(), request)),
                "Prefab Instantiator 未接続なら遅延生成は失敗する");
            check.Expect(request == 0, "失敗した要求には番号が振られない");

            ObjectHandle taken;
            check.Expect(Failed(runtime.TryTakeSpawnResult(0, taken)),
                "番号 0 の引き取りは弾く");
            check.Expect(Failed(runtime.TryTakeSpawnResult(12345, taken)),
                "知らない番号の引き取りは弾く");
            check.Expect(runtime.PendingSpawnResultCount() == 0,
                "引き取り待ちの結果は残っていない");
        }

        // ---- 接触が EventBus へ流れる -----------------------------------------
        {
            int collision_events = 0;
            int trigger_events = 0;
            Runtime::ScopedSubscription collision_token = runtime.Events().Subscribe(
                Runtime::EngineEvents::CollisionEnter,
                [&collision_events](const Runtime::EventRecord&) { ++collision_events; });
            Runtime::ScopedSubscription trigger_token = runtime.Events().Subscribe(
                Runtime::EngineEvents::TriggerEnter,
                [&trigger_events](const Runtime::EventRecord&) { ++trigger_events; });
            check.Expect(collision_token.Valid() && trigger_token.Valid(),
                "接触イベントを購読できる");

            Runtime::EventRecord probe;
            probe.type = Runtime::EngineEvents::CollisionEnter;
            probe.type_name = "CollisionEnter";
            probe.source = actor;
            probe.payload.Set("normal_y", Reflection::PropertyValue::MakeFloat(1.0f));
            runtime.Events().Publish(std::move(probe));
            runtime.Events().Dispatch(&runtime.Resolver());
            check.Expect(collision_events == 1,
                "CollisionEnter が購読者へ届く");
            check.Expect(trigger_events == 0,
                "別種のイベントは届かない");

            check.Expect(runtime.Events().HasSubscribers(
                Runtime::EngineEvents::CollisionEnter),
                "購読中の型は HasSubscribers が true");
            check.Expect(!runtime.Events().HasSubscribers(
                Runtime::EngineEvents::CollisionStay),
                "購読していない型は HasSubscribers が false");
        }

        // 購読を捨てたあとは false へ戻る。接触の発行側はこれを見て抜ける。
        check.Expect(!runtime.Events().HasSubscribers(
            Runtime::EngineEvents::CollisionEnter),
            "購読を解除すると HasSubscribers が false へ戻る");

        return check.Report("component-api validation");
    }
}
