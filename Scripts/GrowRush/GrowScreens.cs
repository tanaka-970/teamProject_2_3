using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using ReplayEngine;

namespace Game.GrowRush;

public static class GrowDiagnostics
{
    public static bool Auto => Environment.GetEnvironmentVariable("GROWRUSH_TEST") == "1";
    public static string Capture => Environment.GetEnvironmentVariable("GROWRUSH_CAPTURE") ?? "";
    public static int Completed;
    public static void Record(string value)
    {
        if (!Auto && Capture.Length == 0) return;
        Directory.CreateDirectory("Saved/GrowRush");
        File.AppendAllText("Saved/GrowRush/session.log", value + Environment.NewLine);
    }
}

public abstract class GrowScreen : ScriptBehaviour
{
    protected static readonly Color Mint = new(.13f, .87f, .64f, 1);
    protected static readonly Color Orange = new(1, .42f, .17f, 1);
    protected static readonly Color Ink = new(.035f, .09f, .10f, 1);
    protected static readonly Color Cream = new(.97f, .98f, .90f, 1);
    private readonly Dictionary<string, ObjectHandle> objects = new();
    private readonly Dictionary<string, string> textCache = new();
    private EventSubscription clicks;
    protected bool Loading;
    protected float ScreenTime;

    protected void BeginScreen(string name)
    {
        GrowPointer.Release();
        var result = SubscribeEvent(EngineEventIds.ButtonClicked);
        if (result.Succeeded) clicks = result.Value;
        GrowDiagnostics.Record("SCENE " + name);
    }
    protected ObjectHandle Find(string name)
    {
        if (objects.TryGetValue(name, out var h)) return h;
        var found = Runtime.FindGameObject(name);
        if (!found.Succeeded || found.Value.IsEmpty) throw new InvalidOperationException("GrowRush missing object: " + name);
        objects[name] = found.Value;
        return found.Value;
    }
    protected void Text(string name, string value)
    {
        if (textCache.TryGetValue(name, out var previous) && previous == value) return;
        Runtime.SetUIText(Find(name), "<b>" + value + "</b>"); textCache[name] = value;
    }
    protected void Show(string name, bool show) => Runtime.SetEnabled(Find(name), show);
    protected void Fill(string name, float value)
    {
        var image = Runtime.GetComponent<UIImageComponent>(Find(name));
        if (image.Succeeded) { var item = image.Value; item.FillAmount = Math.Clamp(value, 0, 1); }
    }
    protected void TextColor(string name, Color color)
    {
        var result = Runtime.GetComponent<UITextComponent>(Find(name));
        if (result.Succeeded) { var item = result.Value; item.Color = color; }
    }
    protected void Sound(string name, float volume = .4f)
    {
        if (Runtime.AudioAvailable) Runtime.PlayAudio("resources/Game/GrowRush/Audio/" + name + ".wav", volume: volume);
    }
    protected void PollButtons(Action<string> handle, params string[] names)
    {
        if (!clicks.IsValid) return;
        for (int n = 0; n < 16; n++)
        {
            var e = PollEvent(clicks);
            if (!e.Succeeded || string.IsNullOrEmpty(e.Value.TypeGuid)) break;
            if (Loading) continue;
            foreach (string name in names)
                if (e.Value.Source.Object == Find(name).Object) { handle(name); break; }
        }
    }
    protected void TriggerFlow(string eventName)
    {
        if (Loading) return;
        var status = Runtime.TriggerSceneFlow(eventName);
        GrowDiagnostics.Record("FLOW " + eventName + " " + status);
        if (status != RuntimeStatus.Ok) { TransitionFailed(status); return; }
        Loading = true; GrowPointer.Release();
        Show("Loading", true); Text("LoadingText", "…");
        StartCoroutine(WaitForTransition());
    }
    protected void TestClick(string name)
    {
        GrowDiagnostics.Record("BUTTON " + name);
        Runtime.PublishEvent(EngineEventIds.ButtonClicked, source: Find(name));
    }
    private IEnumerator WaitForTransition()
    {
        yield return null;
        while (true)
        {
            var transition = Runtime.SceneTransition;
            if (!transition.Succeeded) { TransitionFailed(transition.Status); yield break; }
            if (!transition.Value.InProgress)
            {
                if (transition.Value.Status != RuntimeStatus.Ok) TransitionFailed(transition.Value.Status);
                yield break;
            }
            yield return null;
        }
    }
    private void TransitionFailed(RuntimeStatus status)
    {
        Loading = false; Show("Loading", true);
        Text("LoadingText", "読み込み失敗 — 再試行できます");
        Runtime.LogError("GrowRush SceneFlow failed: " + status, GameObject);
        GrowDiagnostics.Record("ERROR flow " + status);
    }
    public override void OnDisable() => GrowPointer.Release();
    public override void OnDestroy() => GrowPointer.Release();
    public override void OnApplicationQuit(ApplicationQuitEventInfo info) => GrowPointer.Release();
}

