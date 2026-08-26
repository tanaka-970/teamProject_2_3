using ReplayEngine;

namespace Game;

[ReplayGuid("33d04c24bcae46e09820b2018a4f0cb3")]
public sealed class PersonaMenuShowcase : ScriptBehaviour
{
    private const float F = 1.0f / 30.0f;
    private const float OpenSeconds = 31.0f * F;
    private const float SkillItemSeconds = 6.0f * F;
    private const float ItemStatusSeconds = 4.0f * F;
    private const float StatusItemSeconds = 6.0f * F;
    private const float ItemEnterSeconds = 32.0f * F;
    private const float ItemMorphSeconds = 2.0f * F; // f605 -> f606 -> f607

    private enum Phase { Closed, Opening, Main, MainTransition, EnteringItems, Items, ItemTransition, LeavingItems, Closing }
    private enum MainSelection { Skill, Item, Status }
    private enum ItemSelection { LifeStone, Medicine, Balm }

    private bool initialized;
    private bool inputWarningLogged;
    private Phase phase = Phase.Closed;
    private MainSelection mainSelection = MainSelection.Skill;
    private MainSelection transitionTarget = MainSelection.Skill;
    private ItemSelection itemSelection = ItemSelection.LifeStone;
    private ItemSelection itemTarget = ItemSelection.LifeStone;
    private float phaseTime;
    private float transitionDuration;
    private bool closeAfterItemExit;

    private ObjectHandle hint, openFx, closeFx, mainBase, mainSkill, mainItem, mainStatus;
    private ObjectHandle skillToItem, itemToSkill, itemToStatus, statusToItem;
    private ObjectHandle itemEnter, itemExit, itemBase, itemLife, itemMedicine, itemBalm;
    private ObjectHandle medicineToBalm, balmToMedicine;

    public override void Start()
    {
        initialized = ResolveAll();
        if (!initialized) { Runtime.LogError("PersonaMenuShowcase: 必須オブジェクトが不足しています", GameObject); return; }
        HideEverything();
        Runtime.SetEnabled(hint, true);
        phase = Phase.Closed;
        Runtime.LogInfo("PersonaMenuShowcase ready: Game Viewをクリックして P / START で開きます", GameObject);
    }

    public override void Update(float deltaTime)
    {
        if (!initialized) return;
        phaseTime += deltaTime < 0 ? 0 : deltaTime;
        if (!Runtime.InputAvailable)
        {
            if (!inputWarningLogged) { inputWarningLogged = true; Runtime.LogWarning("PersonaMenuShowcase: Input service unavailable", GameObject); }
            return;
        }

        if (Pressed("Menu"))
        {
            Runtime.LogInfo($"PersonaMenuShowcase: Menu input phase={phase}", GameObject);
            if (phase == Phase.Closed) { BeginOpen(); return; }
            if (phase == Phase.Main) { BeginClose(); return; }
            if (phase == Phase.Items) { closeAfterItemExit = true; BeginItemExit(); return; }
        }

        switch (phase)
        {
            case Phase.Opening: if (phaseTime >= OpenSeconds) FinishOpen(); break;
            case Phase.Main: UpdateMain(); break;
            case Phase.MainTransition: if (phaseTime >= transitionDuration) FinishMainTransition(); break;
            case Phase.EnteringItems: if (phaseTime >= ItemEnterSeconds) FinishItemEnter(); break;
            case Phase.Items: UpdateItems(); break;
            case Phase.ItemTransition: if (phaseTime >= ItemMorphSeconds) FinishItemTransition(); break;
            case Phase.LeavingItems: if (phaseTime >= ItemEnterSeconds) FinishItemExit(); break;
            case Phase.Closing: if (phaseTime >= OpenSeconds) FinishClose(); break;
        }
    }

    private void BeginOpen()
    {
        HideEverything(); Runtime.SetEnabled(hint, false); Runtime.SetEnabled(openFx, true); PlayOnce(openFx, "Open");
        phase = Phase.Opening; phaseTime = 0;
    }
    private void FinishOpen()
    {
        Runtime.SetEnabled(openFx, false); Runtime.SetEnabled(mainBase, true); PlayLoop(mainBase, "Idle");
        mainSelection = MainSelection.Skill; SetMainSelection(mainSelection); phase = Phase.Main; phaseTime = 0;
    }
    private void BeginClose()
    {
        Runtime.SetEnabled(mainBase, false); HideMainSelection(); Runtime.SetEnabled(closeFx, true); PlayOnce(closeFx, "Close");
        phase = Phase.Closing; phaseTime = 0;
    }
    private void FinishClose()
    {
        Runtime.SetEnabled(closeFx, false); Runtime.SetEnabled(hint, true); phase = Phase.Closed; phaseTime = 0;
    }

