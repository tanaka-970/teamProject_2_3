#include "UISpriteAnimatorComponent.h"

#include "RectTransformComponent.h"
#include "UIImageComponent.h"
#include "../../Motion/MotionMixer.h"
#include "../../Object/GameObject/GameObject.h"

#include <algorithm>
#include <cmath>

namespace ReplayEngine::Components
{
    namespace
    {
        int ClampInt(int value, int low, int high) noexcept
        {
            return (std::max)(low, (std::min)(high, value));
        }
    }

    void UISpriteAnimatorComponent::OnAttach()
    {
        Core::GameObject* owner = Owner();
        if (owner == nullptr) return;
        owner->AddComponent<RectTransformComponent>();
        owner->AddComponent<UIImageComponent>();
    }

    void UISpriteAnimatorComponent::UpdateSprite(float delta_time,
        const Motion::MotionMixer* mixer) noexcept
    {
        Core::GameObject* owner = Owner();
        if (owner == nullptr) return;

        UIImageComponent* image = owner->GetComponent<UIImageComponent>();
        if (image == nullptr) return;

        const int safe_columns = (std::max)(1, columns);
        const int safe_rows = (std::max)(1, rows);
        const int total_frames = safe_columns * safe_rows;
        if (total_frames <= 0) return;

        const int first = ClampInt(start_frame, 0, total_frames - 1);
        const int last = end_frame >= 0
            ? ClampInt(end_frame, first, total_frames - 1)
            : total_frames - 1;

        const bool driven_by_motion =
            mixer != nullptr && mixer->WasDriven(*this, "frame");
        if (playing && !driven_by_motion)
        {
            const float speed = (std::max)(0.0f, frames_per_second);
            if (play_mode == Reverse)
            {
                frame -= speed * delta_time;
                if (frame < static_cast<float>(first))
                {
                    frame = static_cast<float>(last);
                }
            }
            else if (play_mode == PingPong)
            {
                frame += speed * delta_time * static_cast<float>(ping_pong_direction_);
                while (frame > static_cast<float>(last) ||
                    frame < static_cast<float>(first))
                {
                    if (frame > static_cast<float>(last))
                    {
                        frame = static_cast<float>(last) -
                            (frame - static_cast<float>(last));
                        ping_pong_direction_ = -1;
                    }
                    else
                    {
                        frame = static_cast<float>(first) +
                            (static_cast<float>(first) - frame);
                        ping_pong_direction_ = 1;
                    }
                }
            }
            else
            {
                frame += speed * delta_time;
                if (frame > static_cast<float>(last))
                {
                    if (play_mode == Once)
                    {
                        frame = static_cast<float>(last);
                        playing = false;
                    }
                    else
                    {
                        const float span = static_cast<float>(last - first + 1);
                        if (span > 0.0f)
                        {
                            frame = static_cast<float>(first) +
                                std::fmod(frame - static_cast<float>(first), span);
                        }
                        else
                        {
                            frame = static_cast<float>(first);
                        }
                    }
                }
            }
        }

        frame = (std::max)(static_cast<float>(first),
            (std::min)(static_cast<float>(last), frame));
        const int frame_index = ClampInt(static_cast<int>(std::floor(frame + 0.5f)),
            first, last);
        const int column = frame_index % safe_columns;
        const int row = frame_index / safe_columns;

        image->uv_scale = {
            1.0f / static_cast<float>(safe_columns),
            1.0f / static_cast<float>(safe_rows) };
        image->uv_offset = {
            static_cast<float>(column) * image->uv_scale.x,
            static_cast<float>(row) * image->uv_scale.y };
    }
}
