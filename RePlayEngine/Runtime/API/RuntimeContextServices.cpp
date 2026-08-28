#include "RuntimeContext.h"

#include "../Events/EventBus.h"
#include "../../Components/Motion/MotionPlayerComponent.h"
#include "../../Components/UI/UIButtonComponent.h"
#include "../../Components/UI/UIImageComponent.h"
#include "../../Components/UI/RectTransformComponent.h"
#include "../../Components/UI/UITextComponent.h"
#include "../../Object/Component/Component.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../../Scene/Runtime/Scene.h"

#include <cmath>

namespace ReplayEngine::Runtime
{
    using Core::Component;
    using Core::GameObject;
    using Core::ObjectID;

    namespace
    {
        bool Finite(float value) noexcept { return std::isfinite(value); }

        bool Finite(const DirectX::XMFLOAT3& value) noexcept
        {
            return Finite(value.x) && Finite(value.y) && Finite(value.z);
        }

        bool Finite(const DirectX::XMFLOAT4& value) noexcept
        {
            return Finite(value.x) && Finite(value.y) &&
                Finite(value.z) && Finite(value.w);
        }

        bool Finite(const DirectX::XMFLOAT2& value) noexcept
        {
            return Finite(value.x) && Finite(value.y);
        }

        RuntimeStatus ResolveAudioParams(const std::string& clip_path, bool loop,
            float volume, float pitch, int spatial_mode,
            const DirectX::XMFLOAT3& position, float min_distance,
            float max_distance, Audio::AudioPlaybackParams& out) noexcept
        {
            if (clip_path.empty() || !Finite(volume) || !Finite(pitch) ||
                !Finite(position) || !Finite(min_distance) || !Finite(max_distance) ||
                min_distance < 0.0f || max_distance < min_distance ||
                (spatial_mode != static_cast<int>(Audio::AudioSpatialMode::TwoD) &&
                    spatial_mode != static_cast<int>(Audio::AudioSpatialMode::ThreeD)))
            {
                return RuntimeStatus::InvalidArgument;
            }

            out.clip_path = clip_path;
            out.loop = loop;
            out.volume = volume;
            out.pitch = pitch;
            out.spatial_mode = static_cast<Audio::AudioSpatialMode>(spatial_mode);
            out.position = position;
            out.min_distance = min_distance;
            out.max_distance = max_distance;
            return RuntimeStatus::Ok;
        }
    }

    // ---- Input Action -------------------------------------------------------

    bool RuntimeContext::InputActionAvailable() const noexcept
    {
        return input_service_ != nullptr;
    }

