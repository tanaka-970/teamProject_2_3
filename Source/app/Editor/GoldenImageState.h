#pragma once

#include "../framework_class.h"

namespace ReplayEngine::Editor
{
    struct GoldenImageState final
    {
        framework::golden_request_kind golden_request{ framework::golden_request_kind::none };
        std::string golden_name{ "default" };

        // 撮る前に何フレーム「止めた状態」で回すか。
        //
        // TAA の履歴が収束するまで待つ必要がある。
        // 止めずに撮ると毎回違う絵になり、差分が出続けて誰も見なくなる。
        int golden_settle_frames{ 8 };
        int golden_countdown{ 0 };

        // チャンネルごとの許容差。0 なら完全一致。
        int golden_tolerance{ 0 };

        std::string golden_last_summary;
        bool golden_last_ok{ false };

        // self_check の 1 回目を覚えておく場所。
        ReplayEngine::Rendering::Capture::Image golden_self_check_first;
        bool golden_self_check_has_first{ false };
    };
}
