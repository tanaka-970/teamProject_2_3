using System;

namespace ReplayEngine;

public readonly struct InputActionId : IEquatable<InputActionId>
{
    public InputActionId(string name) => Name = name ?? string.Empty;
    public string Name { get; }
    public bool IsValid => !string.IsNullOrWhiteSpace(Name);
    public bool Equals(InputActionId other) => string.Equals(Name, other.Name, StringComparison.Ordinal);
    public override bool Equals(object? obj) => obj is InputActionId other && Equals(other);
    public override int GetHashCode() => StringComparer.Ordinal.GetHashCode(Name ?? string.Empty);
    public override string ToString() => Name ?? string.Empty;
}

public readonly struct InputAxisId : IEquatable<InputAxisId>
{
    public InputAxisId(string name) => Name = name ?? string.Empty;
    public string Name { get; }
    public bool IsValid => !string.IsNullOrWhiteSpace(Name);
    public bool Equals(InputAxisId other) => string.Equals(Name, other.Name, StringComparison.Ordinal);
    public override bool Equals(object? obj) => obj is InputAxisId other && Equals(other);
    public override int GetHashCode() => StringComparer.Ordinal.GetHashCode(Name ?? string.Empty);
    public override string ToString() => Name ?? string.Empty;
}

// Engine既定Input Action Assetと同じ名前。独自Assetは同じ型で定数を定義できる。
public static class InputActions
{
    public static readonly InputActionId Jump = new("Jump");
    public static readonly InputActionId Dash = new("Dash");
    public static readonly InputActionId Menu = new("Menu");
    public static readonly InputActionId UISubmit = new("UISubmit");
    public static readonly InputActionId UICancel = new("UICancel");
    public static readonly InputActionId NavigateUp = new("NavigateUp");
    public static readonly InputActionId NavigateDown = new("NavigateDown");
    public static readonly InputActionId NavigateLeft = new("NavigateLeft");
    public static readonly InputActionId NavigateRight = new("NavigateRight");
    public static readonly InputActionId PrimaryClick = new("PrimaryClick");
}

public static class InputAxes
{
    public static readonly InputAxisId MoveX = new("MoveX");
    public static readonly InputAxisId MoveY = new("MoveY");
}

// キーボードの仮想キーコード。Windows の VK_* と同じ値。
public enum Key
{
    Backspace = 0x08, Tab = 0x09, Enter = 0x0D, Shift = 0x10, Control = 0x11,
    Alt = 0x12, Pause = 0x13, CapsLock = 0x14, Escape = 0x1B, Space = 0x20,
    PageUp = 0x21, PageDown = 0x22, End = 0x23, Home = 0x24,
    Left = 0x25, Up = 0x26, Right = 0x27, Down = 0x28,
    Insert = 0x2D, Delete = 0x2E,
    Alpha0 = 0x30, Alpha1 = 0x31, Alpha2 = 0x32, Alpha3 = 0x33, Alpha4 = 0x34,
    Alpha5 = 0x35, Alpha6 = 0x36, Alpha7 = 0x37, Alpha8 = 0x38, Alpha9 = 0x39,
    A = 0x41, B = 0x42, C = 0x43, D = 0x44, E = 0x45, F = 0x46, G = 0x47,
    H = 0x48, I = 0x49, J = 0x4A, K = 0x4B, L = 0x4C, M = 0x4D, N = 0x4E,
    O = 0x4F, P = 0x50, Q = 0x51, R = 0x52, S = 0x53, T = 0x54, U = 0x55,
    V = 0x56, W = 0x57, X = 0x58, Y = 0x59, Z = 0x5A,
    Numpad0 = 0x60, Numpad1 = 0x61, Numpad2 = 0x62, Numpad3 = 0x63, Numpad4 = 0x64,
    Numpad5 = 0x65, Numpad6 = 0x66, Numpad7 = 0x67, Numpad8 = 0x68, Numpad9 = 0x69,
    Multiply = 0x6A, Add = 0x6B, Subtract = 0x6D, Decimal = 0x6E, Divide = 0x6F,
    F1 = 0x70, F2 = 0x71, F3 = 0x72, F4 = 0x73, F5 = 0x74, F6 = 0x75,
    F7 = 0x76, F8 = 0x77, F9 = 0x78, F10 = 0x79, F11 = 0x7A, F12 = 0x7B,
    LeftShift = 0xA0, RightShift = 0xA1,
    LeftControl = 0xA2, RightControl = 0xA3,
    LeftAlt = 0xA4, RightAlt = 0xA5,
}

public enum MouseButton
{
    Left = 0, Right = 1, Middle = 2, X1 = 3, X2 = 4,
}

