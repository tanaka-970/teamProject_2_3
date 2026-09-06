using System;
using System.Collections.Generic;
using ReplayEngine;
using N2 = System.Numerics.Vector2;
using N3 = System.Numerics.Vector3;

namespace Game.GrowRush;

[ReplayGuid("beea44001a1543a2bfe8807732410002")]
public sealed class GrowArena : GrowScreen
{
    private sealed class Visual
    {
        public TransformAccess Transform;
        public PrimitiveMeshRendererComponent Renderer;
        public bool Visible;
        public void Show(bool visible) { if (Visible != visible) { Renderer.Visible = visible; Visible = visible; } }
    }
    private readonly Dictionary<string, Visual> visuals = new();
    private readonly int[] revisions = new int[GrowMatch.CellCount];
    private readonly float[] splashAges = new float[16];
    private GrowMatch match = null!;
    private GrowCpu enemy = new();
    private GrowCpu autoPlayer = new(4);
    private TransformAccess camera;
    private float yaw = .42f, pitch = .20f, accumulator, hudTimer, hitTimer, soundCooldown, resultDelay;
    private int nextSplash, lastSecond = -1;
    private bool paused, ending;
    private N3 aim;
    private static readonly Color Soil = new(.30f, .38f, .29f, 1);
    private static Vector3 E(N3 v) => new(v.X, v.Y, v.Z);

    public override void Start()
    {
        BeginScreen("ARENA " + GrowSession.Seconds);
        match = new GrowMatch(GrowSession.Seconds);
        match.Impact = OnImpact; match.Knockout = OnKnockout;
        Array.Fill(revisions, -1); Array.Fill(splashAges, 10);
        camera = Runtime.Transform(Find("Camera"));
        Show("Pause", false); Show("Loading", false);
        Text("ControlHint", GrowPointer.Standalone ? "WASD 移動   マウス 照準   SPACE 水   ESC 停止" : "WASD 移動   右ドラッグ 照準   SPACE 水   ESC 停止");
        for (int i = 0; i < GrowMatch.CellCount; i++) { V("Plot" + i); V("Stem" + i); V("Leaf" + i); }
        for (int i = 0; i < GrowMatch.DropCount; i++) V("Drop" + i).Show(false);
        for (int i = 0; i < splashAges.Length; i++) V("Splash" + i).Show(false);
        RefreshWorld(0); UpdateCamera(0); UpdateHud();
    }

    public override void Update(float deltaTime)
    {
        if (match == null || Loading) return;
        float dt = Math.Clamp(deltaTime, 0, .1f);
        ScreenTime += dt; soundCooldown -= dt;
        PollButtons(Click, "Resume", "BackTitle");
        if (Input.GetKeyDown(Key.Escape) && !ending) SetPaused(!paused);
        if (GrowPointer.Standalone && !GrowPointer.HasFocus && !GrowDiagnostics.Auto && GrowDiagnostics.Capture.Length == 0 && !paused)
            SetPaused(true);
        if (paused) { GrowPointer.Release(); return; }
        UpdateCamera(dt);
        var forward = new N2(MathF.Sin(yaw), MathF.Cos(yaw));
        var right = new N2(forward.Y, -forward.X);
        float x = (Input.GetKey(Key.D) ? 1 : 0) - (Input.GetKey(Key.A) ? 1 : 0);
        float z = (Input.GetKey(Key.W) ? 1 : 0) - (Input.GetKey(Key.S) ? 1 : 0);
        var command = new GrowCommand(right * x + forward * z, aim, Input.GetKey(Key.Space));
        bool automated = GrowDiagnostics.Auto || GrowDiagnostics.Capture == "arena" || GrowDiagnostics.Capture == "result";
        float rate = automated ? 12 : 1;
        bool freezeCapture = GrowDiagnostics.Capture == "arena" && match.Elapsed > 18;
        if (!freezeCapture) accumulator += dt * rate;
        const float step = 1f / 60;
        for (int n = 0; accumulator >= step && n < 120; n++)
        {
            accumulator -= step;
            match.Tick(step, automated ? autoPlayer.Command(match, step, 1) : command, enemy.Command(match, step));
        }
        if (automated && match.Fighters[0].Alive)
        {
            var d = match.Fighters[0].Aim - match.Fighters[0].Position;
            if (d.LengthSquared() > .01f) yaw = MathF.Atan2(d.X, d.Z);
        }
        RefreshWorld(dt); UpdateCamera(0);
        hudTimer -= dt;
        if (hudTimer <= 0) { UpdateHud(); hudTimer = .1f; }
        if (match.Phase == MatchPhase.Finished)
        {
            if (!ending)
            {
                ending = true; GrowPointer.Release(); Sound("finish", .6f);
                GrowSession.You = match.FinalYou; GrowSession.Cpu = match.FinalCpu; GrowSession.HasResult = true;
                Show("CenterMessage", true); Text("CenterMessage", "TIME UP");
                GrowDiagnostics.Record($"FINISH {match.Duration} deaths={match.Fighters[0].Deaths}/{match.Fighters[1].Deaths}");
            }
            resultDelay += dt;
            if (resultDelay >= 1.25f) TriggerFlow("MatchFinished");
        }
    }

