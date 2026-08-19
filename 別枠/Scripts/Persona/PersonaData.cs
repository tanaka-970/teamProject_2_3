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

public enum PersonaSkillId
{
    Attack = 0,
    Agi = 1,
    Bufu = 2,
}

public enum PersonaBattleState
{
    BattleStart = 0,
    DetermineTurnOrder = 1,
    CommandSelect = 2,
    SkillSelect = 3,
    TargetSelect = 4,
    ExecuteAction = 5,
    OneMoreWait = 6,
    AllOutAttackConfirm = 7,
    AllOutAttack = 8,
    Victory = 9,
    Defeat = 10,
}

public readonly struct PersonaSkillDefinition
{
    public PersonaSkillDefinition(
        PersonaSkillId id,
        string name,
        PersonaElement element,
        int spCost,
        int power)
    {
        Id = id;
        Name = name;
        Element = element;
        SpCost = spCost;
        Power = power;
    }

    public PersonaSkillId Id { get; }
    public string Name { get; }
    public PersonaElement Element { get; }
    public int SpCost { get; }
    public int Power { get; }
}

public readonly struct PersonaCombatantDefinition
{
    public PersonaCombatantDefinition(
        string name,
        int maxHp,
        int maxSp,
        int attackPower,
        PersonaAffinity physicalAffinity,
        PersonaAffinity fireAffinity,
        PersonaAffinity iceAffinity,
        bool hasAgi,
        bool hasBufu)
    {
        Name = name;
        MaxHp = maxHp;
        MaxSp = maxSp;
        AttackPower = attackPower;
        PhysicalAffinity = physicalAffinity;
        FireAffinity = fireAffinity;
        IceAffinity = iceAffinity;
        HasAgi = hasAgi;
        HasBufu = hasBufu;
    }

    public string Name { get; }
    public int MaxHp { get; }
    public int MaxSp { get; }
    public int AttackPower { get; }
    public PersonaAffinity PhysicalAffinity { get; }
    public PersonaAffinity FireAffinity { get; }
    public PersonaAffinity IceAffinity { get; }
    public bool HasAgi { get; }
    public bool HasBufu { get; }
}

[ReplayGuid("4f9bc2e78ad14e8fb33150984b06a721")]
public static class PersonaData
{
    // 仕様 4-3。威力は「小 / 中」を固定整数へ落とし込む。
    public const int AttackPowerSmall = 4;
    public const int SkillPowerMedium = 6;

    // 総攻撃は通常スキルより明確に大きい値にする。
    public const int AllOutAttackPower = 10;

    private static readonly PersonaSkillDefinition AttackSkill = new(
        PersonaSkillId.Attack,
        "たたかう",
        PersonaElement.Physical,
        0,
        AttackPowerSmall);

    private static readonly PersonaSkillDefinition AgiSkill = new(
        PersonaSkillId.Agi,
        "アギ",
        PersonaElement.Fire,
        4,
        SkillPowerMedium);

    private static readonly PersonaSkillDefinition BufuSkill = new(
        PersonaSkillId.Bufu,
        "ブフ",
        PersonaElement.Ice,
        4,
        SkillPowerMedium);

    // 味方は炎弱点。敵 AI の「こちらの弱点を狙う」を確認できるようにする。
    public static readonly PersonaCombatantDefinition Player = new(
        "Player",
        120,
        32,
        8,
        PersonaAffinity.Normal,
        PersonaAffinity.Weak,
        PersonaAffinity.Normal,
        hasAgi: true,
        hasBufu: true);

    // 敵 1 は炎弱点。
    public static readonly PersonaCombatantDefinition EnemyFireWeak = new(
        "Enemy_FireWeak",
        64,
        16,
        6,
        PersonaAffinity.Normal,
        PersonaAffinity.Weak,
        PersonaAffinity.Resist,
        hasAgi: false,
        hasBufu: true);

    // 敵 2 は氷弱点。アギを持つため、味方の炎弱点も狙える。
    public static readonly PersonaCombatantDefinition EnemyIceWeak = new(
        "Enemy_IceWeak",
        64,
        16,
        6,
        PersonaAffinity.Normal,
        PersonaAffinity.Resist,
        PersonaAffinity.Weak,
        hasAgi: true,
        hasBufu: false);

    public static PersonaSkillDefinition GetSkill(PersonaSkillId id)
    {
        return id switch
        {
            PersonaSkillId.Agi => AgiSkill,
            PersonaSkillId.Bufu => BufuSkill,
            _ => AttackSkill,
        };
    }

    public static PersonaAffinity NormalizeAffinity(int value)
    {
        return value switch
        {
            (int)PersonaAffinity.Weak => PersonaAffinity.Weak,
            (int)PersonaAffinity.Resist => PersonaAffinity.Resist,
            _ => PersonaAffinity.Normal,
        };
    }

    public static PersonaAffinity GetAffinity(
        PersonaElement element,
        int physicalAffinity,
        int fireAffinity,
        int iceAffinity)
    {
        return element switch
        {
            PersonaElement.Fire => NormalizeAffinity(fireAffinity),
            PersonaElement.Ice => NormalizeAffinity(iceAffinity),
            _ => NormalizeAffinity(physicalAffinity),
        };
    }

    public static double GetAffinityMultiplier(PersonaAffinity affinity)
    {
        return affinity switch
        {
            PersonaAffinity.Weak => 1.5,
            PersonaAffinity.Resist => 0.5,
            _ => 1.0,
        };
    }

    // 仕様 6。乱数なし。端数切り捨て、最低 1。
    public static int CalculateDamage(
        int power,
        int attackPower,
        PersonaAffinity affinity,
        bool guarding)
    {
        int safePower = System.Math.Max(1, power);
        int safeAttack = System.Math.Max(1, attackPower);

        double raw = safePower * safeAttack * GetAffinityMultiplier(affinity);

        // ぼうぎょは今回の最小縦切りとして被ダメージ半減。
        if (guarding)
        {
            raw *= 0.5;
        }

        return System.Math.Max(1, (int)raw);
    }

    public static bool IsWeak(PersonaAffinity affinity)
    {
        return affinity == PersonaAffinity.Weak;
    }

    public static bool CanUseSkill(
        PersonaSkillId skillId,
        int currentSp,
        bool hasAgi,
        bool hasBufu)
    {
        PersonaSkillDefinition skill = GetSkill(skillId);

        if (skillId == PersonaSkillId.Agi && !hasAgi)
        {
            return false;
        }

        if (skillId == PersonaSkillId.Bufu && !hasBufu)
        {
            return false;
        }

        return currentSp >= skill.SpCost;
    }
}
