using System.Collections;
using System.Collections.Generic;
using ReplayEngine;

namespace Game.SwordClash;

// アタッチ先: SwordClash.replayscene の Director。
// 担当: タイトル / 対戦 / 結果の進行、ストック、HUD、画面エフェクトの制御。
//
// 1 シーンの中で 3 つの状態を切り替える。Scene は読み直さない。
// execution_order を後ろにしてあるので、Fighter の Awake より後に走る。
[ReplayGuid("1a48d93e7c0b562f8e41ad35b9726c08")]
public class SwordClashGame : MonoBehaviour
{
    [SerializeField]
    [Tooltip("1 人あたりのストック")]
    [Range(1.0, 9.0)]
    int startStocks = 3;

    [SerializeField]
    [Tooltip("撃墜から復帰までの秒数")]
    [Range(0.2, 3.0)]
    float respawnDelay = 1.1f;

    [Header("状態")]
    [ReadOnly]
    public string Phase = "Title";

    // 自動確認から読むための集計。
    [HideInInspector]
    public int TotalHits;

    [HideInInspector]
    public int TotalKOs;

    [HideInInspector]
    public int Counters;

    enum Mode { Title, Playing, Result }

    Mode mode = Mode.Title;

    readonly List<SwordClashFighter> fighters = new();
    SwordClashCamera? rig;
    ScreenEffectStack? screenFx;

    // UI。名前で 1 回だけ引いて持っておく。
    GameObject? hud;
    GameObject? banner;
    UIText? bannerTitle;
    UIText? bannerBody;
    UIText? damage1;
    UIText? damage2;
    UIText? stock1;
    UIText? stock2;
    UIImage? gauge1;
    UIImage? gauge2;

    void Awake()
    {
        var stage = GameObject.Find("Fighters");
        if (stage != null) fighters.AddRange(stage.GetComponentsInChildren<SwordClashFighter>());

        var rigObject = GameObject.Find("CameraRig");
        rig = rigObject != null ? rigObject.GetComponent<SwordClashCamera>() : null;

        // 画面エフェクトは Scene に並べてある順で触る。0=ビネット 1=色収差 2=グリッチ。
        var fxObject = GameObject.Find("ScreenFx");
        screenFx = fxObject != null ? fxObject.GetComponent<ScreenEffectStack>() : null;

        hud = GameObject.Find("Hud");
        banner = GameObject.Find("Banner");
        bannerTitle = FindText("BannerTitle");
        bannerBody = FindText("BannerBody");
        damage1 = FindText("Damage1Value");
        damage2 = FindText("Damage2Value");
        stock1 = FindText("Stock1Value");
        stock2 = FindText("Stock2Value");
        gauge1 = FindImage("Gauge1Fill");
        gauge2 = FindImage("Gauge2Fill");

        Debug.Log($"SwordClash: fighters={fighters.Count} fx={(screenFx != null)}", this);
    }

    void Start() => EnterTitle();

    void Update()
    {
        switch (mode)
        {
            case Mode.Title:
                if (Input.GetKeyDown(KeyCode.Space)) StartMatch();
                if (Input.GetKeyDown(KeyCode.Escape)) Application.Quit("SwordClash title");
                break;

            case Mode.Playing:
                UpdateMatch();
                break;

            case Mode.Result:
                if (Input.GetKeyDown(KeyCode.Space)) EnterTitle();
                if (Input.GetKeyDown(KeyCode.Escape)) Application.Quit("SwordClash result");
                break;
        }
    }

    void UpdateMatch()
    {
        foreach (var fighter in fighters)
        {
            if (fighter == null || !fighter.ControlEnabled) continue;
            if (!fighter.OutOfBounds()) continue;
            StartCoroutine(KnockOut(fighter));
        }
        RefreshHud();
    }

    // ---- 進行 -------------------------------------------------------------

    void EnterTitle()
    {
        mode = Mode.Title;
        Phase = "Title";
        StopAllCoroutines();

        foreach (var fighter in fighters)
        {
            if (fighter == null) continue;
            fighter.ControlEnabled = false;
            fighter.Stocks = startStocks;
            fighter.ResetForRound();
        }

        rig?.SnapToFighters();
        SetScreenIntensity(0.55f, 0.10f, 0.0f);

        hud?.SetActive(false);
        ShowBanner("SWORD CLASH",
            "SPACE ではじめる\n\n" +
            "1P  A / D 移動   SPACE ジャンプ   J 斬り   K + 方向 で必殺\n" +
            "K のみ 横B    K + W 上B    K + S 下B（カウンター）");
    }

    void StartMatch()
    {
        mode = Mode.Playing;
        Phase = "Playing";
        TotalHits = 0;
        TotalKOs = 0;
        Counters = 0;

        foreach (var fighter in fighters)
        {
            if (fighter == null) continue;
            fighter.Stocks = startStocks;
            fighter.ResetForRound();
            fighter.ControlEnabled = true;
        }

        rig?.SnapToFighters();
        HideBanner();
        hud?.SetActive(true);
        RefreshHud();
        SetScreenIntensity(0.38f, 0.06f, 0.0f);
    }

    void EnterResult(string title, string body)
    {
        mode = Mode.Result;
        Phase = "Result";
        StopAllCoroutines();

        foreach (var fighter in fighters)
        {
            if (fighter != null) fighter.ControlEnabled = false;
        }

        hud?.SetActive(false);
        ShowBanner(title, body + "\n\nSPACE でタイトルへ");
        SetScreenIntensity(0.70f, 0.16f, 0.0f);
        Debug.Log($"SwordClash result: {title} hits={TotalHits} ko={TotalKOs} counter={Counters}",
            this);
    }

