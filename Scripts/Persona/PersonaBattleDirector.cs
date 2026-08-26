using ReplayEngine;

namespace Game;

internal struct PersonaCombatantSnapshot
{
    public string DisplayName;
    public bool IsEnemy;
    public int MaxHp;
    public int CurrentHp;
    public int MaxSp;
    public int CurrentSp;
    public int AttackPower;
    public PersonaAffinity PhysicalAffinity;
    public PersonaAffinity FireAffinity;
    public PersonaAffinity IceAffinity;
    public bool KnowsAgi;
    public bool KnowsBufu;
    public bool Down;
    public bool Guarding;

    public bool Alive => CurrentHp > 0;
}

[ReplayGuid("6f6c3e8da1db4fd2bff42917901b6b77")]
public sealed class PersonaBattleDirector : ScriptBehaviour
{
    // MotionPlayerComponent の key。Motion Asset 自体は依頼者が Motion 編集シーンで割り当てる。
    public string AttackMotionName = "Attack";
    public string SkillMotionName = "Skill";
    public string GuardMotionName = "Guard";
    public string DownMotionName = "Down";
    public string RiseMotionName = "Rise";
    public string CommandMenuOpenMotionName = "CommandMenuOpen";
    public string SkillMenuOpenMotionName = "SkillMenuOpen";
    public string TargetSelectMotionName = "TargetSelect";
    public string TargetVignetteMotionName = "TargetVignette";
    public string WeaknessMotionName = "WeaknessHit";
    public string HitShakeMotionName = "HitShake";
    public string OneMoreMotionName = "OneMore";
    public string DamagePopupMotionName = "DamagePopup";
    public string HpBarChangeMotionName = "HpBarChange";
    public string AllOutAttackMotionName = "AllOutAttack";
    public string VictoryMotionName = "Victory";
    public string DefeatMotionName = "Defeat";
    public string ResultScreenMotionName = "ResultScreen";

    // 音素材は同梱されていないため Inspector から実ファイルを指定する。
    public string AttackAudioPath = "resources/Audio/Persona/persona_attack.wav";
    public string WeaknessAudioPath = "resources/Audio/Persona/persona_weak.wav";
    public string DownAudioPath = "resources/Audio/Persona/persona_down.wav";

    // MotionEvent が来ない場合だけ使う進行用フォールバック。
    public float MotionHitFallbackSeconds = 0.65f;
    public float ActionFinishFallbackSeconds = 2.20f;
    public float ResultHoldSeconds = 0.30f;
    public float OneMoreHoldSeconds = 0.40f;

    public int CurrentState = (int)PersonaBattleState.BattleStart;
    public int CurrentTurnIndex = 0;

    private bool initialized;
    private bool presetsApplied;

    private ObjectHandle player;
    private ObjectHandle enemyFireWeak;
    private ObjectHandle enemyIceWeak;
    private ComponentHandle playerCombatant;
    private ComponentHandle enemyFireCombatant;
    private ComponentHandle enemyIceCombatant;

    private ObjectHandle battleEffects;
    private ObjectHandle battleCanvas;
    private ObjectHandle playerStatusText;
    private ObjectHandle playerHpBar;
    private ObjectHandle enemyFireStatusText;
    private ObjectHandle enemyIceStatusText;
    private ObjectHandle battleMessage;
    private ObjectHandle damageText;
    private ObjectHandle oneMoreText;
    private ObjectHandle resultText;

    private ObjectHandle commandMenu;
    private ObjectHandle commandAttack;
    private ObjectHandle commandSkill;
    private ObjectHandle commandGuard;

    private ObjectHandle skillMenu;
    private ObjectHandle skillAgi;
    private ObjectHandle skillBufu;

    private ObjectHandle targetMenu;
    private ObjectHandle targetEnemyFireWeak;
    private ObjectHandle targetEnemyIceWeak;

    private ObjectHandle allOutMenu;
    private ObjectHandle allOutAttack;
    private ObjectHandle allOutCancel;

    private EventSubscription motionEventSubscription;
    private MotionPlayer targetVignetteMotion;
    private bool targetVignetteMotionStarted;

    private PersonaSkillData selectedSkill;
    private PersonaBattleState targetReturnState = PersonaBattleState.CommandSelection;

    private ObjectHandle actionActor;
    private ObjectHandle actionTarget;
    private ComponentHandle actionActorCombatant;
    private ComponentHandle actionTargetCombatant;
    private PersonaSkillData actionSkill;
    private MotionPlayer actionMotion;
    private bool actionMotionStarted;
    private bool actionHitApplied;
    private bool actionResolved;
    private bool actionGrantedOneMore;
    private bool actionAllOut;
    private float actionElapsed;
    private float resultElapsed;
    private float actionFinishTimeout;

    private float stateElapsed;