    private void UpdateMain()
    {
        if (Pressed("UICancel")) { BeginClose(); return; }
        if (Pressed("NavigateDown"))
        {
            if (mainSelection == MainSelection.Skill) BeginMainTransition(MainSelection.Item, skillToItem, "SkillToItem", SkillItemSeconds);
            else if (mainSelection == MainSelection.Item) BeginMainTransition(MainSelection.Status, itemToStatus, "ItemToStatus", ItemStatusSeconds);
            return;
        }
        if (Pressed("NavigateUp"))
        {
            if (mainSelection == MainSelection.Status) BeginMainTransition(MainSelection.Item, statusToItem, "StatusToItem", StatusItemSeconds);
            else if (mainSelection == MainSelection.Item) BeginMainTransition(MainSelection.Skill, itemToSkill, "ItemToSkill", SkillItemSeconds);
            return;
        }
        if (Pressed("UISubmit") && mainSelection == MainSelection.Item) BeginItemEnter();
    }
    private void BeginMainTransition(MainSelection target, ObjectHandle overlay, string key, float duration)
    {
        HideMainSelection(); Runtime.SetEnabled(overlay, true); PlayOnce(overlay, key);
        transitionTarget = target; transitionDuration = duration; phase = Phase.MainTransition; phaseTime = 0;
    }
    private void FinishMainTransition()
    {
        Runtime.SetEnabled(skillToItem, false); Runtime.SetEnabled(itemToSkill, false); Runtime.SetEnabled(itemToStatus, false); Runtime.SetEnabled(statusToItem, false);
        mainSelection = transitionTarget; SetMainSelection(mainSelection); phase = Phase.Main; phaseTime = 0;
    }

    private void BeginItemEnter()
    {
        HideMainSelection(); Runtime.SetEnabled(itemEnter, true); PlayOnce(itemEnter, "ItemEnter"); phase = Phase.EnteringItems; phaseTime = 0;
    }
    private void FinishItemEnter()
    {
        Runtime.SetEnabled(itemEnter, false); Runtime.SetEnabled(mainBase, false); Runtime.SetEnabled(itemBase, true); PlayLoop(itemBase, "Idle");
        itemSelection = ItemSelection.LifeStone; SetItemSelection(itemSelection); phase = Phase.Items; phaseTime = 0;
    }
    private void UpdateItems()
    {
        if (Pressed("UICancel")) { closeAfterItemExit = false; BeginItemExit(); return; }
        if (Pressed("NavigateDown"))
        {
            if (itemSelection == ItemSelection.LifeStone) { itemSelection = ItemSelection.Medicine; SetItemSelection(itemSelection); }
            else if (itemSelection == ItemSelection.Medicine) BeginItemMorph(ItemSelection.Balm, medicineToBalm, "MedicineToBalm");
            return;
        }
        if (Pressed("NavigateUp"))
        {
            if (itemSelection == ItemSelection.Balm) BeginItemMorph(ItemSelection.Medicine, balmToMedicine, "BalmToMedicine");
            else if (itemSelection == ItemSelection.Medicine) { itemSelection = ItemSelection.LifeStone; SetItemSelection(itemSelection); }
        }
    }
    private void BeginItemMorph(ItemSelection target, ObjectHandle overlay, string key)
    {
        HideItemSelection(); Runtime.SetEnabled(overlay, true); PlayOnce(overlay, key); itemTarget = target; phase = Phase.ItemTransition; phaseTime = 0;
    }
    private void FinishItemTransition()
    {
        Runtime.SetEnabled(medicineToBalm, false); Runtime.SetEnabled(balmToMedicine, false); itemSelection = itemTarget; SetItemSelection(itemSelection); phase = Phase.Items; phaseTime = 0;
    }
    private void BeginItemExit()
    {
        HideItemSelection(); Runtime.SetEnabled(itemExit, true); PlayOnce(itemExit, "ItemExit"); phase = Phase.LeavingItems; phaseTime = 0;
    }
    private void FinishItemExit()
    {
        Runtime.SetEnabled(itemExit, false); Runtime.SetEnabled(itemBase, false);
        if (closeAfterItemExit) { closeAfterItemExit = false; BeginCloseFromItems(); return; }
        Runtime.SetEnabled(mainBase, true); PlayLoop(mainBase, "Idle"); mainSelection = MainSelection.Item; SetMainSelection(mainSelection); phase = Phase.Main; phaseTime = 0;
    }
    private void BeginCloseFromItems()
    {
        Runtime.SetEnabled(closeFx, true); PlayOnce(closeFx, "Close"); phase = Phase.Closing; phaseTime = 0;
    }