// XInput のボタンビット。複数を or して渡してもよい。
[Flags]
public enum GamepadButton
{
    None = 0,
    DPadUp = 0x0001, DPadDown = 0x0002, DPadLeft = 0x0004, DPadRight = 0x0008,
    Start = 0x0010, Back = 0x0020,
    LeftThumb = 0x0040, RightThumb = 0x0080,
    LeftShoulder = 0x0100, RightShoulder = 0x0200,
    A = 0x1000, B = 0x2000, X = 0x4000, Y = 0x8000,
}

public enum GamepadAxis
{
    LeftStickX = 0, LeftStickY = 1,
    RightStickX = 2, RightStickY = 3,
    LeftTrigger = 4, RightTrigger = 5,
}

// 生デバイス入力。
//
// Action / Axis で足りるなら ScriptRuntimeContext の InputHeld / InputAxis を使うこと。
// ここはキーコンフィグ画面や一時的な入力のための窓口。
// Editor がキーボード/マウスを掴んでいる間は false / 0 を返す。
public static class Input
{
    public static RuntimeResult<bool> GetAction(InputActionId action, int playerSlot = 0)
        => action.IsValid ? NativeBridge.InputHeld(action.Name, playerSlot)
            : new(RuntimeStatus.InvalidArgument);
    public static RuntimeResult<bool> GetActionDown(InputActionId action, int playerSlot = 0)
        => action.IsValid ? NativeBridge.InputPressed(action.Name, playerSlot)
            : new(RuntimeStatus.InvalidArgument);
    public static RuntimeResult<bool> GetActionUp(InputActionId action, int playerSlot = 0)
        => action.IsValid ? NativeBridge.InputReleased(action.Name, playerSlot)
            : new(RuntimeStatus.InvalidArgument);
    public static RuntimeResult<float> GetAxis(InputAxisId axis, int playerSlot = 0)
        => axis.IsValid ? NativeBridge.InputAxis(axis.Name, playerSlot)
            : new(RuntimeStatus.InvalidArgument);

    public static bool GetKey(Key key) => Flag(NativeBridge.InputKeyHeld((int)key));
    public static bool GetKeyDown(Key key) => Flag(NativeBridge.InputKeyPressed((int)key));
    public static bool GetKeyUp(Key key) => Flag(NativeBridge.InputKeyReleased((int)key));

    public static bool GetMouseButton(MouseButton button)
        => Flag(NativeBridge.InputMouseHeld((int)button));
    public static bool GetMouseButtonDown(MouseButton button)
        => Flag(NativeBridge.InputMousePressed((int)button));
    public static bool GetMouseButtonUp(MouseButton button)
        => Flag(NativeBridge.InputMouseReleased((int)button));

    // 画面座標。ウィンドウを動かしても意味が変わらない。
    public static Vector2 MousePosition
    {
        get
        {
            var result = NativeBridge.InputPointerPosition();
            return result.Succeeded ? result.Value : default;
        }
    }

    // ホイールの回転量。1.0 が 1 ノッチ。
    public static float MouseScrollDelta
    {
        get
        {
            var result = NativeBridge.InputWheelDelta();
            return result.Succeeded ? result.Value : 0.0f;
        }
    }

    public static bool GamepadConnected(int playerSlot = 0)
        => Flag(NativeBridge.InputPadConnected(playerSlot));

    public static bool GetGamepadButton(GamepadButton button, int playerSlot = 0)
        => Flag(NativeBridge.InputPadButtonHeld(playerSlot, (int)button));
    public static bool GetGamepadButtonDown(GamepadButton button, int playerSlot = 0)
        => Flag(NativeBridge.InputPadButtonPressed(playerSlot, (int)button));
    public static bool GetGamepadButtonUp(GamepadButton button, int playerSlot = 0)
        => Flag(NativeBridge.InputPadButtonReleased(playerSlot, (int)button));

    // スティックは -1..1、トリガーは 0..1。デッドゾーン処理済み。
    public static float GetGamepadAxis(GamepadAxis axis, int playerSlot = 0)
    {
        var result = NativeBridge.InputPadAxis(playerSlot, (int)axis);
        return result.Succeeded ? result.Value : 0.0f;
    }

    public static Vector2 GetLeftStick(int playerSlot = 0)
        => new(GetGamepadAxis(GamepadAxis.LeftStickX, playerSlot),
            GetGamepadAxis(GamepadAxis.LeftStickY, playerSlot));

    public static Vector2 GetRightStick(int playerSlot = 0)
        => new(GetGamepadAxis(GamepadAxis.RightStickX, playerSlot),
            GetGamepadAxis(GamepadAxis.RightStickY, playerSlot));

    // 0..1。切るときは両方 0 を渡す。停止は呼び出し側の責任。
    public static RuntimeStatus SetVibration(float low, float high, int playerSlot = 0)
        => NativeBridge.InputSetVibration(playerSlot, low, high);

    public static RuntimeStatus StopVibration(int playerSlot = 0)
        => NativeBridge.InputSetVibration(playerSlot, 0.0f, 0.0f);

    private static bool Flag(RuntimeResult<bool> result)
        => result.Succeeded && result.Value;
}
