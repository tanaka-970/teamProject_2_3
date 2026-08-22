// v10 で足した C# 向け API の実行検証。
//
// 検証するのは C++ 側の RuntimeContext と Native API 表のヘッダー。
// C# の wrapper はこの API をそのまま呼ぶだけなので、ここが通れば経路は通る。

#include "BehaviourValidationInternal.h"

#include "../../Components/Camera/CameraComponent.h"
#include "../../Components/Physics/RigidbodyComponent.h"
#include "../../Components/Rendering/LightComponents.h"
#include "../../Components/Rendering/MeshRendererComponent.h"
#include "../../Object/Registry/BuiltInComponents.h"
#include "../../Reflection/Property/PropertyValue.h"
#include "../../Scripting/CSharp/CSharpScriptBackendNativeInternal.h"

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
                table.rigidbody_add_force != nullptr,
                "v10 で足した callback が表へ入っている");
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

        return check.Report("component-api validation");
    }
}
