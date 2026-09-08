// アタッチ先: なし（補助クラス）。各シーンの Director に付いた GrowScreen 系スクリプトから呼ぶ。
// 主な呼び出し元: GrowRush_Arena.replayscene の Director / GrowArena。
// 担当: 現在のエンジンAPIにないカーソル固定を、C#からWindows APIを呼んで補う。

// 数学・乱数・例外などC#の基本機能を使えるようにする。
using System;
// 診断用名前空間を読み込む。現在このファイルでは参照していない。
using System.Diagnostics;
// Windows APIの呼び出しと構造体のメモリ配置指定を使う。
using System.Runtime.InteropServices;
// エンジンのスクリプト・入力・描画・UI・Runtime APIを使う。
using ReplayEngine;

// このファイルの型をゲーム専用のGame.GrowRush名前空間へまとめる。
namespace Game.GrowRush;

// Windowsのカーソル固定を補う内部クラス。エディターでは固定しない。
internal static class GrowPointer
{
    // Windows APIと同じ並びでX・Y座標を受け渡す構造体。
    [StructLayout(LayoutKind.Sequential)] private struct Point { public int X, Y; }
    // Windows APIと同じ並びで矩形の四辺を受け渡す構造体。
    [StructLayout(LayoutKind.Sequential)] private struct Rect { public int Left, Top, Right, Bottom; }
    // 最前面のウィンドウを取得するWindows APIを宣言する。
    [DllImport("user32.dll")] private static extern IntPtr GetForegroundWindow();
    // ウィンドウを所有するプロセス番号を取得するAPIを宣言する。
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    // ウィンドウの描画領域の大きさを取得するAPIを宣言する。
    [DllImport("user32.dll")] private static extern bool GetClientRect(IntPtr window, out Rect rect);
    // ウィンドウ内の座標を画面全体の座標へ変換するAPIを宣言する。
    [DllImport("user32.dll")] private static extern bool ClientToScreen(IntPtr window, ref Point point);
    // 現在のマウスポインターの画面座標を取得するAPIを宣言する。
    [DllImport("user32.dll")] private static extern bool GetCursorPos(out Point point);
    // マウスポインターを指定した画面座標へ移すAPIを宣言する。
    [DllImport("user32.dll")] private static extern bool SetCursorPos(int x, int y);
    // マウスポインターを指定矩形の内側に制限するAPIを宣言する。
    [DllImport("user32.dll")] private static extern bool ClipCursor(ref Rect rect);
    // 同じClipCursorへ空のポインターを渡して固定解除するための宣言。
    [DllImport("user32.dll", EntryPoint = "ClipCursor")] private static extern bool Unclip(IntPtr rect);
    // Windowsのカーソル表示カウンターを増減するAPIを宣言する。
    [DllImport("user32.dll")] private static extern int ShowCursor(bool show);
    // この補助クラスが現在カーソルを固定しているかを保持する。
    private static bool captured;
    // 非表示APIを呼んだ回数。解除時に同じ回数だけ戻す。
    private static int hideCalls;
    // 起動引数に--gameがあるかで、このランチャーの単体ゲーム起動を判定する。
    public static bool Standalone => Environment.CommandLine.Contains("--game", StringComparison.Ordinal);
    // 最前面のウィンドウが自分のプロセスのものか調べる。
    public static bool HasFocus
    {
        // 最前面ウィンドウのプロセス番号と現在のプロセス番号を比較する。
        get { var window = GetForegroundWindow(); GetWindowThreadProcessId(window, out uint process); return process == (uint)Environment.ProcessId; }
    }
    // マウスを中央へ戻しながら相対移動量を取得する。
    public static Vector2 Delta(bool active)
    {
        // 操作不可・エディター・別アプリ操作中なら固定解除して移動量0を返す。
        if (!active || !Standalone || !HasFocus) { Release(); return default; }
        // 自分が操作中の最前面ウィンドウを取得する。
        var window = GetForegroundWindow();
        // 描画領域を取得できなければ今回の移動量を0にする。
        if (!GetClientRect(window, out var r)) return default;
        // 描画領域の左上を画面全体の座標に変換する。
        var top = new Point(); ClientToScreen(window, ref top);
        // 画面全体の座標で描画領域の中央位置を求める。
        int cx = top.X + (r.Right - r.Left) / 2, cy = top.Y + (r.Bottom - r.Top) / 2;
        // マウスを閉じ込める描画領域を画面全体の矩形として作る。
        var bounds = new Rect { Left = top.X, Top = top.Y, Right = top.X + r.Right, Bottom = top.Y + r.Bottom };
        // まだカーソルを固定していない場合に初回準備を行う。
        if (!captured)
        {
            // 固定中の状態にし、ポインターを領域中央へ移す。
            captured = true; SetCursorPos(cx, cy);
            // カーソルが非表示になるまで表示カウンターを減らし、呼んだ回数を記録する。
            while (ShowCursor(false) >= 0 && hideCalls < 16) hideCalls++;
            // 最後の呼び出し分も数え、領域に固定する。初回はカメラの跳ねを防ぐため移動量0。
            hideCalls++; ClipCursor(ref bounds); return default;
        }
        // ウィンドウの移動やサイズ変更に合わせて固定矩形を更新する。
        ClipCursor(ref bounds);
        // 今のポインター位置を読み取ってから中央へ戻す。
        GetCursorPos(out var pointer); SetCursorPos(cx, cy);
        // 中央から動いた量を各軸-250〜250へ制限して返す。
        return new Vector2(Math.Clamp(pointer.X - cx, -250, 250), Math.Clamp(pointer.Y - cy, -250, 250));
    }
    // このクラスが行ったカーソル固定と非表示を解除する。
    public static void Release()
    {
        // 自分が固定していなければ他の状態に触れない。
        if (!captured) return;
        // Windowsへ空の矩形ポインターを渡し、移動範囲の制限を外す。
        Unclip(IntPtr.Zero);
        // 非表示にした回数と同じ回数だけ表示カウンターを戻す。
        for (int n = 0; n < hideCalls; n++) ShowCursor(true);
        // 呼び出し回数と固定フラグを初期状態へ戻す。
        hideCalls = 0; captured = false;
    }
}