[ReplayGuid("beea44001a1543a2bfe8807732410001")]
public sealed class GrowTitle : GrowScreen
{
    private bool testClicked;
    public override void Start()
    {
        BeginScreen("TITLE");
        var cam = Runtime.Transform(Find("Camera")); cam.LookAt(new Vector3(0, 1, 0));
        Show("Loading", false);
    }
    public override void Update(float dt)
    {
        ScreenTime += dt;
        PollButtons(Click, "OneMinute", "TwoMinutes", "Quit");
        if (Loading || ScreenTime < .3f) return;
        if (Input.GetKeyDown(Key.Alpha1)) StartMatch(60);
        else if (Input.GetKeyDown(Key.Alpha2)) StartMatch(120);
        else if (Input.GetKeyDown(Key.Escape)) Runtime.QuitApplication("GrowRush title");
        if (GrowDiagnostics.Auto && !testClicked && ScreenTime > 1)
        {
            testClicked = true;
            if (GrowDiagnostics.Completed >= 3)
            {
                GrowDiagnostics.Record("PASS title -> 60s -> result -> replay 60s -> title -> 120s -> result -> title -> quit");
                TestClick("Quit");
            }
            else TestClick(GrowDiagnostics.Completed == 2 ? "TwoMinutes" : "OneMinute");
        }
        if ((GrowDiagnostics.Capture == "arena" || GrowDiagnostics.Capture == "result") && ScreenTime > 1)
            StartMatch(60);
    }
    private void Click(string name)
    {
        if (name == "Quit") Runtime.QuitApplication("GrowRush title");
        else StartMatch(name == "TwoMinutes" ? 120 : 60);
    }
    private void StartMatch(int seconds)
    {
        GrowSession.Seconds = seconds; GrowSession.HasResult = false;
        Sound("start"); TriggerFlow("StartMatch");
    }
}

[ReplayGuid("beea44001a1543a2bfe8807732410003")]
public sealed class GrowResult : GrowScreen
{
    private GrowScore you, cpu;
    private bool testClicked;
    public override void Start()
    {
        BeginScreen("RESULT " + GrowSession.Seconds);
        var cam = Runtime.Transform(Find("Camera")); cam.LookAt(new Vector3(0, 1, 0));
        Show("Loading", false);
        you = GrowSession.You; cpu = GrowSession.Cpu;
        Text("ResultTitle", !GrowSession.HasResult ? "結果なし" : you.Total == cpu.Total ? "DRAW" : you.Total > cpu.Total ? "YOU WIN!" : "CPU WINS");
        TextColor("ResultTitle", you.Total >= cpu.Total ? Mint : Orange);
        Text("Duration", GrowSession.Seconds == 120 ? "2分 MATCH" : "1分 MATCH");
        Text("YouArea", you.Area.ToString()); Text("CpuArea", cpu.Area.ToString());
        Text("YouGrowth", "+ " + you.Growth); Text("CpuGrowth", "+ " + cpu.Growth);
        Text("YouTrees", you.Trees.ToString()); Text("CpuTrees", cpu.Trees.ToString());
        Text("RetryLabel", GrowSession.Seconds == 120 ? "もう一度  2分" : "もう一度  1分");
        Text("YouTotal", "0"); Text("CpuTotal", "0");
        Sound(you.Total >= cpu.Total ? "win" : "finish", .6f);
        if (GrowDiagnostics.Auto)
        {
            GrowDiagnostics.Record($"RESULT seconds={GrowSession.Seconds} you={you.Total} cpu={cpu.Total} area={you.Area}/{cpu.Area} growth={you.Growth}/{cpu.Growth}");
            GrowDiagnostics.Completed++;
        }
    }
    public override void Update(float dt)
    {
        ScreenTime += dt;
        float t = Math.Clamp(ScreenTime / 1.1f, 0, 1); t = 1 - (1 - t) * (1 - t);
        Text("YouTotal", ((int)MathF.Round(you.Total * t)).ToString());
        Text("CpuTotal", ((int)MathF.Round(cpu.Total * t)).ToString());
        int max = Math.Max(1, Math.Max(you.Total, cpu.Total));
        Fill("YouBar", you.Total / (float)max * t); Fill("CpuBar", cpu.Total / (float)max * t);
        PollButtons(Click, "Retry", "Title");
        if (!Loading && ScreenTime > .5f)
        {
            if (Input.GetKeyDown(Key.R)) Click("Retry");
            if (Input.GetKeyDown(Key.Escape)) Click("Title");
        }
        if (GrowDiagnostics.Auto && !Loading && !testClicked && ScreenTime > 1.5f)
        {
            testClicked = true;
            TestClick(GrowDiagnostics.Completed == 1 ? "Retry" : "Title");
        }
    }
    private void Click(string name)
    {
        Sound("start", .25f);
        if (name == "Retry") GrowSession.HasResult = false;
        TriggerFlow(name == "Retry" ? "Replay" : "BackToTitle");
    }
}