    private void SetPaused(bool value)
    {
        paused = value; Show("Pause", value); Show("Crosshair", !value);
        GrowPointer.Release();
    }
    private void Click(string name)
    {
        if (!paused) return;
        if (name == "Resume") SetPaused(false);
        else TriggerFlow("BackToTitle");
    }

    private void UpdateCamera(float dt)
    {
        bool active = match.Phase != MatchPhase.Finished && !paused && match.Fighters[0].Alive;
        Vector2 delta = default;
        if (GrowPointer.Standalone && !GrowDiagnostics.Auto && GrowDiagnostics.Capture.Length == 0 && dt > 0)
            delta = GrowPointer.Delta(active);
        else if (!GrowPointer.Standalone && active && Input.GetMouseButton(MouseButton.Right) && dt > 0)
        {
            var dx = Runtime.PointerDeltaX(); var dy = Runtime.PointerDeltaY();
            delta = new(dx.Succeeded ? dx.Value : 0, dy.Succeeded ? dy.Value : 0);
        }
        yaw += delta.X * .003f; pitch = Math.Clamp(pitch + delta.Y * .0027f, -.12f, .75f);
        var flat = new N3(MathF.Sin(yaw), 0, MathF.Cos(yaw));
        var right = new N3(flat.Z, 0, -flat.X);
        var f = new N3(flat.X * MathF.Cos(pitch), -MathF.Sin(pitch), flat.Z * MathF.Cos(pitch));
        var eye = match.Fighters[0].Position + N3.UnitY * 2.65f - flat * 4.8f + right * .8f;
        camera.Position = E(eye); camera.LookAt(E(eye + f * 20));
        float distance = f.Y < -.02f ? Math.Clamp((eye.Y - .1f) / -f.Y, 2, 25) : 20;
        aim = eye + f * distance;
        // The aim point is taken along the camera ray, then fired from the capsule's nozzle.
        var cpu = match.Fighters[1];
        var toCpu = cpu.Position + N3.UnitY - eye;
        float projected = N3.Dot(toCpu, f);
        if (cpu.Alive && projected > 0 && projected < distance && (toCpu - f * projected).LengthSquared() < .65f * .65f)
            aim = cpu.Position + N3.UnitY;
        var muzzle = match.Fighters[0].Position + N3.UnitY * 1.05f;
        var diff = aim - muzzle; float range = new N2(diff.X, diff.Z).Length();
        if (range > 13) aim = muzzle + diff * (13 / range);
        var marker = V("AimMarker");
        marker.Show(active && aim.Y < .7f);
        marker.Transform.Position = new(aim.X, .14f, aim.Z);
        marker.Transform.LocalScale = new(.8f, .025f, .8f);
    }

    private Visual V(string name)
    {
        if (visuals.TryGetValue(name, out var found)) return found;
        var h = Find(name);
        var renderer = Runtime.GetComponent<PrimitiveMeshRendererComponent>(h);
        if (!renderer.Succeeded) throw new InvalidOperationException("GrowRush renderer missing: " + name);
        var v = new Visual { Transform = Runtime.Transform(h), Renderer = renderer.Value, Visible = renderer.Value.Visible };
        visuals[name] = v; return v;
    }