    // ---- 撃墜 -------------------------------------------------------------

    // 落ちた側を少し止めてから戻す。撃墜の間を作る。
    IEnumerator KnockOut(SwordClashFighter fighter)
    {
        fighter.ControlEnabled = false;
        --fighter.Stocks;
        ++TotalKOs;

        rig?.Shake(0.85f, 0.5f);
        StartCoroutine(GlitchBurst(0.55f));
        RefreshHud();

        if (fighter.Stocks <= 0)
        {
            var winner = WinnerOf(fighter);
            EnterResult(winner != null ? Label(winner) + " WIN" : "DRAW",
                $"撃墜 {TotalKOs}    命中 {TotalHits}    カウンター {Counters}");
            yield break;
        }

        yield return new WaitForSeconds(respawnDelay);
        if (mode != Mode.Playing) yield break;

        fighter.Respawn();
        fighter.ControlEnabled = true;
        RefreshHud();
    }

    SwordClashFighter? WinnerOf(SwordClashFighter loser)
    {
        foreach (var fighter in fighters)
        {
            if (fighter != null && !ReferenceEquals(fighter, loser) && fighter.Stocks > 0)
                return fighter;
        }
        return null;
    }

    // ---- Fighter からの通知 -----------------------------------------------

    public void OnHit(SwordClashFighter attacker, SwordClashFighter target, float damage)
    {
        if (mode != Mode.Playing) return;
        ++TotalHits;

        // 蓄積が高いほど大きく揺らす。手応えを数字と一致させる。
        rig?.Shake(0.10f + Mathf.Min(0.45f, target.Damage * 0.004f), 0.28f);
        StartCoroutine(GlitchBurst(Mathf.Min(0.42f, 0.12f + damage * 0.02f)));
        RefreshHud();
    }

    public void OnCounter(SwordClashFighter guard, SwordClashFighter attacker)
    {
        if (mode != Mode.Playing) return;
        ++Counters;
        rig?.Shake(0.55f, 0.4f);
        StartCoroutine(GlitchBurst(0.8f));
    }

    // Fighter が相手を知るための入口。Director が 2 人を持っているので迷わない。
    public SwordClashFighter? RivalOf(SwordClashFighter self)
    {
        foreach (var fighter in fighters)
        {
            if (fighter != null && !ReferenceEquals(fighter, self)) return fighter;
        }
        return null;
    }

    // ---- 画面エフェクト ---------------------------------------------------

    // 当たった瞬間だけ画面を歪ませて戻す。常時掛けると酔うので短くする。
    IEnumerator GlitchBurst(float strength)
    {
        if (screenFx == null) yield break;

        screenFx.SetIntensity(2, strength);
        screenFx.SetIntensity(1, 0.06f + strength * 0.22f);
        yield return new WaitForSeconds(0.07f);

        // 戻しは 6 分割。1 フレームで消すと点滅して見える。
        for (var step = 0; step < 6; ++step)
        {
            if (screenFx == null) yield break;
            var t = 1.0f - (step + 1) / 6.0f;
            screenFx.SetIntensity(2, strength * t);
            screenFx.SetIntensity(1, 0.06f + strength * 0.22f * t);
            yield return null;
        }
        screenFx.SetIntensity(2, 0.0f);
        screenFx.SetIntensity(1, 0.06f);
    }

    void SetScreenIntensity(float vignette, float aberration, float glitch)
    {
        if (screenFx == null) return;
        screenFx.SetIntensity(0, vignette);
        screenFx.SetIntensity(1, aberration);
        screenFx.SetIntensity(2, glitch);
    }

    // ---- UI ---------------------------------------------------------------

    void RefreshHud()
    {
        Apply(0, damage1, stock1, gauge1);
        Apply(1, damage2, stock2, gauge2);
    }

    void Apply(int index, UIText? damageText, UIText? stockText, UIImage? gauge)
    {
        if (index >= fighters.Count) return;
        var fighter = fighters[index];
        if (fighter == null) return;

        var percent = (int)fighter.Damage;
        if (damageText != null)
        {
            damageText.text = percent + "%";
            // 蓄積が上がるほど白 -> 橙 -> 赤。数字を読まなくても危険が分かる。
            damageText.color = Color.Lerp(new Color(0.94f, 0.97f, 1.0f, 1.0f),
                new Color(1.0f, 0.26f, 0.24f, 1.0f), Mathf.Clamp01(percent / 150.0f));
        }
        if (stockText != null) stockText.text = new string('◆', Mathf.Max(0, fighter.Stocks));
        if (gauge != null) gauge.fillAmount = Mathf.Clamp01(percent / 180.0f);
    }

    void ShowBanner(string title, string body)
    {
        banner?.SetActive(true);
        if (bannerTitle != null) bannerTitle.text = title;
        if (bannerBody != null) bannerBody.text = body;
    }

    void HideBanner() => banner?.SetActive(false);

    static string Label(SwordClashFighter fighter) => fighter.secondPlayer ? "CPU" : "1P";

    UIText? FindText(string name)
    {
        var found = GameObject.Find(name);
        return found != null ? found.GetComponent<UIText>() : null;
    }

    UIImage? FindImage(string name)
    {
        var found = GameObject.Find(name);
        return found != null ? found.GetComponent<UIImage>() : null;
    }
}
