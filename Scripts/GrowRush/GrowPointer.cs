using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using ReplayEngine;

namespace Game.GrowRush;

// Windows-only adapter: engine exposes pointer deltas but not cursor confinement yet.
// Never confines the editor. Standalone owns only its foreground window; loss of focus releases it.
internal static class GrowPointer
{
    [StructLayout(LayoutKind.Sequential)] private struct Point { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] private struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] private static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll")] private static extern bool GetClientRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] private static extern bool ClientToScreen(IntPtr window, ref Point point);
    [DllImport("user32.dll")] private static extern bool GetCursorPos(out Point point);
    [DllImport("user32.dll")] private static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] private static extern bool ClipCursor(ref Rect rect);
    [DllImport("user32.dll", EntryPoint = "ClipCursor")] private static extern bool Unclip(IntPtr rect);
    [DllImport("user32.dll")] private static extern int ShowCursor(bool show);
    private static bool captured;
    private static int hideCalls;
    public static bool Standalone => Environment.CommandLine.Contains("--game", StringComparison.Ordinal);
    public static bool HasFocus
    {
        get { var window = GetForegroundWindow(); GetWindowThreadProcessId(window, out uint process); return process == (uint)Environment.ProcessId; }
    }
    public static Vector2 Delta(bool active)
    {
        if (!active || !Standalone || !HasFocus) { Release(); return default; }
        var window = GetForegroundWindow();
        if (!GetClientRect(window, out var r)) return default;
        var top = new Point(); ClientToScreen(window, ref top);
        int cx = top.X + (r.Right - r.Left) / 2, cy = top.Y + (r.Bottom - r.Top) / 2;
        var bounds = new Rect { Left = top.X, Top = top.Y, Right = top.X + r.Right, Bottom = top.Y + r.Bottom };
        if (!captured)
        {
            captured = true; SetCursorPos(cx, cy);
            while (ShowCursor(false) >= 0 && hideCalls < 16) hideCalls++;
            hideCalls++; ClipCursor(ref bounds); return default;
        }
        ClipCursor(ref bounds);
        GetCursorPos(out var pointer); SetCursorPos(cx, cy);
        return new Vector2(Math.Clamp(pointer.X - cx, -250, 250), Math.Clamp(pointer.Y - cy, -250, 250));
    }
    public static void Release()
    {
        if (!captured) return;
        Unclip(IntPtr.Zero);
        for (int n = 0; n < hideCalls; n++) ShowCursor(true);
        hideCalls = 0; captured = false;
    }
}