    private void SetMainSelection(MainSelection s)
    {
        HideMainSelection(); Runtime.SetEnabled(s == MainSelection.Skill ? mainSkill : s == MainSelection.Item ? mainItem : mainStatus, true);
    }
    private void HideMainSelection() { Runtime.SetEnabled(mainSkill,false); Runtime.SetEnabled(mainItem,false); Runtime.SetEnabled(mainStatus,false); }
    private void SetItemSelection(ItemSelection s)
    {
        HideItemSelection(); Runtime.SetEnabled(s == ItemSelection.LifeStone ? itemLife : s == ItemSelection.Medicine ? itemMedicine : itemBalm,true);
    }
    private void HideItemSelection() { Runtime.SetEnabled(itemLife,false); Runtime.SetEnabled(itemMedicine,false); Runtime.SetEnabled(itemBalm,false); }
    private void HideEverything()
    {
        foreach (var h in new[]{openFx,closeFx,mainBase,mainSkill,mainItem,mainStatus,skillToItem,itemToSkill,itemToStatus,statusToItem,itemEnter,itemExit,itemBase,itemLife,itemMedicine,itemBalm,medicineToBalm,balmToMedicine}) Runtime.SetEnabled(h,false);
    }
    private void PlayOnce(ObjectHandle owner,string key)
    {
        var p=Runtime.FindMotionPlayer(owner,key); if(!p.Succeeded||!p.Value.IsValid){Runtime.LogError($"PersonaMenuShowcase: Motion '{key}' not found",owner);return;} p.Value.Stop(); p.Value.SetTime(0); p.Value.SetSpeed(1); p.Value.Play();
    }
    private void PlayLoop(ObjectHandle owner,string key)
    {
        var p=Runtime.FindMotionPlayer(owner,key); if(!p.Succeeded||!p.Value.IsValid)return; p.Value.Stop(); p.Value.SetTime(0); p.Value.SetSpeed(1); p.Value.Play();
    }
    private bool Pressed(string action) { var r=Runtime.InputPressed(action); return r.Succeeded&&r.Value; }
    private bool Resolve(string n,out ObjectHandle h){var r=Runtime.FindGameObject(n);h=r.Value;if(r.Succeeded&&!h.IsEmpty)return true;Runtime.LogError($"PersonaMenuShowcase: '{n}' not found",GameObject);return false;}
    private bool ResolveAll()
    {
        return Resolve("PersonaMenuHint",out hint)&&Resolve("PersonaMenuOpen",out openFx)&&Resolve("PersonaMenuClose",out closeFx)&&Resolve("PersonaMainBase",out mainBase)&&Resolve("PersonaMainSkill",out mainSkill)&&Resolve("PersonaMainItem",out mainItem)&&Resolve("PersonaMainStatus",out mainStatus)&&Resolve("PersonaSkillToItem",out skillToItem)&&Resolve("PersonaItemToSkill",out itemToSkill)&&Resolve("PersonaItemToStatus",out itemToStatus)&&Resolve("PersonaStatusToItem",out statusToItem)&&Resolve("PersonaItemEnter",out itemEnter)&&Resolve("PersonaItemExit",out itemExit)&&Resolve("PersonaItemBase",out itemBase)&&Resolve("PersonaItemLife",out itemLife)&&Resolve("PersonaItemMedicine",out itemMedicine)&&Resolve("PersonaItemBalm",out itemBalm)&&Resolve("PersonaMedicineToBalm",out medicineToBalm)&&Resolve("PersonaBalmToMedicine",out balmToMedicine);
    }
}
