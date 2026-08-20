using ReplayEngine;

namespace Game;

public enum PersonaElement
{
    Physical = 0,
    Fire = 1,
    Ice = 2,
}

public enum PersonaAffinity
{
    Weak = 0,
    Normal = 1,
    Resist = 2,
}

public readonly struct PersonaSkillData
{
    public PersonaSkillData(string name, PersonaElement element, int spCost, int power)
    {
        Name = name;
        Element = element;
        SpCost = spCost;
        Power = power;
    }

    public string Name { get; }
    public PersonaElement Element { get; }
    public int SpCost { get; }
    public int Power { get; }
}

public readonly struct PersonaCombatantPreset
{
    public PersonaCombatantPreset(
        string name, bool enemy, int hp, int sp, int attack,
        PersonaAffinity physical, PersonaAffinity fire, PersonaAffinity ice,
        bool knowsAgi, bool knowsBufu)
    {
        Name = name;
        Enemy = enemy;
        Hp = hp;
        Sp = sp;
        Attack = attack;
        Physical = physical;
        Fire = fire;
        Ice = ice;
        KnowsAgi = knowsAgi;
        KnowsBufu = knowsBufu;
    }

    public string Name { get; }
    public bool Enemy { get; }
    public int Hp { get; }
    public int Sp { get; }
    public int Attack { get; }
    public PersonaAffinity Physical { get; }
    public PersonaAffinity Fire { get; }
    public PersonaAffinity Ice { get; }
    public bool KnowsAgi { get; }
    public bool KnowsBufu { get; }
}

// 今回の縦切りで使う戦闘データをこの 1 ファイルへ閉じ込める。
// 数値は後でデータアセットへ移すときにここだけ差し替えられるようにしている。
public readonly struct PersonaData
{
    public static readonly PersonaSkillData Attack =
        new("たたかう", PersonaElement.Physical, 0, 5);

    public static readonly PersonaSkillData Agi =
        new("アギ", PersonaElement.Fire, 4, 7);

    public static readonly PersonaSkillData Bufu =
        new("ブフ", PersonaElement.Ice, 4, 7);

    public static readonly PersonaCombatantPreset Player = new(
        "Player", false, 180, 32, 8,
        PersonaAffinity.Normal, PersonaAffinity.Weak, PersonaAffinity.Normal,
        true, true);

    // Enemy 1 は炎弱点。
    public static readonly PersonaCombatantPreset EnemyFireWeak = new(
        "Enemy Fire Weak", true, 95, 12, 3,
        PersonaAffinity.Normal, PersonaAffinity.Weak, PersonaAffinity.Resist,
        true, false);

    // Enemy 2 は氷弱点。
    public static readonly PersonaCombatantPreset EnemyIceWeak = new(
        "Enemy Ice Weak", true, 95, 12, 3,
        PersonaAffinity.Normal, PersonaAffinity.Resist, PersonaAffinity.Weak,
        false, true);

    public static PersonaAffinity AffinityFor(
        PersonaElement element,
        PersonaAffinity physical,
        PersonaAffinity fire,
        PersonaAffinity ice)
    {
        return element switch
        {
            PersonaElement.Fire => fire,
            PersonaElement.Ice => ice,
            _ => physical,
        };
    }

    public static double AffinityMultiplier(PersonaAffinity affinity)
    {
        return affinity switch
        {
            PersonaAffinity.Weak => 1.5,
            PersonaAffinity.Resist => 0.5,
            _ => 1.0,
        };
    }

    public static int CalculateDamage(
        PersonaSkillData skill, int attack, PersonaAffinity affinity, bool guarding)
    {
        var raw = skill.Power * attack * AffinityMultiplier(affinity);
        if (guarding) raw *= 0.5;
        var damage = (int)raw;
        return damage < 1 ? 1 : damage;
    }
}