    public override void Start()
    {
        initialized = ResolveSceneObjects();
        if (!initialized)
        {
            Runtime.LogError("PersonaBattleDirector: 必須オブジェクトの取得に失敗したため戦闘を開始できません", GameObject);
            return;
        }

        var subscription = SubscribeEvent(PersonaEngineIds.MotionEventGuid);
        if (subscription.Status == RuntimeStatus.Ok && subscription.Value.IsValid)
        {
            motionEventSubscription = subscription.Value;
        }
        else
        {
            Runtime.LogWarning("PersonaBattleDirector: MotionEvent を購読できません。攻撃判定は時間フォールバックで進行します", GameObject);
        }

        HideAllMenus();
        Runtime.SetEnabled(damageText, false);
        Runtime.SetEnabled(oneMoreText, false);
        Runtime.SetEnabled(resultText, false);
        SetState(PersonaBattleState.BattleStart);
    }

    public override void Update(float deltaTime)
    {
        if (!initialized) return;

        var safeDelta = deltaTime < 0.0f ? 0.0f : deltaTime;
        stateElapsed += safeDelta;

        switch ((PersonaBattleState)CurrentState)
        {
            case PersonaBattleState.BattleStart:
                UpdateBattleStart();
                break;
            case PersonaBattleState.DetermineTurnOrder:
                UpdateDetermineTurnOrder();
                break;
            case PersonaBattleState.CommandSelection:
                UpdateCommandSelection();
                break;
            case PersonaBattleState.SkillSelection:
                UpdateSkillSelection();
                break;
            case PersonaBattleState.TargetSelection:
                UpdateTargetSelection();
                break;
            case PersonaBattleState.Executing:
                UpdateExecuting(safeDelta);
                break;
            case PersonaBattleState.OneMoreWait:
                UpdateOneMoreWait();
                break;
            case PersonaBattleState.AllOutAttackConfirm:
                UpdateAllOutAttackConfirm();
                break;
            case PersonaBattleState.AllOutAttack:
                UpdateExecuting(safeDelta);
                break;
            case PersonaBattleState.Victory:
            case PersonaBattleState.Defeat:
                UpdateHud();
                break;
        }
    }

    private void UpdateBattleStart()
    {
        if (!presetsApplied)
        {
            if (!ApplyPreset(playerCombatant, PersonaData.Player) ||
                !ApplyPreset(enemyFireCombatant, PersonaData.EnemyFireWeak) ||
                !ApplyPreset(enemyIceCombatant, PersonaData.EnemyIceWeak))
            {
                Runtime.LogError("PersonaBattleDirector: PersonaCombatant の初期データを書き込めません", GameObject);
                initialized = false;
                return;
            }
            presetsApplied = true;
        }

        Runtime.SetUIText(battleMessage, "戦闘開始");
        UpdateHud();
        SetState(PersonaBattleState.DetermineTurnOrder);
    }

    private void UpdateDetermineTurnOrder()
    {
        CurrentTurnIndex = 0;
        BeginCurrentTurn();
    }

    private void BeginCurrentTurn()
    {
        if (CheckBattleEnd()) return;

        if (CurrentTurnIndex == 0)
        {
            BeginPlayerTurn();
            return;
        }

        if (CurrentTurnIndex == 1)
        {
            BeginEnemyTurn(enemyFireWeak, enemyFireCombatant);
            return;
        }

        BeginEnemyTurn(enemyIceWeak, enemyIceCombatant);
    }

    private void BeginPlayerTurn()
    {
        if (!TryReadCombatant(playerCombatant, out var snapshot)) return;
        if (!snapshot.Alive)
        {
            SetState(PersonaBattleState.Defeat);
            ShowResult("DEFEAT", DefeatMotionName);
            return;
        }

        if (snapshot.Guarding)
            Runtime.SetScriptFieldBool(playerCombatant, "Guarding", false);

        if (snapshot.Down)
        {
            Runtime.SetScriptFieldBool(playerCombatant, "Down", false);
            Runtime.SetUIText(battleMessage, $"{snapshot.DisplayName} は立ち上がった");
            PlayMotion(player, RiseMotionName, out _);
            UpdateHud();
            AdvanceTurn();
            return;
        }

        if (AllLivingEnemiesDown())
        {
            OpenAllOutConfirm();
            return;
        }

        OpenCommandSelection();
    }

    private void BeginEnemyTurn(ObjectHandle enemy, ComponentHandle enemyCombatant)
    {
        if (!TryReadCombatant(enemyCombatant, out var enemyState)) return;
        if (!enemyState.Alive)
        {
            AdvanceTurn();
            return;
        }

        if (enemyState.Guarding)
            Runtime.SetScriptFieldBool(enemyCombatant, "Guarding", false);

        if (enemyState.Down)
        {
            Runtime.SetScriptFieldBool(enemyCombatant, "Down", false);
            Runtime.SetUIText(battleMessage, $"{enemyState.DisplayName} は立ち上がった");
            UpdateHud();
            PlayMotion(enemy, RiseMotionName, out _);
            AdvanceTurn();
            return;
        }

        if (!TryReadCombatant(playerCombatant, out var playerState)) return;
        var decision = PersonaEnemyBrain.Decide(
            enemyState.CurrentSp,
            enemyState.KnowsAgi,
            enemyState.KnowsBufu,
            playerState.PhysicalAffinity,
            playerState.FireAffinity,
            playerState.IceAffinity);

        StartSingleTargetAction(enemy, enemyCombatant, player, playerCombatant, decision.Skill);
    }