    RuntimeStatus RuntimeContext::InputHeld(const std::string& action, int player_slot,
        bool& out) const
    {
        if (input_service_ == nullptr) return RuntimeStatus::ServiceUnavailable;
        if (player_slot < 0 || action.empty() || !input_service_->ActionAvailable(action))
            return RuntimeStatus::InvalidArgument;
        out = input_service_->Held(action, player_slot);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::InputPressed(const std::string& action, int player_slot,
        bool& out) const
    {
        if (input_service_ == nullptr) return RuntimeStatus::ServiceUnavailable;
        if (player_slot < 0 || action.empty() || !input_service_->ActionAvailable(action))
            return RuntimeStatus::InvalidArgument;
        out = input_service_->Pressed(action, player_slot);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::InputReleased(const std::string& action, int player_slot,
        bool& out) const
    {
        if (input_service_ == nullptr) return RuntimeStatus::ServiceUnavailable;
        if (player_slot < 0 || action.empty() || !input_service_->ActionAvailable(action))
            return RuntimeStatus::InvalidArgument;
        out = input_service_->Released(action, player_slot);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::InputAxis(const std::string& axis, int player_slot,
        float& out) const
    {
        if (input_service_ == nullptr) return RuntimeStatus::ServiceUnavailable;
        if (player_slot < 0 || axis.empty() || !input_service_->AxisAvailable(axis))
            return RuntimeStatus::InvalidArgument;
        out = input_service_->Axis(axis, player_slot);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::InputPointerDeltaX(float& out) const
    {
        if (input_service_ == nullptr) return RuntimeStatus::ServiceUnavailable;
        out = input_service_->PointerDeltaX();
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::InputPointerDeltaY(float& out) const
    {
        if (input_service_ == nullptr) return RuntimeStatus::ServiceUnavailable;
        out = input_service_->PointerDeltaY();
        return RuntimeStatus::Ok;
    }

    // ---- Audio --------------------------------------------------------------

    bool RuntimeContext::AudioAvailable() const noexcept
    {
        return audio_service_ != nullptr && audio_service_->Available();
    }

    RuntimeStatus RuntimeContext::PlayAudio(const std::string& clip_path, bool loop,
        float volume, float pitch, int spatial_mode,
        const DirectX::XMFLOAT3& position, float min_distance,
        float max_distance, std::uint64_t& out) const
    {
        out = 0;
        if (!AudioAvailable()) return RuntimeStatus::ServiceUnavailable;
        Audio::AudioPlaybackParams params;
        const RuntimeStatus params_status = ResolveAudioParams(clip_path, loop, volume,
            pitch, spatial_mode, position, min_distance, max_distance, params);
        if (params_status != RuntimeStatus::Ok) return params_status;
        const Audio::AudioVoiceHandle handle = audio_service_->Play(params);
        if (!handle.Valid()) return RuntimeStatus::AssetMissing;
        out = handle.value;
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::StopAudio(std::uint64_t voice) const
    {
        if (!AudioAvailable()) return RuntimeStatus::ServiceUnavailable;
        if (voice == 0) return RuntimeStatus::InvalidArgument;
        audio_service_->Stop(Audio::AudioVoiceHandle{ voice });
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::UpdateAudio(std::uint64_t voice,
        const std::string& clip_path, bool loop, float volume, float pitch,
        int spatial_mode, const DirectX::XMFLOAT3& position,
        float min_distance, float max_distance) const
    {
        if (!AudioAvailable()) return RuntimeStatus::ServiceUnavailable;
        if (voice == 0) return RuntimeStatus::InvalidArgument;
        Audio::AudioPlaybackParams params;
        const RuntimeStatus params_status = ResolveAudioParams(clip_path, loop, volume,
            pitch, spatial_mode, position, min_distance, max_distance, params);
        if (params_status != RuntimeStatus::Ok) return params_status;
        audio_service_->UpdateVoice(Audio::AudioVoiceHandle{ voice }, params);
        return RuntimeStatus::Ok;
    }

    // ---- SaveGame -----------------------------------------------------------

    bool RuntimeContext::SaveGameAvailable() const noexcept
    {
        return save_game_service_ != nullptr && save_game_service_->Available();
    }

    RuntimeStatus RuntimeContext::SaveBool(const std::string& slot,
        const std::string& key, bool value) const
    {
        return SaveGameAvailable() ? save_game_service_->SetBool(slot, key, value)
            : RuntimeStatus::ServiceUnavailable;
    }

    RuntimeStatus RuntimeContext::SaveInt(const std::string& slot,
        const std::string& key, std::int64_t value) const
    {
        return SaveGameAvailable() ? save_game_service_->SetInt(slot, key, value)
            : RuntimeStatus::ServiceUnavailable;
    }

    RuntimeStatus RuntimeContext::SaveDouble(const std::string& slot,
        const std::string& key, double value) const
    {
        return SaveGameAvailable() ? save_game_service_->SetDouble(slot, key, value)
            : RuntimeStatus::ServiceUnavailable;
    }

    RuntimeStatus RuntimeContext::SaveString(const std::string& slot,
        const std::string& key, const std::string& value) const
    {
        return SaveGameAvailable() ? save_game_service_->SetString(slot, key, value)
            : RuntimeStatus::ServiceUnavailable;
    }

    RuntimeStatus RuntimeContext::LoadBool(const std::string& slot,
        const std::string& key, bool& out) const
    {
        return SaveGameAvailable() ? save_game_service_->GetBool(slot, key, out)
            : RuntimeStatus::ServiceUnavailable;
    }

    RuntimeStatus RuntimeContext::LoadInt(const std::string& slot,
        const std::string& key, std::int64_t& out) const
    {
        return SaveGameAvailable() ? save_game_service_->GetInt(slot, key, out)
            : RuntimeStatus::ServiceUnavailable;
    }

    RuntimeStatus RuntimeContext::LoadDouble(const std::string& slot,
        const std::string& key, double& out) const
    {
        return SaveGameAvailable() ? save_game_service_->GetDouble(slot, key, out)
            : RuntimeStatus::ServiceUnavailable;
    }

    RuntimeStatus RuntimeContext::LoadString(const std::string& slot,
        const std::string& key, std::string& out) const
    {
        return SaveGameAvailable() ? save_game_service_->GetString(slot, key, out)
            : RuntimeStatus::ServiceUnavailable;
    }

    RuntimeStatus RuntimeContext::HasSaveKey(const std::string& slot,
        const std::string& key, bool& out) const
    {
        return SaveGameAvailable() ? save_game_service_->HasKey(slot, key, out)
            : RuntimeStatus::ServiceUnavailable;
    }

    RuntimeStatus RuntimeContext::DeleteSaveKey(const std::string& slot,
        const std::string& key) const
    {
        return SaveGameAvailable() ? save_game_service_->DeleteKey(slot, key)
            : RuntimeStatus::ServiceUnavailable;
    }

    RuntimeStatus RuntimeContext::SaveGame(const std::string& slot) const
    {
        return SaveGameAvailable() ? save_game_service_->Save(slot)
            : RuntimeStatus::ServiceUnavailable;
    }

    RuntimeStatus RuntimeContext::LoadGame(const std::string& slot) const
    {
        return SaveGameAvailable() ? save_game_service_->Load(slot)
            : RuntimeStatus::ServiceUnavailable;
    }

    RuntimeStatus RuntimeContext::DeleteSave(const std::string& slot) const
    {
        return SaveGameAvailable() ? save_game_service_->DeleteSlot(slot)
            : RuntimeStatus::ServiceUnavailable;
    }

    // ---- Runtime UI ---------------------------------------------------------

    bool RuntimeContext::RuntimeUIAvailable() const noexcept
    {
        return world_ != nullptr;
    }

    RuntimeStatus RuntimeContext::CreateUIElement(const std::string& name,
        const ObjectHandle& parent, ObjectHandle& out)
    {
        out = ObjectHandle::None();
        if (!RuntimeUIAvailable() || name.empty()) return RuntimeStatus::InvalidArgument;

        RuntimeStatus status = RuntimeStatus::Ok;
        GameObject* parent_object = nullptr;
        if (!parent.IsEmpty())
        {
            parent_object = ResolveObject(parent, status);
            if (parent_object == nullptr) return status;
        }

        GameObject* object = world_->CreateGameObject(name);
        if (object == nullptr) return RuntimeStatus::UnsupportedOperation;
        if (parent_object != nullptr && !object->SetParent(parent_object, false))
        {
            world_->DestroyGameObject(object);
            return RuntimeStatus::InvalidArgument;
        }

        if (object->AddComponent(Components::RectTransformComponent::StaticTypeID()) == nullptr)
        {
            world_->DestroyGameObject(object);
            return RuntimeStatus::UnsupportedOperation;
        }
        out = resolver_.MakeHandle(object);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::SetUIText(const ObjectHandle& handle,
        const std::string& text)
    {
        RuntimeStatus status = RuntimeStatus::Ok;
        GameObject* object = ResolveObject(handle, status);
        if (object == nullptr) return status;
        auto* component = object->GetComponent<Components::UITextComponent>();
        if (component == nullptr) return RuntimeStatus::ComponentNotFound;
        component->text = text;
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::GetUIText(const ObjectHandle& handle,
        std::string& out) const
    {
        RuntimeStatus status = RuntimeStatus::Ok;
        GameObject* object = ResolveObject(handle, status);
        if (object == nullptr) return status;
        const auto* component = object->GetComponent<Components::UITextComponent>();
        if (component == nullptr) return RuntimeStatus::ComponentNotFound;
        out = component->text;
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::SetUIImageColor(const ObjectHandle& handle,
        const DirectX::XMFLOAT4& color)
    {
        if (!Finite(color)) return RuntimeStatus::InvalidArgument;
        RuntimeStatus status = RuntimeStatus::Ok;
        GameObject* object = ResolveObject(handle, status);
        if (object == nullptr) return status;
        auto* component = object->GetComponent<Components::UIImageComponent>();
        if (component == nullptr) return RuntimeStatus::ComponentNotFound;
        component->color = color;
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::SetUIRect(const ObjectHandle& handle,
        const DirectX::XMFLOAT2& position, const DirectX::XMFLOAT2& size,
        const DirectX::XMFLOAT2& scale, float rotation, int sort_order)
    {
        if (!Finite(position) || !Finite(size) || !Finite(scale) || !Finite(rotation) ||
            size.x < 0.0f || size.y < 0.0f)
            return RuntimeStatus::InvalidArgument;
        RuntimeStatus status = RuntimeStatus::Ok;
        GameObject* object = ResolveObject(handle, status);
        if (object == nullptr) return status;
        auto* component = object->GetComponent<Components::RectTransformComponent>();
        if (component == nullptr) return RuntimeStatus::ComponentNotFound;
        component->anchored_position = position;
        component->size_delta = size;
        component->scale = scale;
        component->rotation = rotation;
        component->sort_order = sort_order;
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::SetUIButtonInteractable(const ObjectHandle& handle,
        bool interactable)
    {
        RuntimeStatus status = RuntimeStatus::Ok;
        GameObject* object = ResolveObject(handle, status);
        if (object == nullptr) return status;
        auto* component = object->GetComponent<Components::UIButtonComponent>();
        if (component == nullptr) return RuntimeStatus::ComponentNotFound;
        component->interactable = interactable;
        return RuntimeStatus::Ok;
    }

    // ---- Physics ------------------------------------------------------------

    bool RuntimeContext::PhysicsAvailable() const noexcept
    {
        const Scene::IPhysicsQueryService* physics = world_->Services().Physics();
        return physics != nullptr && physics->CollisionAvailable();
    }

    RuntimeStatus RuntimeContext::QueryGround(const DirectX::XMFLOAT3& origin, float radius,
        float up_offset, float down_distance, float walkable_normal_y,
        const ObjectHandle& ignore, Scene::GroundHit& out) const
    {
        const Scene::IPhysicsQueryService* physics = world_->Services().Physics();
        if (physics == nullptr || !physics->CollisionAvailable())
        {
            return RuntimeStatus::ServiceUnavailable;
        }

        Scene::CollisionQueryFilter filter;
        if (!ignore.IsEmpty()) filter.ignore_object = ignore.object;

        physics->QueryGroundFiltered(origin, radius, up_offset, down_distance,
            walkable_normal_y, filter, out);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::SweepSphere(const DirectX::XMFLOAT3& start,
        const DirectX::XMFLOAT3& end, float radius, float maximum_normal_y,
        const ObjectHandle& ignore, Scene::SphereSweepHit& out) const
    {
        const Scene::IPhysicsQueryService* physics = world_->Services().Physics();
        if (physics == nullptr || !physics->CollisionAvailable())
        {
            return RuntimeStatus::ServiceUnavailable;
        }

        Scene::CollisionQueryFilter filter;
        if (!ignore.IsEmpty()) filter.ignore_object = ignore.object;

        physics->SweepSphereFiltered(start, end, radius, maximum_normal_y, filter, out);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::Raycast(const DirectX::XMFLOAT3& origin,
        const DirectX::XMFLOAT3& direction, float max_distance,
        int layer, int mask, const ObjectHandle& ignore, Scene::RaycastHit& out) const
    {
        out = Scene::RaycastHit{};
        if (max_distance <= 0.0f) return RuntimeStatus::InvalidArgument;

        const Scene::IPhysicsQueryService* physics = world_->Services().Physics();
        if (physics == nullptr || !physics->CollisionAvailable())
            return RuntimeStatus::ServiceUnavailable;

        Scene::CollisionQueryFilter filter;
        filter.layer = layer;
        filter.mask = mask;
        if (!ignore.IsEmpty()) filter.ignore_object = ignore.object;

        physics->RaycastFiltered(origin, direction, max_distance, filter, out);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::PhysicsQuery(const PhysicsQueryRequest& request,
        std::vector<Scene::PhysicsQueryHit>& out) const
    {
        out.clear();
        const Scene::IPhysicsQueryService* physics = world_->Services().Physics();
        if (physics == nullptr || !physics->CollisionAvailable())
            return RuntimeStatus::ServiceUnavailable;

        Scene::CollisionQueryFilter filter;
        filter.layer = request.layer;
        filter.mask = request.mask;
        if (!request.ignore.IsEmpty())
        {
            RuntimeStatus status = RuntimeStatus::Ok;
            if (ResolveObject(request.ignore, status) == nullptr) return status;
            filter.ignore_object = request.ignore.object;
        }

        switch (request.kind)
        {
        case PhysicsQueryKind::RaycastAll:
            if (request.max_distance <= 0.0f) return RuntimeStatus::InvalidArgument;
            physics->RaycastAllFiltered(request.point_a, request.direction,
                request.max_distance, filter, out);
            break;
        case PhysicsQueryKind::OverlapSphere:
            if (request.radius < 0.0f) return RuntimeStatus::InvalidArgument;
            physics->OverlapSphere(request.point_a, request.radius, filter, out);
            break;
        case PhysicsQueryKind::OverlapBox:
            physics->OverlapBox(request.point_a, request.half_extents,
                request.rotation, filter, out);
            break;
        case PhysicsQueryKind::OverlapCapsule:
            if (request.radius < 0.0f) return RuntimeStatus::InvalidArgument;
            physics->OverlapCapsule(request.point_a, request.point_b,
                request.radius, filter, out);
            break;
        case PhysicsQueryKind::SphereCast:
            physics->SphereCastAll(request.point_a, request.direction,
                request.radius, request.max_distance, filter, out);
            break;
        case PhysicsQueryKind::BoxCast:
            physics->BoxCastAll(request.point_a, request.half_extents,
                request.rotation, request.direction, request.max_distance, filter, out);
            break;
        case PhysicsQueryKind::CapsuleCast:
            physics->CapsuleCastAll(request.point_a, request.point_b, request.radius,
                request.direction, request.max_distance, filter, out);
            break;
        default:
            return RuntimeStatus::InvalidArgument;
        }
        return RuntimeStatus::Ok;
    }

    // ---- Log ----------------------------------------------------------------

    void RuntimeContext::Log(LogLevel level, const std::string& message,
        const ObjectHandle& source) const
    {
        // 出力先が無ければ捨てる。ここで OutputDebugString を直接呼ばない。
        // Runtime 層が Windows API と Editor の表示方法へ依存しないようにするため。
        if (log_sink_ != nullptr) log_sink_->Write(level, message, source);
    }

    void RuntimeContext::LogInfo(const std::string& message, const ObjectHandle& source) const
    {
        Log(LogLevel::Info, message, source);
    }

    void RuntimeContext::LogWarning(const std::string& message,
        const ObjectHandle& source) const
    {
        Log(LogLevel::Warning, message, source);
    }

    void RuntimeContext::LogError(const std::string& message,
        const ObjectHandle& source) const
    {
        Log(LogLevel::Error, message, source);
    }

    // ---- 生デバイス入力 --------------------------------------------------------

    RuntimeStatus RuntimeContext::InputKeyHeld(int key, bool& out) const
    {
        out = false;
        if (input_service_ == nullptr) return RuntimeStatus::ServiceUnavailable;
        if (key <= 0 || key > 255) return RuntimeStatus::InvalidArgument;
        out = input_service_->KeyHeld(key);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::InputKeyPressed(int key, bool& out) const
    {
        out = false;
        if (input_service_ == nullptr) return RuntimeStatus::ServiceUnavailable;
        if (key <= 0 || key > 255) return RuntimeStatus::InvalidArgument;
        out = input_service_->KeyPressed(key);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::InputKeyReleased(int key, bool& out) const
    {
        out = false;
        if (input_service_ == nullptr) return RuntimeStatus::ServiceUnavailable;
        if (key <= 0 || key > 255) return RuntimeStatus::InvalidArgument;
        out = input_service_->KeyReleased(key);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::InputMouseButtonHeld(int button, bool& out) const
    {
        out = false;
        if (input_service_ == nullptr) return RuntimeStatus::ServiceUnavailable;
        if (button < 0 || button > 4) return RuntimeStatus::InvalidArgument;
        out = input_service_->MouseButtonHeld(button);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::InputMouseButtonPressed(int button, bool& out) const
    {
        out = false;
        if (input_service_ == nullptr) return RuntimeStatus::ServiceUnavailable;
        if (button < 0 || button > 4) return RuntimeStatus::InvalidArgument;
        out = input_service_->MouseButtonPressed(button);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::InputMouseButtonReleased(int button, bool& out) const
    {
        out = false;
        if (input_service_ == nullptr) return RuntimeStatus::ServiceUnavailable;
        if (button < 0 || button > 4) return RuntimeStatus::InvalidArgument;
        out = input_service_->MouseButtonReleased(button);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::InputPointerPosition(float& out_x, float& out_y) const
    {
        out_x = 0.0f;
        out_y = 0.0f;
        if (input_service_ == nullptr) return RuntimeStatus::ServiceUnavailable;
        out_x = input_service_->PointerX();
        out_y = input_service_->PointerY();
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::InputWheelDelta(float& out) const
    {
        out = 0.0f;
        if (input_service_ == nullptr) return RuntimeStatus::ServiceUnavailable;
        out = input_service_->WheelDelta();
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::InputGamepadConnected(int player_slot, bool& out) const
    {
        out = false;
        if (input_service_ == nullptr) return RuntimeStatus::ServiceUnavailable;
        if (player_slot < 0) return RuntimeStatus::InvalidArgument;
        out = input_service_->GamepadConnected(player_slot);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::InputGamepadButtonHeld(int player_slot, int button,
        bool& out) const
    {
        out = false;
        if (input_service_ == nullptr) return RuntimeStatus::ServiceUnavailable;
        if (player_slot < 0 || button == 0) return RuntimeStatus::InvalidArgument;
        out = input_service_->GamepadButtonHeld(player_slot, button);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::InputGamepadButtonPressed(int player_slot, int button,
        bool& out) const
    {
        out = false;
        if (input_service_ == nullptr) return RuntimeStatus::ServiceUnavailable;
        if (player_slot < 0 || button == 0) return RuntimeStatus::InvalidArgument;
        out = input_service_->GamepadButtonPressed(player_slot, button);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::InputGamepadButtonReleased(int player_slot, int button,
        bool& out) const
    {
        out = false;
        if (input_service_ == nullptr) return RuntimeStatus::ServiceUnavailable;
        if (player_slot < 0 || button == 0) return RuntimeStatus::InvalidArgument;
        out = input_service_->GamepadButtonReleased(player_slot, button);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::InputGamepadAxis(int player_slot, int axis,
        float& out) const
    {
        out = 0.0f;
        if (input_service_ == nullptr) return RuntimeStatus::ServiceUnavailable;
        if (player_slot < 0 || axis < 0 || axis > 5) return RuntimeStatus::InvalidArgument;
        out = input_service_->GamepadAxisValue(player_slot, axis);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::InputSetGamepadVibration(int player_slot,
        float low, float high)
    {
        if (input_service_ == nullptr) return RuntimeStatus::ServiceUnavailable;
        if (player_slot < 0) return RuntimeStatus::InvalidArgument;
        if (!std::isfinite(low) || !std::isfinite(high))
            return RuntimeStatus::InvalidArgument;
        // 振動は const な問い合わせではないので、接続された実体側へ書き込む。
        Scene::IInputService* service =
            const_cast<Scene::IInputService*>(input_service_);
        return service->SetGamepadVibration(player_slot, low, high)
            ? RuntimeStatus::Ok : RuntimeStatus::UnsupportedOperation;
    }
}
