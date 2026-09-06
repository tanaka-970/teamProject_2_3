using System;
using System.Numerics;

namespace Game.GrowRush;

// Engine-independent rules. Rendering, input and scene changes live in ScriptBehaviour.
public enum MatchPhase { Countdown, Running, Finished }
public sealed class GrowCell
{
    public int Team;
    public float Growth;
    public int Revision;
    public int Stage => Team == 0 ? 0 : Growth >= 20 ? 4 : Growth >= 10 ? 3 : Growth >= 4 ? 2 : 1;
}
public sealed class GrowFighter
{
    public Vector3 Position;
    public Vector3 Aim;
    public int Hp = 6;
    public float Respawn;
    public float Shield;
    public float FireCooldown;
    public int Deaths;
    public bool Alive => Respawn <= 0;
}
public sealed class GrowDrop
{
    public bool Active;
    public int Team;
    public Vector3 Position;
    public Vector3 Velocity;
    public float Age;
}
public readonly record struct GrowCommand(Vector2 Move, Vector3 Aim, bool Fire);
public readonly record struct GrowScore(int Area, int Growth, int Trees)
{
    public int Total => Area + Growth;
}
public readonly record struct GrowImpact(Vector3 Position, int Team, bool Fighter);

public sealed class GrowMatch
{
    public const int Columns = 20, Rows = 16, CellCount = Columns * Rows, DropCount = 64;
    public const float CellSize = 1.35f, HalfWidth = Columns * CellSize / 2, HalfDepth = Rows * CellSize / 2;
    public const float WaterSpeed = 19, Gravity = 10, SplashRadius = 1.35f;
    public readonly GrowCell[] Cells = new GrowCell[CellCount];
    public readonly GrowFighter[] Fighters = { new(), new() };
    public readonly GrowDrop[] Drops = new GrowDrop[DropCount];
    public MatchPhase Phase { get; private set; } = MatchPhase.Countdown;
    public float Countdown { get; private set; } = 3;
    public float Remaining { get; private set; }
    public int Duration { get; }
    public float Elapsed => Duration - Remaining;
    public GrowScore FinalYou { get; private set; }
    public GrowScore FinalCpu { get; private set; }
    public Action<GrowImpact>? Impact;
    public Action<int>? Knockout;
    private float seedTimer;
    private readonly Random random;

    public GrowMatch(int seconds, int seed = 1451)
    {
        Duration = seconds == 120 ? 120 : 60;
        Remaining = Duration;
        random = new Random(seed);
        for (int i = 0; i < Cells.Length; i++) Cells[i] = new();
        for (int i = 0; i < Drops.Length; i++) Drops[i] = new();
        Fighters[0].Position = Spawn(1);
        Fighters[1].Position = Spawn(2);
    }

    public static Vector3 Spawn(int team) => team == 1 ? new(-6, 0, -6) : new(6, 0, 6);
    public static Vector3 CellCenter(int index) => new((index % Columns + .5f) * CellSize - HalfWidth,
        0, (index / Columns + .5f) * CellSize - HalfDepth);
    public static int CellAt(Vector3 p)
    {
        int x = (int)MathF.Floor((p.X + HalfWidth) / CellSize);
        int z = (int)MathF.Floor((p.Z + HalfDepth) / CellSize);
        return x < 0 || z < 0 || x >= Columns || z >= Rows ? -1 : z * Columns + x;
    }

    public void Tick(float dt, GrowCommand you, GrowCommand cpu)
    {
        if (!float.IsFinite(dt) || dt <= 0 || Phase == MatchPhase.Finished) return;
        // A fixed-step caller preserves fire rate and swept collision at low frame rates.
        if (Phase == MatchPhase.Countdown)
        {
            Countdown = MathF.Max(0, Countdown - dt);
            if (Countdown <= .00001f) Phase = MatchPhase.Running;
            return;
        }
        dt = MathF.Min(dt, Remaining);
        UpdateFighter(0, you, dt);
        UpdateFighter(1, cpu, dt);
        SeparateFighters();
        foreach (var drop in Drops) if (drop.Active) UpdateDrop(drop, dt);
        seedTimer += dt;
        if (seedTimer >= 5) { seedTimer -= 5; ScatterSeeds(); }
        Remaining = MathF.Max(0, Remaining - dt);
        if (Remaining <= .00001f)
        {
            Remaining = 0;
            FinalYou = Score(1); FinalCpu = Score(2);
            Phase = MatchPhase.Finished;
            foreach (var drop in Drops) drop.Active = false;
        }
    }

