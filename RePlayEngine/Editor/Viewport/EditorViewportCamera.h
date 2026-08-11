#pragma once

#include <DirectXMath.h>

namespace ReplayEngine::Editor
{
    // Scene View 専用の編集カメラ。
    //
    // 【Runtime Camera とは完全に別物】
    //   このクラスは次のどれにも触れない。触れられるような参照も持たない。
    //     controlledObjectId / PlayerControlSystem / CameraTargetComponent /
    //     Runtime Camera / PlayerControllerComponent / Scene 内 GameObject の Transform
    //   持っているのは自分の姿勢と設定値だけで、Scene への参照は 1 つも無い。
    //
    // 【GameObject にしない理由】
    //   Scene 内の GameObject にすると、Scene 保存の対象になり、Prefab へ入り込み、
    //   Hierarchy に現れ、controlledObjectId から指せてしまう。
    //   Editor が値として所有する「編集用の視点」であって、ゲームの登場物ではない。
    //
    // 【依存しないもの】
    //   ImGui / D3D11 / windows.h / SceneCollisionWorld / CharacterMotor。
    //   入力は EditorCameraController が読み、ここへは「どれだけ動かすか」だけが届く。
    //   そのためこのクラスは描画も入力も無しに数値だけで検証できる。
    //
    // 【行列規約】
    //   既存エンジンに合わせて左手系・行優先・行ベクトル。
    //   View は XMMatrixLookAtLH、Projection は XMMatrixPerspectiveFovLH で作る。
    class EditorViewportCamera final
    {
    public:
        EditorViewportCamera();

        // ---- 姿勢 ----------------------------------------------------------

        const DirectX::XMFLOAT3& Position() const noexcept { return position_; }
        void SetPosition(const DirectX::XMFLOAT3& value) noexcept { position_ = value; }

        // ラジアン。yaw は連続回転、pitch は上下反転しないよう制限される。
        float Yaw() const noexcept { return yaw_; }
        float Pitch() const noexcept { return pitch_; }
        float Roll() const noexcept { return roll_; }
        void SetYawPitch(float yaw, float pitch) noexcept;
        void SetYawPitchRoll(float yaw, float pitch, float roll) noexcept;

        // 基底ベクトル。SetYawPitch / SetPosition のたびに 1 か所で作り直される。
        const DirectX::XMFLOAT3& Forward() const noexcept { return forward_; }
        const DirectX::XMFLOAT3& Right() const noexcept { return right_; }
        const DirectX::XMFLOAT3& Up() const noexcept { return up_; }

        // ---- Orbit -----------------------------------------------------------

        const DirectX::XMFLOAT3& OrbitPivot() const noexcept { return orbit_pivot_; }
        float OrbitDistance() const noexcept { return orbit_distance_; }

        // Pivot を明示指定する（選択対象の中心など）。
        void SetOrbitPivot(const DirectX::XMFLOAT3& pivot) noexcept;

        // Pivot が未指定のときに使う「カメラ前方の既定距離の点」を Pivot にする。
        void SetOrbitPivotToViewCenter() noexcept;

        // ---- 操作 -------------------------------------------------------------

        // ローカル軸方向の移動量（-1〜1）。実際の速さは move_speed と倍率で決まる。
        struct MoveAxes
        {
            float forward = 0.0f;   // W / S
            float right = 0.0f;     // D / A
            float up = 0.0f;        // E / Q
        };

        // フライ移動。delta_time は呼び出し側で上限を掛けてから渡すこと。
        void Fly(const MoveAxes& axes, float speed_multiplier, float delta_time,
            bool world_vertical_move = false) noexcept;

        // 視点回転。引数はマウスの相対移動量（ピクセル）。
        void Look(float mouse_delta_x, float mouse_delta_y) noexcept;

        // キーボードなどから角度を直接加える。roll も Editor Camera だけで保持する。
        void Rotate(float yaw_delta_radians, float pitch_delta_radians,
            float roll_delta_radians) noexcept;

        // 平行移動。Right / Up 方向へ動かす。
        // 現在の Orbit 距離に比例させるので、遠景でも近景でも操作感が変わらない。
        void Pan(float mouse_delta_x, float mouse_delta_y) noexcept;