    private void UpdateCommandSelection()
    {
        UpdateHud();
        if (!Pressed("UISubmit")) return;

        var focus = Runtime.GetUIFocus();
        if (focus.Status != RuntimeStatus.Ok) return;

        if (SameObject(focus.Value, commandAttack))
        {
            selectedSkill = PersonaData.Attack;
            targetReturnState = PersonaBattleState.CommandSelection;
            OpenTargetSelection();
        }
        else if (SameObject(focus.Value, commandSkill))
        {
            OpenSkillSelection();
        }
        else if (SameObject(focus.Value, commandGuard))
        {
            ExecuteGuard();
        }
    }

    private void UpdateSkillSelection()
    {
        UpdateHud();
        if (Pressed("UICancel"))
        {
            OpenCommandSelection();
            return;
        }
        if (!Pressed("UISubmit")) return;

        if (!TryReadCombatant(playerCombatant, out var playerState)) return;
        var focus = Runtime.GetUIFocus();
        if (focus.Status != RuntimeStatus.Ok) return;

        if (SameObject(focus.Value, skillAgi))
        {
            if (playerState.CurrentSp < PersonaData.Agi.SpCost) return;
            selectedSkill = PersonaData.Agi;
        }
        else if (SameObject(focus.Value, skillBufu))
        {
            if (playerState.CurrentSp < PersonaData.Bufu.SpCost) return;
            selectedSkill = PersonaData.Bufu;
        }
        else
        {
            return;
        }

        targetReturnState = PersonaBattleState.SkillSelection;
        OpenTargetSelection();
    }

    private void UpdateTargetSelection()
    {
        UpdateTargetHighlight();
        if (Pressed("UICancel"))
        {
            if (targetReturnState == PersonaBattleState.SkillSelection) OpenSkillSelection();
            else OpenCommandSelection();
            return;
        }
        if (!Pressed("UISubmit")) return;

        var focus = Runtime.GetUIFocus();
        if (focus.Status != RuntimeStatus.Ok) return;

        if (SameObject(focus.Value, targetEnemyFireWeak) && IsAlive(enemyFireCombatant))
        {
            StartSingleTargetAction(player, playerCombatant, enemyFireWeak, enemyFireCombatant, selectedSkill);
        }
        else if (SameObject(focus.Value, targetEnemyIceWeak) && IsAlive(enemyIceCombatant))
        {
            StartSingleTargetAction(player, playerCombatant, enemyIceWeak, enemyIceCombatant, selectedSkill);
        }
    }

    private void UpdateAllOutAttackConfirm()
    {
        if (Pressed("UICancel"))
        {
            OpenCommandSelection();
            return;
        }
        if (!Pressed("UISubmit")) return;

        var focus = Runtime.GetUIFocus();
        if (focus.Status != RuntimeStatus.Ok) return;

        if (SameObject(focus.Value, allOutAttack))
        {
            StartAllOutAttack();
        }
        else if (SameObject(focus.Value, allOutCancel))
        {
            OpenCommandSelection();
        }
    }

    private void UpdateExecuting(float deltaTime)
    {
        actionElapsed += deltaTime;
        PollMotionHitEvents();

        if (!actionHitApplied &&
            (!actionMotionStarted || actionElapsed >= PositiveOr(MotionHitFallbackSeconds, 0.65f)))
        {
            ApplyPendingHit();
        }

        if (!actionHitApplied) return;

        resultElapsed += deltaTime;
        var motionDone = !actionMotionStarted || MotionFinished(actionMotion);
        var finishTimeout = actionElapsed >= actionFinishTimeout;
        if (!motionDone && !finishTimeout) return;
        if (resultElapsed < PositiveOr(ResultHoldSeconds, 0.30f)) return;

        FinishPendingAction();
    }

    private void UpdateOneMoreWait()
    {
        if (stateElapsed < PositiveOr(OneMoreHoldSeconds, 0.40f)) return;
        Runtime.SetEnabled(oneMoreText, false);
        BeginPlayerTurn();
    }

    private void OpenCommandSelection()
    {
        HideAllMenus();
        Runtime.SetEnabled(commandMenu, true);
        Runtime.SetUIText(battleMessage, "コマンドを選択");
        Runtime.SetUIFocus(commandAttack);
        PlayMotion(commandMenu, CommandMenuOpenMotionName, out _);
        SetState(PersonaBattleState.CommandSelection);
        UpdateHud();
    }

    private void OpenSkillSelection()
    {
        HideAllMenus();
        Runtime.SetEnabled(skillMenu, true);

        if (!TryReadCombatant(playerCombatant, out var playerState)) return;
        var agiEnabled = playerState.CurrentSp >= PersonaData.Agi.SpCost;
        var bufuEnabled = playerState.CurrentSp >= PersonaData.Bufu.SpCost;
        Runtime.SetUIButtonInteractable(skillAgi, agiEnabled);
        Runtime.SetUIButtonInteractable(skillBufu, bufuEnabled);
        Runtime.SetUIText(battleMessage, "スキルを選択");

        if (agiEnabled)
        {
            Runtime.SetUIFocus(skillAgi);
        }
        else
        {
            var next = Runtime.FindUIFocus(skillAgi, UIFocusDirection.Down);
            if (next.Status == RuntimeStatus.Ok && bufuEnabled) Runtime.SetUIFocus(next.Value);
            else if (bufuEnabled) Runtime.SetUIFocus(skillBufu);
        }

        PlayMotion(skillMenu, SkillMenuOpenMotionName, out _);
        SetState(PersonaBattleState.SkillSelection);
    }

