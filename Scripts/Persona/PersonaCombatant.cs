using ReplayEngine;

namespace Game;

[ReplayGuid("9e1eaad6bf664bcb9adb2ff4ab02cb97")]
public sealed class PersonaCombatant : ScriptBehaviour
{
    // Director から GetScriptField* / SetScriptField* で読む戦闘状態。
    public string DisplayName = "Combatant";
    public bool IsEnemy = false;

    public int MaxHp = 100;
    public int CurrentHp = 100;
    public int MaxSp = 20;
    public int CurrentSp = 20;
    public int AttackPower = 8;

    // PersonaAffinity の整数値。enum 自体は C# Script schema の公開型に無いため int で公開する。
    public int PhysicalAffinity = (int)PersonaAffinity.Normal;
    public int FireAffinity = (int)PersonaAffinity.Normal;
    public int IceAffinity = (int)PersonaAffinity.Normal;

    public bool KnowsAgi = false;
    public bool KnowsBufu = false;

    public bool Down = false;
    public bool Guarding = false;

    public override void Awake()
    {
        if (MaxHp < 1) MaxHp = 1;
        if (MaxSp < 0) MaxSp = 0;
        if (AttackPower < 1) AttackPower = 1;

        if (CurrentHp < 0) CurrentHp = 0;
        if (CurrentHp > MaxHp) CurrentHp = MaxHp;
        if (CurrentSp < 0) CurrentSp = 0;
        if (CurrentSp > MaxSp) CurrentSp = MaxSp;

        PhysicalAffinity = ClampAffinity(PhysicalAffinity);
        FireAffinity = ClampAffinity(FireAffinity);
        IceAffinity = ClampAffinity(IceAffinity);

        if (CurrentHp == 0) Down = false;
    }

    private static int ClampAffinity(int value)
    {
        if (value < (int)PersonaAffinity.Weak) return (int)PersonaAffinity.Weak;
        if (value > (int)PersonaAffinity.Resist) return (int)PersonaAffinity.Resist;
        return value;
    }
}
