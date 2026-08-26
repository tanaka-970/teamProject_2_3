using ReplayEngine;

namespace Game;

public readonly struct PersonaEnemyDecision
{
    public PersonaEnemyDecision(PersonaSkillData skill)
    {
        Skill = skill;
    }

    public PersonaSkillData Skill { get; }
}

// 敵 AI は「弱点を突けるなら突く。SP 不足なら通常攻撃」だけに限定する。
public readonly struct PersonaEnemyBrain
{
    public static PersonaEnemyDecision Decide(
        int currentSp,
        bool knowsAgi,
        bool knowsBufu,
        PersonaAffinity targetPhysical,
        PersonaAffinity targetFire,
        PersonaAffinity targetIce)
    {
        if (knowsAgi &&
            currentSp >= PersonaData.Agi.SpCost &&
            targetFire == PersonaAffinity.Weak)
        {
            return new PersonaEnemyDecision(PersonaData.Agi);
        }

        if (knowsBufu &&
            currentSp >= PersonaData.Bufu.SpCost &&
            targetIce == PersonaAffinity.Weak)
        {
            return new PersonaEnemyDecision(PersonaData.Bufu);
        }

        return new PersonaEnemyDecision(PersonaData.Attack);
    }
}
