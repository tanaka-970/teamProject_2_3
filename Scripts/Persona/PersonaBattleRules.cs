namespace Game;

public enum PersonaBattleState
{
    BattleStart = 0,
    DetermineTurnOrder = 1,
    CommandSelection = 2,
    SkillSelection = 3,
    TargetSelection = 4,
    Executing = 5,
    OneMoreWait = 6,
    AllOutAttackConfirm = 7,
    AllOutAttack = 8,
    Victory = 9,
    Defeat = 10,
}

public enum PersonaCommandKind
{
    Attack = 0,
    Skill = 1,
    Guard = 2,
}

public readonly struct PersonaHitResult
{
    public PersonaHitResult(
        int damage, int hpAfter, PersonaAffinity affinity,
        bool knockedDown, bool downAfter, bool grantedOneMore, bool defeated)
    {
        Damage = damage;
        HpAfter = hpAfter;
        Affinity = affinity;
        KnockedDown = knockedDown;
        DownAfter = downAfter;
        GrantedOneMore = grantedOneMore;
        Defeated = defeated;
    }

    public int Damage { get; }
    public int HpAfter { get; }
    public PersonaAffinity Affinity { get; }
    public bool KnockedDown { get; }
    public bool DownAfter { get; }
    public bool GrantedOneMore { get; }
    public bool Defeated { get; }
}

// Director が進行判断を二重管理しないため、判定ルールだけをここへ集約する。
public readonly struct PersonaBattleRules
{
    public const int AllOutAttackPower = 24;

    public static PersonaHitResult ResolveHit(
        PersonaSkillData skill,
        int attackerAttack,
        int targetCurrentHp,
        bool targetWasDown,
        bool targetGuarding,
        PersonaAffinity targetPhysical,
        PersonaAffinity targetFire,
        PersonaAffinity targetIce)
    {
        var affinity = PersonaData.AffinityFor(
            skill.Element, targetPhysical, targetFire, targetIce);
        var damage = PersonaData.CalculateDamage(
            skill, attackerAttack, affinity, targetGuarding);

        var hpAfter = targetCurrentHp - damage;
        if (hpAfter < 0) hpAfter = 0;
        var defeated = hpAfter == 0;

        // 既にダウンしている相手へ同じ弱点を当てても 1 More は増えない。
        var knockedDown = !defeated && !targetWasDown && affinity == PersonaAffinity.Weak;
        var downAfter = !defeated && (targetWasDown || knockedDown);
        var oneMore = knockedDown;

        return new PersonaHitResult(
            damage, hpAfter, affinity, knockedDown, downAfter, oneMore, defeated);
    }

    public static bool AllLivingEnemiesDown(
        bool enemy1Alive, bool enemy1Down,
        bool enemy2Alive, bool enemy2Down)
    {
        var anyAlive = enemy1Alive || enemy2Alive;
        if (!anyAlive) return false;
        if (enemy1Alive && !enemy1Down) return false;
        if (enemy2Alive && !enemy2Down) return false;
        return true;
    }

    public static int CalculateAllOutAttackDamage(int attackerAttack)
    {
        var raw = AllOutAttackPower * attackerAttack;
        return raw < 1 ? 1 : raw;
    }
}
