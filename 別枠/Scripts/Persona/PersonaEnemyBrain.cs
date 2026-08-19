using ReplayEngine;

namespace Game;

public readonly struct PersonaEnemyDecision
{
    public PersonaEnemyDecision(PersonaSkillId skillId, bool usesSkill)
    {
        SkillId = skillId;
        UsesSkill = usesSkill;
    }

    public PersonaSkillId SkillId { get; }
    public bool UsesSkill { get; }
}

[ReplayGuid("195adbb4b195470098f8dbf6fa40e9ca")]
public static class PersonaEnemyBrain
{
    // 仕様 7 の優先順位だけを実装する。
    // 1) 弱点を突けるスキル
    // 2) SP 不足なら通常攻撃
    // 3) 対象は生存味方（縦切りでは 1 体なので Director 側で固定）
    public static PersonaEnemyDecision Decide(
        int currentSp,
        bool hasAgi,
        bool hasBufu,
        int targetFireAffinity,
        int targetIceAffinity)
    {
        PersonaAffinity fireAffinity = PersonaData.NormalizeAffinity(targetFireAffinity);
        PersonaAffinity iceAffinity = PersonaData.NormalizeAffinity(targetIceAffinity);

        PersonaSkillDefinition agi = PersonaData.GetSkill(PersonaSkillId.Agi);
        if (hasAgi &&
            fireAffinity == PersonaAffinity.Weak &&
            currentSp >= agi.SpCost)
        {
            return new PersonaEnemyDecision(PersonaSkillId.Agi, usesSkill: true);
        }

        PersonaSkillDefinition bufu = PersonaData.GetSkill(PersonaSkillId.Bufu);
        if (hasBufu &&
            iceAffinity == PersonaAffinity.Weak &&
            currentSp >= bufu.SpCost)
        {
            return new PersonaEnemyDecision(PersonaSkillId.Bufu, usesSkill: true);
        }

        return new PersonaEnemyDecision(PersonaSkillId.Attack, usesSkill: false);
    }
}
