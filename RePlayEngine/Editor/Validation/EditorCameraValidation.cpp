#include "EditorCameraValidation.h"

#include "../Viewport/EditorCameraController.h"
#include "../Viewport/EditorCameraPreset.h"
#include "../Viewport/EditorViewportCamera.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace ReplayEngine::Editor::Validation
{
    namespace
    {
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

            int Report() const
            {
                if (first_failure_ == 0)
                {
                    std::fprintf(stderr, "EditorCameraValidation OK: %d checks passed\n", total_);
                    return 0;
                }
                std::fprintf(stderr,
                    "EditorCameraValidation FAILED: %d/%d checks failed (first=%d)\n",
                    failures_, total_, first_failure_);
                return first_failure_;
            }

        private:
            int next_code_ = 0;
            int first_failure_ = 0;
            int total_ = 0;
            int failures_ = 0;
        };

        bool Finite3(const DirectX::XMFLOAT3& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) &&
                std::isfinite(value.z);
        }

        bool CameraStateFinite(const EditorViewportCamera& camera) noexcept
        {
            return Finite3(camera.Position()) && Finite3(camera.OrbitPivot()) &&
                std::isfinite(camera.Yaw()) && std::isfinite(camera.Pitch()) &&
                std::isfinite(camera.Roll()) && std::isfinite(camera.OrbitDistance());
        }

        EditorCameraInput OpenGateInput() noexcept
        {
            EditorCameraInput input;
            input.window_focused = true;
            input.viewport_hovered = true;
            input.viewport_focused = true;
            input.delta_time = 1.0f / 60.0f;
            return input;
        }

        void SetGesture(EditorCameraInput& input,
            const EditorCameraMouseGesture& gesture, bool down) noexcept
        {
            input.alt_down = gesture.alt;
            input.shift_down = gesture.shift;
            input.control_down = gesture.control;
            switch (gesture.button)
            {
            case EditorCameraMouseButton::Left: input.left_mouse_down = down; break;
            case EditorCameraMouseButton::Middle: input.middle_mouse_down = down; break;
            case EditorCameraMouseButton::Right: input.right_mouse_down = down; break;
            case EditorCameraMouseButton::None:
            default: break;
            }
        }

        struct GateClosureResult
        {
            bool finite_after_close = false;
            bool kept_current_mode = false;
            bool released_and_restarted = false;
        };

        GateClosureResult ExerciseGateClosure(const EditorCameraMouseGesture& gesture,
            EditorCameraController::Mode expected_mode)
        {
            EditorViewportCamera camera;
            EditorCameraPreset preset;
            EditorCameraController controller;

            EditorCameraInput begin = OpenGateInput();
            begin.mouse_x = 100.0f;
            begin.mouse_y = 80.0f;
            SetGesture(begin, gesture, true);
            controller.Update(camera, begin, preset);

            EditorCameraInput closed = begin;
            closed.ui_popup_open = true;
            closed.mouse_x += 24.0f;
            closed.mouse_y += 12.0f;
            controller.Update(camera, closed, preset);

            GateClosureResult result;
            result.finite_after_close = CameraStateFinite(camera);
            result.kept_current_mode = controller.CurrentMode() == expected_mode;

            EditorCameraInput released = closed;
            SetGesture(released, gesture, false);
            controller.Update(camera, released, preset);
            const bool released_ok = controller.CurrentMode() == EditorCameraController::Mode::None;

            EditorCameraInput restart = OpenGateInput();
            restart.mouse_x = released.mouse_x;
            restart.mouse_y = released.mouse_y;
            SetGesture(restart, gesture, true);
            controller.Update(camera, restart, preset);
            result.released_and_restarted = released_ok &&
                controller.CurrentMode() == expected_mode && CameraStateFinite(camera);
            return result;
        }

        const EditorCameraPreset* FindPreset(const std::vector<EditorCameraPreset>& presets,
            const std::string& id) noexcept
        {
            for (const EditorCameraPreset& preset : presets)
            {
                if (preset.id == id) return &preset;
            }
            return nullptr;
        }

        class CurrentPathGuard final
        {
        public:
            CurrentPathGuard()
            {
                std::error_code error;
                original_ = std::filesystem::current_path(error);
                valid_ = !error;
            }

            ~CurrentPathGuard()
            {
                Restore();
            }

            bool ChangeTo(const std::filesystem::path& path) noexcept
            {
                if (!valid_) return false;
                std::error_code error;
                std::filesystem::current_path(path, error);
                return !error;
            }

            bool Restore() noexcept
            {
                if (!valid_ || restored_) return valid_;
                std::error_code error;
                std::filesystem::current_path(original_, error);
                if (error) return false;
                restored_ = true;
                return true;
            }

            const std::filesystem::path& Original() const noexcept { return original_; }
            bool Valid() const noexcept { return valid_; }

        private:
            std::filesystem::path original_;
            bool valid_ = false;
            bool restored_ = false;
        };
    }

    int RunEditorCameraValidation()
    {
        Checker check(1900);

        // -----------------------------------------------------------------
        // 1900-1909: CanBeginInteraction の真理値表
        // -----------------------------------------------------------------
        const EditorCameraInput open = OpenGateInput();
        check.Expect(EditorCameraController::CanBeginInteraction(open),
            "関門: blocker が無ければ開く");

        EditorCameraInput input = open;
        input.window_focused = false;
        check.Expect(!EditorCameraController::CanBeginInteraction(input),
            "関門: window_focused=false だけで閉じる");

        input = open;
        input.viewport_hovered = false;
        input.viewport_focused = false;
        check.Expect(!EditorCameraController::CanBeginInteraction(input),
            "関門: viewport_hovered=false かつ viewport_focused=false で閉じる");

        input = open;
        input.ui_wants_mouse = true;
        check.Expect(!EditorCameraController::CanBeginInteraction(input),
            "関門: ui_wants_mouse だけで閉じる");

        input = open;
        input.ui_wants_keyboard = true;
        check.Expect(!EditorCameraController::CanBeginInteraction(input),
            "関門: ui_wants_keyboard だけで閉じる");

        input = open;
        input.ui_text_input_active = true;
        check.Expect(!EditorCameraController::CanBeginInteraction(input),
            "関門: ui_text_input_active だけで閉じる");

        input = open;
        input.ui_popup_open = true;
        check.Expect(!EditorCameraController::CanBeginInteraction(input),
            "関門: ui_popup_open だけで閉じる");

        input = open;
        input.gizmo_dragging = true;
        check.Expect(!EditorCameraController::CanBeginInteraction(input),
            "関門: gizmo_dragging だけで閉じる");

        input = open;
        input.viewport_focused = false;
        check.Expect(EditorCameraController::CanBeginInteraction(input),
            "関門: viewport_hovered=true なら viewport_focused=false でも開く");

        input = open;
        input.viewport_hovered = false;
        check.Expect(EditorCameraController::CanBeginInteraction(input),
            "関門: viewport_focused=true なら viewport_hovered=false でも開く");

        // -----------------------------------------------------------------
        // 1910-1921: 操作開始後に関門が閉じる入力列
        //
        // 現在の実装は CanBeginInteraction を「開始関門」として使う。
        // 操作中は gesture が押されている限り mode を維持し、popup が開いても
        // そのフレームで強制 Cancel はしない。ここではその現状を写し取りつつ、
        // 数値が有限のまま・release で解除・再開可能であることを確認する。
        // -----------------------------------------------------------------
        EditorCameraPreset preset;

        GateClosureResult result = ExerciseGateClosure(
            preset.orbit, EditorCameraController::Mode::Orbit);
        check.Expect(result.finite_after_close,
            "操作中関門: Orbit 中に popup が開いてもカメラ値が有限");
        check.Expect(result.kept_current_mode,
            "操作中関門: Orbit は gesture 保持中なら現在仕様どおり継続する");
        check.Expect(result.released_and_restarted,
            "操作中関門: Orbit は release で解除され、関門再開後に再始動できる");

        result = ExerciseGateClosure(preset.pan, EditorCameraController::Mode::Pan);
        check.Expect(result.finite_after_close,
            "操作中関門: Pan 中に popup が開いてもカメラ値が有限");
        check.Expect(result.kept_current_mode,
            "操作中関門: Pan は gesture 保持中なら現在仕様どおり継続する");
        check.Expect(result.released_and_restarted,
            "操作中関門: Pan は release で解除され、関門再開後に再始動できる");

        result = ExerciseGateClosure(preset.dolly, EditorCameraController::Mode::Dolly);
        check.Expect(result.finite_after_close,
            "操作中関門: Dolly 中に popup が開いてもカメラ値が有限");
        check.Expect(result.kept_current_mode,
            "操作中関門: Dolly は gesture 保持中なら現在仕様どおり継続する");
        check.Expect(result.released_and_restarted,
            "操作中関門: Dolly は release で解除され、関門再開後に再始動できる");

        result = ExerciseGateClosure(preset.look, EditorCameraController::Mode::Fly);
        check.Expect(result.finite_after_close,
            "操作中関門: Fly 中に popup が開いてもカメラ値が有限");
        check.Expect(result.kept_current_mode,
            "操作中関門: Fly は gesture 保持中なら現在仕様どおり継続する");
        check.Expect(result.released_and_restarted,
            "操作中関門: Fly は release で解除され、関門再開後に再始動できる");

        // -----------------------------------------------------------------
        // 1922-1925: Preset の保存・読込
        //
        // Store の保存先は相対パスなので、検証中だけ cwd を
        // Saved/Validation/EditorCameraPresetStore へ移す。
        // これにより Store 本体を迂回せず、実データの Saved/Editor や
        // Editor/CameraPresets へ一切触れない。
        // -----------------------------------------------------------------
        CurrentPathGuard path_guard;
        const std::filesystem::path sandbox = path_guard.Original() /
            "Saved" / "Validation" / "EditorCameraPresetStore";
        std::error_code filesystem_error;
        if (path_guard.Valid())
        {
            std::filesystem::remove_all(sandbox, filesystem_error);
            filesystem_error.clear();
            std::filesystem::create_directories(sandbox, filesystem_error);
        }
        const bool sandbox_ready = path_guard.Valid() && !filesystem_error &&
            path_guard.ChangeTo(sandbox);

        EditorCameraPreset stored;
        stored.id = "editor_camera_validation_personal";
        stored.name = "Original Camera";
        stored.scope = EditorCameraPresetScope::Personal;
        std::string error;

        bool rename_ok = false;
        bool japanese_ok = false;
        bool empty_name_ok = false;
        bool shared_rejected = false;

        if (sandbox_ready)
        {
            const bool initial_saved = EditorCameraPresetStore::Save(stored, error);
            stored.name = "Renamed Camera";
            error.clear();
            const bool renamed_saved = initial_saved && EditorCameraPresetStore::Save(stored, error);
            error.clear();
            std::vector<EditorCameraPreset> loaded = EditorCameraPresetStore::LoadAll(error);
            const EditorCameraPreset* found = FindPreset(loaded, stored.id);
            rename_ok = renamed_saved && found != nullptr && found->name == "Renamed Camera";

            stored.name = u8"日本語カメラ・検証";
            error.clear();
            const bool japanese_saved = EditorCameraPresetStore::Save(stored, error);
            error.clear();
            loaded = EditorCameraPresetStore::LoadAll(error);
            found = FindPreset(loaded, stored.id);
            japanese_ok = japanese_saved && found != nullptr &&
                found->name == u8"日本語カメラ・検証";

            stored.name = u8"空名でも残る名前";
            error.clear();
            const bool before_empty_saved = EditorCameraPresetStore::Save(stored, error);
            stored.name.clear();
            error.clear();
            const bool empty_saved = before_empty_saved &&
                EditorCameraPresetStore::Save(stored, error);
            error.clear();
            loaded = EditorCameraPresetStore::LoadAll(error);
            found = FindPreset(loaded, stored.id);
            empty_name_ok = empty_saved && found != nullptr &&
                found->name == u8"空名でも残る名前";

            EditorCameraPreset shared = stored;
            shared.scope = EditorCameraPresetScope::Shared;
            shared.name = "Shared Rewrite Attempt";
            error.clear();
            shared_rejected = !shared.Editable() &&
                !EditorCameraPresetStore::Save(shared, error);
        }

        check.Expect(sandbox_ready && rename_ok,
            "Preset: 名前変更を Save -> LoadAll して同じ名前が戻る");
        check.Expect(sandbox_ready && japanese_ok,
            "Preset: 日本語名を Save -> LoadAll して壊れない");
        check.Expect(sandbox_ready && empty_name_ok,
            "Preset: 既存 preset を空名で Save しても既存名を失わない");
        check.Expect(sandbox_ready && shared_rejected,
            "Preset: Editable=false の Shared preset は Save を拒否する");

        // cwd を先に戻してから sandbox を消す。実データ側は触らない。
        const bool path_restored = path_guard.Restore();
        if (path_restored)
        {
            filesystem_error.clear();
            std::filesystem::remove_all(sandbox, filesystem_error);
        }

        return check.Report();
    }
}