    private void UpdateFighter(int index, GrowCommand command, float dt)
    {
        var f = Fighters[index];
        if (!f.Alive)
        {
            f.Respawn = MathF.Max(0, f.Respawn - dt);
            if (f.Alive) { f.Position = Spawn(index + 1); f.Hp = 6; f.Shield = 2; f.FireCooldown = .2f; }
            return;
        }
        f.Shield = MathF.Max(0, f.Shield - dt);
        var move = command.Move;
        if (!float.IsFinite(move.X) || !float.IsFinite(move.Y)) move = Vector2.Zero;
        if (move.LengthSquared() > 1) move = Vector2.Normalize(move);
        float speed = index == 0 ? 5.8f : 4.5f;
        f.Position += new Vector3(move.X, 0, move.Y) * (speed * dt);
        f.Position = new(Math.Clamp(f.Position.X, -HalfWidth + .65f, HalfWidth - .65f), 0,
            Math.Clamp(f.Position.Z, -HalfDepth + .65f, HalfDepth - .65f));
        f.Aim = command.Aim;
        f.FireCooldown -= dt;
        if (command.Fire && f.FireCooldown <= 0)
        {
            Fire(index + 1, command.Aim);
            f.FireCooldown = .14f;
        }
    }

    private void SeparateFighters()
    {
        var a = Fighters[0]; var b = Fighters[1];
        if (!a.Alive || !b.Alive) return;
        var d = b.Position - a.Position;
        float length = d.Length();
        if (length >= 1.1f) return;
        var offset = (length > .001f ? d / length : Vector3.UnitX) * ((1.1f - length) * .5f);
        a.Position -= offset; b.Position += offset;
    }

    public static Vector3 WaterVelocity(Vector3 origin, Vector3 target)
    {
        var delta = target - origin;
        float horizontal = new Vector2(delta.X, delta.Z).Length();
        float time = Math.Clamp(horizontal / WaterSpeed, .06f, .75f);
        return new(delta.X / time, Math.Clamp(delta.Y / time + .5f * Gravity * time, -14, 12), delta.Z / time);
    }

    private void Fire(int team, Vector3 target)
    {
        if (!float.IsFinite(target.X) || !float.IsFinite(target.Y) || !float.IsFinite(target.Z)) return;
        GrowDrop? free = null;
        foreach (var d in Drops) if (!d.Active) { free = d; break; }
        if (free == null) return;
        var f = Fighters[team - 1];
        var forward = target - (f.Position + Vector3.UnitY);
        forward.Y = 0;
        if (forward.LengthSquared() < .001f) forward = Vector3.UnitZ;
        forward = Vector3.Normalize(forward);
        free.Position = f.Position + Vector3.UnitY * 1.05f + forward * .7f;
        var delta = target - free.Position;
        float range = new Vector2(delta.X, delta.Z).Length();
        if (range > 13) target = free.Position + delta * (13 / range);
        free.Velocity = WaterVelocity(free.Position, target);
        free.Team = team; free.Age = 0; free.Active = true;
    }

    private void UpdateDrop(GrowDrop drop, float dt)
    {
        var old = drop.Position;
        drop.Position += drop.Velocity * dt - Vector3.UnitY * (.5f * Gravity * dt * dt);
        drop.Velocity.Y -= Gravity * dt; drop.Age += dt;
        var enemy = Fighters[drop.Team == 1 ? 1 : 0];
        // Swept segment against a capsule, including fast drops crossing between ticks.
        if (enemy.Alive && HitsCapsule(old, drop.Position, enemy.Position))
        {
            drop.Active = false;
            if (enemy.Shield <= 0)
            {
                enemy.Hp--;
                if (enemy.Hp <= 0) Defeat(drop.Team == 1 ? 2 : 1);
            }
            Impact?.Invoke(new(drop.Position, drop.Team, true));
            return;
        }
        if (drop.Position.Y <= .10f)
        {
            float t = old.Y > drop.Position.Y ? Math.Clamp((old.Y - .1f) / (old.Y - drop.Position.Y), 0, 1) : 1;
            var hit = Vector3.Lerp(old, drop.Position, t); hit.Y = .10f;
            Splash(hit, drop.Team, 1.6f);
            Impact?.Invoke(new(hit, drop.Team, false)); drop.Active = false;
        }
        else if (drop.Age > 2 || MathF.Abs(drop.Position.X) > HalfWidth + 2 || MathF.Abs(drop.Position.Z) > HalfDepth + 2)
            drop.Active = false;
    }

    private static bool HitsCapsule(Vector3 a, Vector3 b, Vector3 feet)
    {
        // Three overlapping spheres approximate the visible capsule conservatively.
        var segment = b - a;
        float length2 = segment.LengthSquared();
        for (int n = 0; n < 3; n++)
        {
            var center = feet + Vector3.UnitY * (.45f + n * .45f);
            float t = length2 > .00001f ? Math.Clamp(Vector3.Dot(center - a, segment) / length2, 0, 1) : 0;
            if (Vector3.DistanceSquared(a + segment * t, center) <= .52f * .52f) return true;
        }
        return false;
    }

    public void Splash(Vector3 hit, int team, float strength)
    {
        if (Phase != MatchPhase.Running || (team != 1 && team != 2) || !float.IsFinite(strength) || strength <= 0) return;
        for (int i = 0; i < Cells.Length; i++)
        {
            var center = CellCenter(i);
            float distance = new Vector2(hit.X - center.X, hit.Z - center.Z).Length();
            if (distance > SplashRadius) continue;
            WaterCell(i, team, strength * (.55f + .45f * (1 - distance / SplashRadius)));
        }
    }