    private void OpenTargetSelection()
    {
        HideAllMenus();
        Runtime.SetEnabled(targetMenu, true);

        var fireAlive = IsAlive(enemyFireCombatant);
        var iceAlive = IsAlive(enemyIceCombatant);
        Runtime.SetUIButtonInteractable(targetEnemyFireWeak, fireAlive);
        Runtime.SetUIButtonInteractable(targetEnemyIceWeak, iceAlive);

        if (fireAlive)
        {
            Runtime.SetUIFocus(targetEnemyFireWeak);
        }
        else if (iceAlive)
        {
            var next = Runtime.FindUIFocus(targetEnemyFireWeak, UIFocusDirection.Right);
            if (next.Status == RuntimeStatus.Ok) Runtime.SetUIFocus(next.Value);
            else Runtime.SetUIFocus(targetEnemyIceWeak);
        }

        Runtime.SetUIText(battleMessage, $"{selectedSkill.Name} の対象を選択");
        PlayMotion(targetMenu, TargetSelectMotionName, out _);
        targetVignetteMotionStarted = PlayMotion(battleEffects, TargetVignetteMotionName, out targetVignetteMotion);
        SetState(PersonaBattleState.TargetSelection);
        UpdateTargetHighlight();
    }

    private void OpenAllOutConfirm()
    {
        HideAllMenus();
        Runtime.SetEnabled(allOutMenu, true);
        Runtime.SetUIText(battleMessage, "敵は全員ダウン！ 総攻撃しますか？");
        Runtime.SetUIFocus(allOutAttack);
        SetState(PersonaBattleState.AllOutAttackConfirm);
    }

    private void ExecuteGuard()
    {
        Runtime.SetScriptFieldBool(playerCombatant, "Guarding", true);
        Runtime.SetUIText(battleMessage, "Player は防御している");
        PlayMotion(player, GuardMotionName, out _);
        UpdateHud();
        AdvanceTurn();
    }

    private void StartSingleTargetAction(
        ObjectHandle actor,
        ComponentHandle actorCombatant,
        ObjectHandle target,
        ComponentHandle targetCombatant,
        PersonaSkillData skill)
    {
        HideAllMenus();
        actionActor = actor;
        actionActorCombatant = actorCombatant;
        actionTarget = target;
        actionTargetCombatant = targetCombatant;
        actionSkill = skill;
        actionAllOut = false;
        ResetActionRuntime();

        if (!TryReadCombatant(actorCombatant, out var actorState)) return;
        if (skill.SpCost > 0)
        {
            var nextSp = actorState.CurrentSp - skill.SpCost;
            if (nextSp < 0) nextSp = 0;
            Runtime.SetScriptFieldInt(actorCombatant, "CurrentSp", nextSp);
        }

        Runtime.SetUIText(battleMessage, $"{actorState.DisplayName}：{skill.Name}");
        PlayAudioIfAssigned(AttackAudioPath);

        var motionName = skill.Element == PersonaElement.Physical ? AttackMotionName : SkillMotionName;
        actionMotionStarted = PlayMotion(actor, motionName, out actionMotion);
        if (actionMotionStarted) actionFinishTimeout = ResolveMotionFinishTimeout(actionMotion);
        SetState(PersonaBattleState.Executing);
        UpdateHud();
    }

    private void StartAllOutAttack()
    {
        HideAllMenus();
        actionActor = player;
        actionActorCombatant = playerCombatant;
        actionTarget = default;
        actionTargetCombatant = default;
        actionSkill = PersonaData.Attack;
        actionAllOut = true;
        ResetActionRuntime();

        Runtime.SetUIText(battleMessage, "総攻撃！");
        PlayAudioIfAssigned(AttackAudioPath);
        actionMotionStarted = PlayMotion(battleEffects, AllOutAttackMotionName, out actionMotion);
        if (actionMotionStarted) actionFinishTimeout = ResolveMotionFinishTimeout(actionMotion);
        SetState(PersonaBattleState.AllOutAttack);
    }

    private void ResetActionRuntime()
    {
        actionHitApplied = false;
        actionResolved = false;
        actionGrantedOneMore = false;
        actionMotionStarted = false;
        actionMotion = default;
        actionElapsed = 0.0f;
        resultElapsed = 0.0f;
        actionFinishTimeout = PositiveOr(ActionFinishFallbackSeconds, 2.20f);
        Runtime.SetEnabled(damageText, false);
    }

    private void PollMotionHitEvents()
    {
        if (!motionEventSubscription.IsValid || actionHitApplied) return;

        for (var i = 0; i < 16; ++i)
        {
            var polled = PollEvent(motionEventSubscription);
            if (polled.Status != RuntimeStatus.Ok) return;
            if (string.IsNullOrEmpty(polled.Value.TypeGuid)) return;

            if (!polled.Value.TryGetString("name", out var eventName)) continue;
            if (eventName != PersonaEngineIds.MotionHitEventName) continue;

            var expectedSource = actionAllOut ? battleEffects : actionActor;
            if (!SameObject(polled.Value.Source, expectedSource)) continue;

            ApplyPendingHit();
            return;
        }
    }

