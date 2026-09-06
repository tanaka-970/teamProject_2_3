using Game.GrowRush;
using System.Numerics;

int checks = 0;
void Check(bool ok, string message) { if (!ok) throw new Exception(message); checks++; Console.WriteLine("PASS " + message); }
GrowMatch Running(int seconds = 60) { var m = new GrowMatch(seconds); for (int n = 0; n < 181; n++) m.Tick(1f / 60, default, default); return m; }
var countdown = new GrowMatch(60);
countdown.WaterCell(0, 1, 20);
Check(countdown.Score(1).Total == 0, "Countdown cannot grow or score");
var m = Running();
Check(m.Phase == MatchPhase.Running, "Countdown starts round");
m.WaterCell(12, 1, 1);
Check(m.Score(1) == new GrowScore(1, 0, 0), "Sprout = 1 area + 0 growth");
m.WaterCell(12, 1, 3);
Check(m.Score(1).Total == 2, "Grass = 2 points");
m.WaterCell(12, 1, 6);
Check(m.Score(1).Total == 3, "Bush = 3 points");
m.WaterCell(12, 1, 100);
Check(m.Score(1) == new GrowScore(1, 4, 1) && m.Cells[12].Growth == 20, "Tree capped at 5 points");
m.WaterCell(12, 2, 20);
Check(m.Cells[12].Team == 0 && m.Score(1).Total == 0, "Enemy water removes ownership and points");
m.WaterCell(12, 2, 1);
Check(m.Score(2).Area == 1, "Neutralized ground can be recaptured");
m.WaterCell(-1, 1, 10); m.WaterCell(0, 1, float.NaN); m.WaterCell(0, 4, 1);
Check(m.Cells[0].Team == 0, "Invalid growth rejected");
var seeds = Running(); seeds.WaterCell(150, 1, 20);
for (int n = 0; n < 301; n++) seeds.Tick(1f / 60, default, default);
Check(seeds.Score(1).Area == 2 && seeds.Score(1).Trees == 1, "Tree creates a nearby sprout, not another tree");
var death = Running(); var location = death.Fighters[0].Position; death.Defeat(1);
Check(!death.Fighters[0].Alive && death.Cells[GrowMatch.CellAt(location)].Team == 1, "Death plants victim's tree");
death.Defeat(1);
Check(death.Fighters[0].Deaths == 1, "Repeated defeat while dead ignored");
for (int n = 0; n < 151; n++) death.Tick(1f / 60, default, default);
Check(death.Fighters[0].Alive && death.Fighters[0].Hp == 6 && death.Fighters[0].Shield > 1.8f, "Respawn restores HP with protection");
var movement = Running();
for (int n = 0; n < 1800; n++) movement.Tick(1f / 60, new(new(10, 10), default, false), default);
Check(movement.Fighters[0].Position.X < GrowMatch.HalfWidth && movement.Fighters[0].Position.Z < GrowMatch.HalfDepth, "Movement stays inside arena");
var watering = Running(); var target = watering.Fighters[0].Position + new Vector3(0, .1f, 5);
for (int n = 0; n < 180; n++) watering.Tick(1f / 60, new(Vector2.Zero, target, true), default);
Check(watering.Score(1).Area > 0 && watering.Score(1).Trees > 0, "Flying water lands, grows and matures grass");
var combat = Running(); combat.Fighters[1].Position = combat.Fighters[0].Position + new Vector3(0, 0, 5);
for (int n = 0; n < 120; n++) combat.Tick(1f / 60, new(Vector2.Zero, combat.Fighters[1].Position + Vector3.UnitY, true), default);
Check(combat.Fighters[1].Deaths > 0, "Water projectiles hit and defeat an opponent");
foreach (int seconds in new[] { 60, 120 })
{
    var round = Running(seconds); var a = new GrowCpu(4); var b = new GrowCpu(42);
    int ticks = 0;
    while (round.Phase != MatchPhase.Finished && ticks++ < (seconds + 1) * 60)
        round.Tick(1f / 60, a.Command(round, 1f / 60, 1), b.Command(round, 1f / 60));
    Check(round.Phase == MatchPhase.Finished && round.Remaining == 0, $"{seconds}s match reaches result");
    Check(round.FinalYou.Total > 0 && round.FinalCpu.Total > 0, $"{seconds}s both teams irrigate");
    Check(round.FinalYou.Total == round.FinalYou.Area + round.FinalYou.Growth, "Result score adds correctly");
    var saved = round.FinalYou; round.WaterCell(0, 1, 20); round.Tick(100, default, default);
    Check(round.FinalYou == saved && round.Score(1) == saved, "Result freezes growth, seeds and score");
    Console.WriteLine($"ROUND {seconds}: YOU {round.FinalYou} CPU {round.FinalCpu}");
}
Check(new GrowMatch(120).Score(1).Total == 0 && new GrowMatch(120).Remaining == 120, "Replay starts clean");
Console.WriteLine($"ALL {checks} CHECKS PASSED");