        // Orbit Pivot を中心に回る。Pivot との距離は保たれる。
        void Orbit(float mouse_delta_x, float mouse_delta_y) noexcept;

        // ホイールによる前後。距離が 0 以下にならないよう必ず下限で止まる。
        void Zoom(float wheel_delta) noexcept;

        // Maya / Blender 系の mouse-drag dolly。Orbit Pivot との距離を連続変更する。
        void Dolly(float mouse_delta) noexcept;

        // ---- フォーカス --------------------------------------------------------

        // ワールド空間の境界ボックス全体が画面へ収まる位置へ移動し、中心を向く。
        //
        // 極端な大きさへの対策:
        //   巨大な対象で遠くへ飛びすぎないよう最大距離、
        //   極小の対象で内部へ入り near plane で消えないよう最小距離を設ける。
        void FocusOnBounds(const DirectX::XMFLOAT3& minimum,
            const DirectX::XMFLOAT3& maximum) noexcept;

        // Bounds が取れない対象向け。位置だけを見る。
        void FocusOnPoint(const DirectX::XMFLOAT3& point) noexcept;

        // 指定の位置から指定の点を向く。新規 Scene の初期位置に使う。
        void LookAt(const DirectX::XMFLOAT3& eye, const DirectX::XMFLOAT3& target) noexcept;

        // ---- 行列 --------------------------------------------------------------
        //
        // Scene View の描画・Picking・Gizmo・Collider Debug Draw は
        // 必ずこの 2 つを使う。別々に行列を組み立てると表示と当たり位置がずれる。

        DirectX::XMMATRIX ViewMatrix() const noexcept;
        DirectX::XMMATRIX ProjectionMatrix(float aspect) const noexcept;

        // 画面座標（ピクセル）からワールド空間の視線を作る。
        // ViewMatrix / ProjectionMatrix と同じ値から導くので、
        // 見えている位置と拾える位置が必ず一致する。
        struct Ray
        {
            DirectX::XMFLOAT3 origin{ 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 direction{ 0.0f, 0.0f, 1.0f };
        };
        Ray BuildPickingRay(float mouse_x, float mouse_y,
            float viewport_width, float viewport_height) const noexcept;

        // ---- 設定値（Editor の UI から編集する）--------------------------------

        float move_speed = 5.0f;
        float fast_multiplier = 4.0f;
        float slow_multiplier = 0.25f;
        float mouse_sensitivity = 0.15f;
        float pan_sensitivity = 1.0f;
        float zoom_sensitivity = 1.0f;
        float field_of_view_degrees = 60.0f;
        float near_clip = 0.05f;
        float far_clip = 10000.0f;

        // 設定値と姿勢を既定へ戻す。
        void ResetToDefault() noexcept;

        // 設定値だけを既定へ戻す（位置と向きは保つ）。
        void ResetSettingsToDefault() noexcept;

        // ---- 制限値 ------------------------------------------------------------

        // 上下反転を防ぐ pitch の制限（ラジアン）。±89 度。
        static constexpr float pitch_limit = 1.5533431f;

        static constexpr float minimum_orbit_distance = 0.05f;
        static constexpr float maximum_orbit_distance = 100000.0f;

        // フォーカス時の距離の下限・上限。
        static constexpr float minimum_focus_distance = 0.2f;
        static constexpr float maximum_focus_distance = maximum_orbit_distance;

    private:
        // Forward / Right / Up を yaw と pitch から作り直す唯一の場所。
        void RecalculateBasis() noexcept;

        DirectX::XMFLOAT3 position_{ 0.0f, 3.0f, -8.0f };
        float yaw_ = 0.0f;
        float pitch_ = 0.0f;
        float roll_ = 0.0f;

        DirectX::XMFLOAT3 forward_{ 0.0f, 0.0f, 1.0f };
        DirectX::XMFLOAT3 right_{ 1.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 up_{ 0.0f, 1.0f, 0.0f };

        DirectX::XMFLOAT3 orbit_pivot_{ 0.0f, 1.0f, 0.0f };
        float orbit_distance_ = 8.0f;
    };
}