    private void ApplyPendingHit()
    {
        if (actionHitApplied) return;
        actionHitApplied = true;

        if (actionAllOut)
        {
            ApplyAllOutDamage();
            return;
        }

        if (!TryReadCombatant(actionActorCombatant, out var attacker) ||
            !TryReadCombatant(actionTargetCombatant, out var target))
            return;

        if (!attacker.Alive || !target.Alive)
        {
            actionResolved = true;
            return;
        }

        var hit = PersonaBattleRules.ResolveHit(
            actionSkill,
            attacker.AttackPower,
            target.CurrentHp,
            target.Down,
            target.Guarding,
            target.PhysicalAffinity,
            target.FireAffinity,
            target.IceAffinity);

        Runtime.SetScriptFieldInt(actionTargetCombatant, "CurrentHp", hit.HpAfter);
        Runtime.SetScriptFieldBool(actionTargetCombatant, "Down", hit.DownAfter);
        actionGrantedOneMore = hit.GrantedOneMore;

        ShowDamage(hit.Damage, hit.Affinity, hit.KnockedDown);
        PlayMotion(battleEffects, HitShakeMotionName, out _);
        if (hit.Affinity == PersonaAffinity.Weak)
        {
            PlayAudioIfAssigned(WeaknessAudioPath);
            PlayMotion(battleEffects, WeaknessMotionName, out _);
        }
        if (hit.KnockedDown)
        {
            PlayAudioIfAssigned(DownAudioPath);
            PlayMotion(actionTarget, DownMotionName, out _);
        }

        if (hit.Defeated)
        {
            Runtime.SetUIText(battleMessage, $"{target.DisplayName} を倒した！");
        }
        else if (hit.KnockedDown)
        {
            Runtime.SetUIText(battleMessage, $"WEAK! {target.DisplayName} はダウンした");
        }
        else
        {
            Runtime.SetUIText(battleMessage, $"{target.DisplayName} に {hit.Damage} ダメージ");
        }

        actionResolved = true;
        UpdateHud();
        if (SameObject(actionTarget, player))
            PlayMotion(playerHpBar, HpBarChangeMotionName, out _);
    }

    private void ApplyAllOutDamage()
    {
        if (!TryReadCombatant(playerCombatant, out var attacker)) return;
        var damage = PersonaBattleRules.CalculateAllOutAttackDamage(attacker.AttackPower);
        var totalDamage = 0;

        totalDamage += ApplyAllOutToEnemy(enemyFireCombatant, damage);
        totalDamage += ApplyAllOutToEnemy(enemyIceCombatant, damage);

        Runtime.SetScriptFieldBool(enemyFireCombatant, "Down", false);
        Runtime.SetScriptFieldBool(enemyIceCombatant, "Down", false);
        Runtime.SetUIText(battleMessage, $"総攻撃！ 合計 {totalDamage} ダメージ");
        ShowDamage(totalDamage, PersonaAffinity.Normal, false);
        actionResolved = true;
        UpdateHud();
    }

    private int ApplyAllOutToEnemy(ComponentHandle component, int damage)
    {
        if (!TryReadCombatant(component, out var enemy) || !enemy.Alive) return 0;
        var applied = damage > enemy.CurrentHp ? enemy.CurrentHp : damage;
        var nextHp = enemy.CurrentHp - damage;
        if (nextHp < 0) nextHp = 0;
        Runtime.SetScriptFieldInt(component, "CurrentHp", nextHp);
        return applied;
    }

    private void FinishPendingAction()
    {
        if (!actionResolved) actionResolved = true;
        Runtime.SetEnabled(damageText, false);

        if (CheckBattleEnd()) return;

        if (actionAllOut)
        {
            AdvanceTurn();
            return;
        }

        if (SameObject(actionActor, player) && actionGrantedOneMore)
        {
            Runtime.SetUIText(oneMoreText, "1 MORE!");
            Runtime.SetEnabled(oneMoreText, true);
            PlayMotion(battleEffects, OneMoreMotionName, out _);
            SetState(PersonaBattleState.OneMoreWait);
            return;
        }

        // 敵も弱点を突けば同じキャラが 1 More。既に Down の相手では再取得できない。
        if (!SameObject(actionActor, player) && actionGrantedOneMore)
        {
            BeginEnemyTurn(actionActor, actionActorCombatant);
            return;
        }

        AdvanceTurn();
    }

    private void AdvanceTurn()
    {
        if (CheckBattleEnd()) return;
        CurrentTurnIndex += 1;
        if (CurrentTurnIndex > 2) CurrentTurnIndex = 0;
        BeginCurrentTurn();
    }

