using ReplayEngine;

namespace Game;

[ReplayGuid("b416d5a1f6b4437599ecbcf72fe6cab2")]
public sealed class PersonaCombatant : ScriptBehaviour
{
    // 他スクリプトから GetScriptField* / SetScriptField* で触る戦闘状態。
    public string DisplayName = "Combatant";
    public bool IsEnemy = false;

    public int MaxHp = 100;
    public int CurrentHp = 100;
    public int MaxSp = 20;
    public int CurrentSp = 20;
    public int AttackPower = 8;

    // PersonaAffinity の int 値を保存する。
    public int PhysicalAffinity = (int)PersonaAffinity.Normal;
    public int FireAffinity = (int)PersonaAffinity.Normal;
    public int IceAffinity = (int)PersonaAffinity.Normal;

    public bool HasAgi = false;
    public bool HasBufu = false;

    public bool Downed = false;
    public bool Guarding = false;
    public bool Alive = true;

    // 演出・デバッグ表示用の直近結果。
    public int LastDamage = 0;
    public string LastResult = "";

    public override void Awake()
    {
        MaxHp = System.Math.Max(1, MaxHp);
        MaxSp = System.Math.Max(0, MaxSp);
        AttackPower = System.Math.Max(1, AttackPower);

        CurrentHp = System.Math.Clamp(CurrentHp, 0, MaxHp);
        CurrentSp = System.Math.Clamp(CurrentSp, 0, MaxSp);

        PhysicalAffinity = (int)PersonaData.NormalizeAffinity(PhysicalAffinity);
        FireAffinity = (int)PersonaData.NormalizeAffinity(FireAffinity);
        IceAffinity = (int)PersonaData.NormalizeAffinity(IceAffinity);

        Alive = CurrentHp > 0;
        if (!Alive)
        {
            Downed = false;
            Guarding = false;
        }

        LastDamage = 0;
        LastResult = "";
    }
}