    public void WaterCell(int index, int team, float amount)
    {
        if (Phase != MatchPhase.Running || index < 0 || index >= CellCount || team < 1 || team > 2 || !float.IsFinite(amount) || amount <= 0) return;
        var c = Cells[index];
        if (c.Team != 0 && c.Team != team)
        {
            c.Growth -= amount;
            if (c.Growth <= 0) { c.Team = 0; c.Growth = 0; }
        }
        else { c.Team = team; c.Growth = MathF.Min(20, c.Growth + amount); }
        c.Revision++;
    }

    public void Defeat(int team)
    {
        if (Phase != MatchPhase.Running || team < 1 || team > 2) return;
        var fighter = Fighters[team - 1];
        if (!fighter.Alive) return;
        int index = CellAt(fighter.Position);
        bool nearTree = false;
        for (int i = 0; i < CellCount; i++)
            if (Cells[i].Stage == 4 && Vector3.DistanceSquared(CellCenter(i), fighter.Position) < 2.7f * 2.7f) nearTree = true;
        if (index >= 0 && !nearTree)
        {
            var cell = Cells[index]; cell.Team = team; cell.Growth = 20; cell.Revision++;
        }
        fighter.Hp = 0; fighter.Deaths++; fighter.Respawn = 2.5f; fighter.FireCooldown = .2f;
        Knockout?.Invoke(team);
    }

    private void ScatterSeeds()
    {
        // Seeds only create sprouts on neutral cells; they cannot mature into more trees.
        for (int i = 0; i < CellCount; i++)
        {
            if (Cells[i].Stage != 4) continue;
            int start = random.Next(24);
            for (int n = 0; n < 24; n++)
            {
                int k = (start + n) % 24;
                int dx = k % 5 - 2, dz = k / 5 - 2;
                int x = i % Columns + dx, z = i / Columns + dz;
                if (x < 0 || z < 0 || x >= Columns || z >= Rows || dx * dx + dz * dz > 8) continue;
                var destination = Cells[z * Columns + x];
                if (destination.Team != 0) continue;
                destination.Team = Cells[i].Team; destination.Growth = 1; destination.Revision++;
                break;
            }
        }
    }

    public GrowScore Score(int team)
    {
        int area = 0, growth = 0, trees = 0;
        foreach (var c in Cells) if (c.Team == team)
        {
            area++;
            growth += c.Stage == 4 ? 4 : c.Stage - 1;
            if (c.Stage == 4) trees++;
        }
        return new(area, growth, trees);
    }
}

public sealed class GrowCpu
{
    private readonly Random random;
    private int goal = -1;
    private float chooseTimer;
    public GrowCpu(int seed = 42) { random = new(seed); }

    public GrowCommand Command(GrowMatch match, float dt, int team = 2)
    {
        var self = match.Fighters[team - 1];
        var other = match.Fighters[team == 1 ? 1 : 0];
        if (match.Phase != MatchPhase.Running || !self.Alive) return default;
        chooseTimer -= dt;
        float distance = Vector3.Distance(self.Position, other.Position);
        if (other.Alive && other.Shield <= 0 && distance < 8.5f && ((int)(match.Elapsed / 3) % 3 != 0))
        {
            var d = other.Position - self.Position;
            var move = new Vector2(d.X, d.Z);
            if (move.LengthSquared() > .001f) move = Vector2.Normalize(move);
            if (distance < 4.2f) move *= -.45f;
            return new(move * .7f, other.Position + Vector3.UnitY, true);
        }
        if (chooseTimer <= 0 || goal < 0 || match.Cells[goal].Stage == 4)
        {
            chooseTimer = 1.5f + (float)random.NextDouble() * 1.3f;
            float best = float.NegativeInfinity;
            for (int k = 0; k < 45; k++)
            {
                int i = random.Next(GrowMatch.CellCount);
                var c = match.Cells[i];
                float dist = Vector3.Distance(self.Position, GrowMatch.CellCenter(i));
                float value = (c.Team == 0 ? 7 : c.Team == team ? 5 : 3) - dist * .5f;
                if (c.Stage == 4) value -= 12;
                if (value > best) { best = value; goal = i; }
            }
        }
        var target = GrowMatch.CellCenter(goal);
        var direction = new Vector2(target.X - self.Position.X, target.Z - self.Position.Z);
        float length = direction.Length();
        var movement = length > 4 ? direction / length : Vector2.Zero;
        return new(movement, target + Vector3.UnitY * .1f, length < 12);
    }
}

public static class GrowSession
{
    public const string TitleScene = "beea44001a1543a2bfe8807732400001";
    public const string ArenaScene = "beea44001a1543a2bfe8807732400002";
    public const string ResultScene = "beea44001a1543a2bfe8807732400003";
    public static int Seconds = 60;
    public static GrowScore You, Cpu;
    public static bool HasResult;
}