    private bool CheckBattleEnd()
    {
        if (!IsAlive(playerCombatant))
        {
            SetState(PersonaBattleState.Defeat);
            ShowResult("DEFEAT", DefeatMotionName);
            return true;
        }

        if (!IsAlive(enemyFireCombatant) && !IsAlive(enemyIceCombatant))
        {
            SetState(PersonaBattleState.Victory);
            ShowResult("VICTORY", VictoryMotionName);
            return true;
        }
        return false;
    }

    private void ShowResult(string text, string motionName)
    {
        HideAllMenus();
        Runtime.SetUIText(resultText, text);
        Runtime.SetEnabled(resultText, true);
        Runtime.SetUIText(battleMessage, text);
        PlayMotion(resultText, motionName, out _);
        PlayMotion(battleEffects, ResultScreenMotionName, out _);
        UpdateHud();
    }

    private bool AllLivingEnemiesDown()
    {
        if (!TryReadCombatant(enemyFireCombatant, out var fire) ||
            !TryReadCombatant(enemyIceCombatant, out var ice))
            return false;

        return PersonaBattleRules.AllLivingEnemiesDown(
            fire.Alive, fire.Down,
            ice.Alive, ice.Down);
    }

    private void ShowDamage(int damage, PersonaAffinity affinity, bool knockedDown)
    {
        var suffix = affinity == PersonaAffinity.Weak ? "  WEAK" :
            affinity == PersonaAffinity.Resist ? "  RESIST" : "";
        if (knockedDown) suffix += "  DOWN";
        Runtime.SetUIText(damageText, damage.ToString() + suffix);
        Runtime.SetEnabled(damageText, true);
        PlayMotion(damageText, DamagePopupMotionName, out _);
    }

    private void UpdateHud()
    {
        if (TryReadCombatant(playerCombatant, out var p))
        {
            var guard = p.Guarding ? "  GUARD" : "";
            var down = p.Down ? "  DOWN" : "";
            Runtime.SetUIText(playerStatusText,
                $"PLAYER  HP {p.CurrentHp}/{p.MaxHp}   SP {p.CurrentSp}/{p.MaxSp}{guard}{down}");

            var ratio = p.MaxHp > 0 ? (float)p.CurrentHp / p.MaxHp : 0.0f;
            if (ratio < 0.0f) ratio = 0.0f;
            if (ratio > 1.0f) ratio = 1.0f;
            Runtime.SetUIRect(playerHpBar,
                new Vector2(-640.0f, -410.0f),
                new Vector2(520.0f * ratio, 18.0f),
                new Vector2(1.0f, 1.0f), 0.0f, 2);
        }

        if (TryReadCombatant(enemyFireCombatant, out var f))
        {
            Runtime.SetUIText(enemyFireStatusText,
                f.Alive ? $"FIRE-WEAK  HP {f.CurrentHp}/{f.MaxHp}" + (f.Down ? "  DOWN" : "") : "FIRE-WEAK  DOWN");
        }
        if (TryReadCombatant(enemyIceCombatant, out var i))
        {
            Runtime.SetUIText(enemyIceStatusText,
                i.Alive ? $"ICE-WEAK  HP {i.CurrentHp}/{i.MaxHp}" + (i.Down ? "  DOWN" : "") : "ICE-WEAK  DOWN");
        }
    }

    private void UpdateTargetHighlight()
    {
        var focus = Runtime.GetUIFocus();
        var fireFocused = focus.Status == RuntimeStatus.Ok && SameObject(focus.Value, targetEnemyFireWeak);
        var iceFocused = focus.Status == RuntimeStatus.Ok && SameObject(focus.Value, targetEnemyIceWeak);

        Runtime.SetUIImageColor(targetEnemyFireWeak,
            fireFocused ? new Color(1.0f, 0.26f, 0.18f, 0.98f) : new Color(0.18f, 0.06f, 0.08f, 0.88f));
        Runtime.SetUIImageColor(targetEnemyIceWeak,
            iceFocused ? new Color(0.20f, 0.62f, 1.0f, 0.98f) : new Color(0.05f, 0.10f, 0.18f, 0.88f));
    }

    private bool ApplyPreset(ComponentHandle component, PersonaCombatantPreset preset)
    {
        var ok = true;
        ok &= SetString(component, "DisplayName", preset.Name);
        ok &= SetBool(component, "IsEnemy", preset.Enemy);
        ok &= SetInt(component, "MaxHp", preset.Hp);
        ok &= SetInt(component, "CurrentHp", preset.Hp);
        ok &= SetInt(component, "MaxSp", preset.Sp);
        ok &= SetInt(component, "CurrentSp", preset.Sp);
        ok &= SetInt(component, "AttackPower", preset.Attack);
        ok &= SetInt(component, "PhysicalAffinity", (int)preset.Physical);
        ok &= SetInt(component, "FireAffinity", (int)preset.Fire);
        ok &= SetInt(component, "IceAffinity", (int)preset.Ice);
        ok &= SetBool(component, "KnowsAgi", preset.KnowsAgi);
        ok &= SetBool(component, "KnowsBufu", preset.KnowsBufu);
        ok &= SetBool(component, "Down", false);
        ok &= SetBool(component, "Guarding", false);
        return ok;
    }

