#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <DirectXMath.h>

namespace ReplayEngine::Motion
{
    // 骨のアニメーション。既存の MotionTrack（プロパティへキーを打つ AE 型）とは
    // 別に持つ。理由は RIG_DESIGN.txt を参照。要点だけ:
    //   回転はクォータニオン。オイラー角の float 3 本へ割ると補間が破綻する。
    enum class RigInterpolation : int
    {
        Step = 0,
        Linear = 1,
        Smooth = 2,
    };

    struct RigTransform
    {
        DirectX::XMFLOAT3 scale{ 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
        DirectX::XMFLOAT3 translation{ 0.0f, 0.0f, 0.0f };
    };

    struct RigKey
    {
        float time = 0.0f;
        RigTransform transform;
    };

    struct RigTrack
    {
        // 骨の階層のパス。名前だけだと同名の骨で衝突する。
        std::string bone_path;
        RigInterpolation interpolation = RigInterpolation::Linear;
        std::vector<RigKey> keys;
    };

    struct RigClip
    {
        int version = 1;
        std::string name{ "NewClip" };
        // どのモデル向けか。GLB へ焼き戻すときに要る。
        std::string model_path;
        // 骨構成が変わったクリップを弾くための指紋。
        std::string skeleton_hash;
        float duration = 1.0f;
        float frame_rate = 30.0f;
        bool loop = true;
        std::vector<RigTrack> tracks;

        const RigTrack* FindTrack(const std::string& path) const noexcept
        {
            for (const RigTrack& track : tracks)
                if (track.bone_path == path) return &track;
            return nullptr;
        }

        static constexpr const char* file_extension = ".replayrig";
        static constexpr int current_version = 1;

        // MotionAsset と同じ行ベースのテキスト。差分が読めて Git にも載る。
        static bool SaveToFile(const std::filesystem::path& path, const RigClip& clip,
            std::string& error);
        static bool LoadFromFile(const std::filesystem::path& path, RigClip& clip,
            std::string& error);
    };
}