    private void RefreshWorld(float dt)
    {
        for (int i = 0; i < GrowMatch.CellCount; i++)
        {
            var cell = match.Cells[i];
            if (revisions[i] == cell.Revision) continue;
            revisions[i] = cell.Revision;
            var color = cell.Team == 1 ? Mint : Orange;
            var p = GrowMatch.CellCenter(i);
            V("Plot" + i).Renderer.Tint = cell.Team == 0 ? Soil : color;
            var stem = V("Stem" + i); var leaf = V("Leaf" + i);
            stem.Show(cell.Stage > 0); leaf.Show(cell.Stage > 0);
            if (cell.Stage == 0) continue;
            float height = cell.Stage switch { 1 => .22f, 2 => .48f, 3 => .90f, _ => 2.15f };
            float width = cell.Stage switch { 1 => .20f, 2 => .36f, 3 => .55f, _ => .84f };
            stem.Transform.Position = new(p.X, height * .42f, p.Z);
            stem.Transform.LocalScale = new(cell.Stage == 4 ? .18f : .07f, height * .8f, cell.Stage == 4 ? .18f : .07f);
            stem.Renderer.Tint = cell.Stage == 4 ? new(.32f, .20f, .12f, 1) : color;
            leaf.Transform.Position = new(p.X, height * .88f, p.Z);
            leaf.Transform.LocalScale = new(width, height * .55f, width);
            leaf.Renderer.Tint = color;
        }
        for (int i = 0; i < 2; i++)
        {
            var fighter = match.Fighters[i]; string prefix = i == 0 ? "You" : "Cpu";
            var body = V(prefix + "Body"); var visor = V(prefix + "Visor"); var gun = V(prefix + "Gun");
            bool visible = fighter.Alive && (fighter.Shield <= 0 || (int)(ScreenTime * 12) % 2 == 0);
            body.Show(visible); visor.Show(visible); gun.Show(visible);
            float angle = i == 0 ? yaw : MathF.Atan2(fighter.Aim.X - fighter.Position.X, fighter.Aim.Z - fighter.Position.Z);
            var forward = new N3(MathF.Sin(angle), 0, MathF.Cos(angle));
            body.Transform.Position = E(fighter.Position + N3.UnitY * .95f);
            visor.Transform.Position = E(fighter.Position + N3.UnitY * 1.35f + forward * .40f);
            visor.Transform.LocalRotationEuler = new(0, angle, 0);
            gun.Transform.Position = E(fighter.Position + N3.UnitY * 1.05f + forward * .7f);
            gun.Transform.LocalRotationEuler = new(MathF.PI / 2, angle, 0);
            for (int k = 0; k < 6; k++)
            {
                var pip = V(prefix + "Hp" + k); pip.Show(fighter.Alive);
                pip.Transform.Position = E(fighter.Position + new N3((k - 2.5f) * .16f, 2.05f, 0));
                pip.Renderer.Tint = k < fighter.Hp ? (i == 0 ? Mint : Orange) : Ink;
            }
        }
        for (int i = 0; i < match.Drops.Length; i++)
        {
            var drop = match.Drops[i]; var v = V("Drop" + i);
            if (drop.Active)
            {
                if (!v.Visible) v.Renderer.Tint = drop.Team == 1 ? new(.48f, 1, .91f, 1) : new(1, .76f, .36f, 1);
                v.Transform.Position = E(drop.Position);
            }
            v.Show(drop.Active);
        }
        for (int i = 0; i < splashAges.Length; i++)
        {
            splashAges[i] += dt; var splash = V("Splash" + i);
            splash.Show(splashAges[i] < .32f);
            if (splashAges[i] < .32f) { float radius = .2f + splashAges[i] * 5; splash.Transform.LocalScale = new(radius, .015f, radius); }
        }
        hitTimer -= dt; Show("HitMark", hitTimer > 0);
    }

    private void OnImpact(GrowImpact impact)
    {
        int slot = nextSplash++ % splashAges.Length; splashAges[slot] = 0;
        var v = V("Splash" + slot); v.Transform.Position = E(impact.Position + N3.UnitY * .025f);
        v.Renderer.Tint = impact.Team == 1 ? Mint : Orange;
        if (impact.Fighter && impact.Team == 1) hitTimer = .16f;
        if (soundCooldown <= 0 && !GrowDiagnostics.Auto)
        {
            Sound(impact.Fighter ? "hit" : "water", impact.Team == 1 ? .16f : .08f);
            soundCooldown = .16f;
        }
    }
    private void OnKnockout(int team)
    {
        Sound("tree", .5f);
        GrowDiagnostics.Record("KNOCKOUT team=" + team);
    }
    private void UpdateHud()
    {
        int seconds = (int)MathF.Ceiling(match.Remaining);
        Text("Timer", $"{seconds / 60}:{seconds % 60:00}");
        TextColor("Timer", seconds <= 10 ? Orange : Cream);
        if (seconds != lastSecond && seconds <= 10 && seconds > 0) Sound("tick", .25f);
        lastSecond = seconds;
        var you = match.Score(1); var cpu = match.Score(2);
        Text("YouScore", you.Total.ToString()); Text("CpuScore", cpu.Total.ToString());
        Text("YouAreaHud", $"{100f * you.Area / GrowMatch.CellCount:0}%");
        Text("CpuAreaHud", $"{100f * cpu.Area / GrowMatch.CellCount:0}%");
        Fill("YouTerritory", you.Area / (float)GrowMatch.CellCount); Fill("CpuTerritory", cpu.Area / (float)GrowMatch.CellCount);
        for (int k = 0; k < 6; k++)
            Runtime.SetUIImageColor(Find("HpPip" + k), k < match.Fighters[0].Hp ? Mint : new(.16f, .24f, .25f, 1));
        bool center = match.Phase == MatchPhase.Countdown || !match.Fighters[0].Alive || ending;
        Show("CenterMessage", center);
        if (!ending)
            Text("CenterMessage", match.Phase == MatchPhase.Countdown ? Math.Max(1, (int)MathF.Ceiling(match.Countdown)).ToString()
                : !match.Fighters[0].Alive ? $"復活  {MathF.Ceiling(match.Fighters[0].Respawn):0}" : "");
    }
}