    private bool TryReadCombatant(ComponentHandle component, out PersonaCombatantSnapshot value)
    {
        value = default;
        var displayName = Runtime.GetScriptFieldString(component, "DisplayName");
        var isEnemy = Runtime.GetScriptFieldBool(component, "IsEnemy");
        var maxHp = Runtime.GetScriptFieldInt(component, "MaxHp");
        var currentHp = Runtime.GetScriptFieldInt(component, "CurrentHp");
        var maxSp = Runtime.GetScriptFieldInt(component, "MaxSp");
        var currentSp = Runtime.GetScriptFieldInt(component, "CurrentSp");
        var attackPower = Runtime.GetScriptFieldInt(component, "AttackPower");
        var physical = Runtime.GetScriptFieldInt(component, "PhysicalAffinity");
        var fire = Runtime.GetScriptFieldInt(component, "FireAffinity");
        var ice = Runtime.GetScriptFieldInt(component, "IceAffinity");
        var knowsAgi = Runtime.GetScriptFieldBool(component, "KnowsAgi");
        var knowsBufu = Runtime.GetScriptFieldBool(component, "KnowsBufu");
        var down = Runtime.GetScriptFieldBool(component, "Down");
        var guarding = Runtime.GetScriptFieldBool(component, "Guarding");

        if (displayName.Status != RuntimeStatus.Ok || isEnemy.Status != RuntimeStatus.Ok ||
            maxHp.Status != RuntimeStatus.Ok || currentHp.Status != RuntimeStatus.Ok ||
            maxSp.Status != RuntimeStatus.Ok || currentSp.Status != RuntimeStatus.Ok ||
            attackPower.Status != RuntimeStatus.Ok || physical.Status != RuntimeStatus.Ok ||
            fire.Status != RuntimeStatus.Ok || ice.Status != RuntimeStatus.Ok ||
            knowsAgi.Status != RuntimeStatus.Ok || knowsBufu.Status != RuntimeStatus.Ok ||
            down.Status != RuntimeStatus.Ok || guarding.Status != RuntimeStatus.Ok)
        {
            Runtime.LogError("PersonaBattleDirector: PersonaCombatant の公開フィールドを読み取れません", GameObject);
            return false;
        }

        value.DisplayName = displayName.Value;
        value.IsEnemy = isEnemy.Value;
        value.MaxHp = maxHp.Value;
        value.CurrentHp = currentHp.Value;
        value.MaxSp = maxSp.Value;
        value.CurrentSp = currentSp.Value;
        value.AttackPower = attackPower.Value;
        value.PhysicalAffinity = (PersonaAffinity)physical.Value;
        value.FireAffinity = (PersonaAffinity)fire.Value;
        value.IceAffinity = (PersonaAffinity)ice.Value;
        value.KnowsAgi = knowsAgi.Value;
        value.KnowsBufu = knowsBufu.Value;
        value.Down = down.Value;
        value.Guarding = guarding.Value;
        return true;
    }

    private bool IsAlive(ComponentHandle component)
    {
        var hp = Runtime.GetScriptFieldInt(component, "CurrentHp");
        return hp.Status == RuntimeStatus.Ok && hp.Value > 0;
    }

    private bool PlayMotion(ObjectHandle owner, string key, out MotionPlayer playerValue)
    {
        playerValue = default;
        if (string.IsNullOrEmpty(key)) return false;

        var playerResult = Runtime.FindMotionPlayer(owner, key);
        if (playerResult.Status != RuntimeStatus.Ok || !playerResult.Value.IsValid)
            return false;

        // Scene には割り当て先の MotionPlayer を用意しているが、Motion Asset は依頼者が作る。
        // Asset 未設定（duration <= 0）は「演出なし」とみなし、待ち時間を発生させない。
        var duration = playerResult.Value.GetDuration();
        if (duration.Status != RuntimeStatus.Ok || duration.Value <= 0.0f)
            return false;

        var status = playerResult.Value.PlayFrom(0.0f);
        if (status != RuntimeStatus.Ok) return false;
        playerValue = playerResult.Value;
        return true;
    }

    private float ResolveMotionFinishTimeout(MotionPlayer playerValue)
    {
        var fallback = PositiveOr(ActionFinishFallbackSeconds, 2.20f);
        var duration = playerValue.GetDuration();
        if (duration.Status != RuntimeStatus.Ok || duration.Value <= 0.0f) return fallback;
        var withMargin = duration.Value + 0.50f;
        return withMargin > fallback ? withMargin : fallback;
    }

    private void StopTargetVignetteMotion()
    {
        if (targetVignetteMotionStarted && targetVignetteMotion.IsValid)
            targetVignetteMotion.Stop();
        targetVignetteMotionStarted = false;
        targetVignetteMotion = default;
    }

    private static bool MotionFinished(MotionPlayer playerValue)
    {
        if (!playerValue.IsValid) return true;
        var playing = playerValue.IsPlaying();
        return playing.Status == RuntimeStatus.Ok && !playing.Value;
    }

    private void PlayAudioIfAssigned(string path)
    {
        if (string.IsNullOrWhiteSpace(path)) return;
        var result = Runtime.PlayAudio(path, false, 1.0f, 1.0f);
        if (result.Status != RuntimeStatus.Ok)
            Runtime.LogWarning($"PersonaBattleDirector: Audio を再生できません: {path}", GameObject);
    }

