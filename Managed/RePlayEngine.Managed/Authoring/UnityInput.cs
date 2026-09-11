namespace ReplayEngine;

// Unity と同じ名前のキー定数。
//
// 値は既存の Key と同じ（Windows の VK_*）ので、そのまま入れ替えて使える。
// 既存の Key を消していないため、今までの Input.GetKey(Key.Space) も動く。
public enum KeyCode
{
    Backspace = Key.Backspace, Tab = Key.Tab, Return = Key.Enter, Escape = Key.Escape,
    Space = Key.Space, Delete = Key.Delete, Insert = Key.Insert,
    LeftShift = Key.LeftShift, RightShift = Key.RightShift,
    LeftControl = Key.LeftControl, RightControl = Key.RightControl,
    LeftAlt = Key.LeftAlt, RightAlt = Key.RightAlt,
    UpArrow = Key.Up, DownArrow = Key.Down, LeftArrow = Key.Left, RightArrow = Key.Right,
    Home = Key.Home, End = Key.End, PageUp = Key.PageUp, PageDown = Key.PageDown,

    Alpha0 = Key.Alpha0, Alpha1 = Key.Alpha1, Alpha2 = Key.Alpha2, Alpha3 = Key.Alpha3,
    Alpha4 = Key.Alpha4, Alpha5 = Key.Alpha5, Alpha6 = Key.Alpha6, Alpha7 = Key.Alpha7,
    Alpha8 = Key.Alpha8, Alpha9 = Key.Alpha9,

    A = Key.A, B = Key.B, C = Key.C, D = Key.D, E = Key.E, F = Key.F, G = Key.G,
    H = Key.H, I = Key.I, J = Key.J, K = Key.K, L = Key.L, M = Key.M, N = Key.N,
    O = Key.O, P = Key.P, Q = Key.Q, R = Key.R, S = Key.S, T = Key.T, U = Key.U,
    V = Key.V, W = Key.W, X = Key.X, Y = Key.Y, Z = Key.Z,

    Keypad0 = Key.Numpad0, Keypad1 = Key.Numpad1, Keypad2 = Key.Numpad2,
    Keypad3 = Key.Numpad3, Keypad4 = Key.Numpad4, Keypad5 = Key.Numpad5,
    Keypad6 = Key.Numpad6, Keypad7 = Key.Numpad7, Keypad8 = Key.Numpad8,
    Keypad9 = Key.Numpad9,

    F1 = Key.F1, F2 = Key.F2, F3 = Key.F3, F4 = Key.F4, F5 = Key.F5, F6 = Key.F6,
    F7 = Key.F7, F8 = Key.F8, F9 = Key.F9, F10 = Key.F10, F11 = Key.F11, F12 = Key.F12,

    Mouse0 = 0x01, Mouse1 = 0x02, Mouse2 = 0x04,
}

// 既存の Input へ Unity 風の入口だけを足す。
//
// Action / Axis / Input mapping は今までどおり GetAction / GetAxis が正本。
// ここは「キーを直接読みたい」ときの入口を Unity と同じ名前にしただけで、
// 内部の Input システムを作り直してはいない。
public static partial class Input
{
    public static bool GetKey(KeyCode key) => GetKey((Key)key);
    public static bool GetKeyDown(KeyCode key) => GetKeyDown((Key)key);
    public static bool GetKeyUp(KeyCode key) => GetKeyUp((Key)key);

    // Unity と同じ 0=左 / 1=右 / 2=中。
    public static bool GetMouseButton(int button) => GetMouseButton((MouseButton)button);
    public static bool GetMouseButtonDown(int button) => GetMouseButtonDown((MouseButton)button);
    public static bool GetMouseButtonUp(int button) => GetMouseButtonUp((MouseButton)button);

    // Unity の Input.GetAxis と同じ名前。
    // 中身は RePlayEngine の Input Axis Asset なので、
    // 名前は Unity の "Horizontal" ではなくプロジェクトの Axis 名を渡す。
    public static float GetAxis(string axisName)
    {
        var result = NativeBridge.InputAxis(axisName, 0);
        return result.Succeeded ? result.Value : 0.0f;
    }

    public static bool GetButton(string actionName)
    {
        var result = NativeBridge.InputHeld(actionName, 0);
        return result.Succeeded && result.Value;
    }

    public static bool GetButtonDown(string actionName)
    {
        var result = NativeBridge.InputPressed(actionName, 0);
        return result.Succeeded && result.Value;
    }

    public static bool GetButtonUp(string actionName)
    {
        var result = NativeBridge.InputReleased(actionName, 0);
        return result.Succeeded && result.Value;
    }

    public static Vector2 mousePosition => MousePosition;
    public static float mouseScrollDelta => MouseScrollDelta;
}