    private bool Pressed(string action)
    {
        var result = Runtime.InputPressed(action);
        return result.Status == RuntimeStatus.Ok && result.Value;
    }

    private void HideAllMenus()
    {
        StopTargetVignetteMotion();
        Runtime.SetEnabled(commandMenu, false);
        Runtime.SetEnabled(skillMenu, false);
        Runtime.SetEnabled(targetMenu, false);
        Runtime.SetEnabled(allOutMenu, false);
    }

    private void SetState(PersonaBattleState state)
    {
        CurrentState = (int)state;
        stateElapsed = 0.0f;
    }

    private bool ResolveSceneObjects()
    {
        var ok = true;
        ok &= FindRequired("Player", out player);
        ok &= FindRequired("EnemyFireWeak", out enemyFireWeak);
        ok &= FindRequired("EnemyIceWeak", out enemyIceWeak);
        ok &= FindRequired("BattleEffects", out battleEffects);
        ok &= FindRequired("BattleCanvas", out battleCanvas);
        ok &= FindRequired("PlayerStatusText", out playerStatusText);
        ok &= FindRequired("PlayerHpBar", out playerHpBar);
        ok &= FindRequired("EnemyFireStatusText", out enemyFireStatusText);
        ok &= FindRequired("EnemyIceStatusText", out enemyIceStatusText);
        ok &= FindRequired("BattleMessage", out battleMessage);
        ok &= FindRequired("DamageText", out damageText);
        ok &= FindRequired("OneMoreText", out oneMoreText);
        ok &= FindRequired("ResultText", out resultText);
        ok &= FindRequired("CommandMenu", out commandMenu);
        ok &= FindRequired("CommandAttack", out commandAttack);
        ok &= FindRequired("CommandSkill", out commandSkill);
        ok &= FindRequired("CommandGuard", out commandGuard);
        ok &= FindRequired("SkillMenu", out skillMenu);
        ok &= FindRequired("SkillAgi", out skillAgi);
        ok &= FindRequired("SkillBufu", out skillBufu);
        ok &= FindRequired("TargetMenu", out targetMenu);
        ok &= FindRequired("TargetEnemyFireWeak", out targetEnemyFireWeak);
        ok &= FindRequired("TargetEnemyIceWeak", out targetEnemyIceWeak);
        ok &= FindRequired("AllOutMenu", out allOutMenu);
        ok &= FindRequired("AllOutAttack", out allOutAttack);
        ok &= FindRequired("AllOutCancel", out allOutCancel);
        if (!ok) return false;

        ok &= FindCombatantComponent("Player", player, out playerCombatant);
        ok &= FindCombatantComponent("EnemyFireWeak", enemyFireWeak, out enemyFireCombatant);
        ok &= FindCombatantComponent("EnemyIceWeak", enemyIceWeak, out enemyIceCombatant);
        return ok;
    }

    private bool FindRequired(string name, out ObjectHandle handle)
    {
        var result = Runtime.FindGameObject(name);
        if (result.Status == RuntimeStatus.Ok && !result.Value.IsEmpty)
        {
            handle = result.Value;
            return true;
        }

        handle = default;
        Runtime.LogError($"PersonaBattleDirector: GameObject '{name}' が見つかりません", GameObject);
        return false;
    }

    private bool FindCombatantComponent(string objectName, ObjectHandle owner, out ComponentHandle component)
    {
        var result = Runtime.GetComponent(owner, PersonaEngineIds.ScriptComponent);
        if (result.Status == RuntimeStatus.Ok && !result.Value.IsEmpty)
        {
            component = result.Value;
            return true;
        }

        component = default;
        Runtime.LogError($"PersonaBattleDirector: '{objectName}' の PersonaCombatant ScriptComponent が見つかりません", GameObject);
        return false;
    }

    private bool SetBool(ComponentHandle component, string field, bool value)
    {
        var status = Runtime.SetScriptFieldBool(component, field, value);
        if (status == RuntimeStatus.Ok) return true;
        Runtime.LogError($"PersonaBattleDirector: field '{field}' の bool 書き込みに失敗しました", GameObject);
        return false;
    }

    private bool SetInt(ComponentHandle component, string field, int value)
    {
        var status = Runtime.SetScriptFieldInt(component, field, value);
        if (status == RuntimeStatus.Ok) return true;
        Runtime.LogError($"PersonaBattleDirector: field '{field}' の int 書き込みに失敗しました", GameObject);
        return false;
    }

    private bool SetString(ComponentHandle component, string field, string value)
    {
        var status = Runtime.SetScriptFieldString(component, field, value);
        if (status == RuntimeStatus.Ok) return true;
        Runtime.LogError($"PersonaBattleDirector: field '{field}' の string 書き込みに失敗しました", GameObject);
        return false;
    }

    private static bool SameObject(ObjectHandle a, ObjectHandle b)
    {
        return a.World == b.World && a.Object == b.Object && a.Generation == b.Generation && !a.IsEmpty && !b.IsEmpty;
    }

    private static float PositiveOr(float value, float fallback)
    {
        return value > 0.0f ? value : fallback;
    }
}
