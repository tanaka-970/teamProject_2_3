using System.Collections;
using System.Collections.Generic;
using ReplayEngine;

namespace Game.Terraform;

// アタッチ先: Terraform.replayscene の Director。
// 担当: 進行と UI。タイトル / プレイ / クリアを 1 シーンの中で切り替える。
//
// 謎の解き方は決めない。オーブへ「届いたか」だけを見る。
// 坂を作って登っても、周りを削って下ろしても、同じように取れる。
[ReplayGuid("5b2c7d9e24fa6038b1d5c7ae9f4c0613")]
public class TerraformGame : MonoBehaviour
{
    [SerializeField]
    [Tooltip("集めるオーブの数。シーンに置いた数より多くはならない")]
    int requiredOrbs = 4;

    [Header("実行中の状態")]
    [ReadOnly] public int Collected;
    [ReadOnly] public float Elapsed;
    [ReadOnly] public string Phase = "Title";

    // 自動確認から読む集計。
    [HideInInspector] public int TotalOrbs;
    [HideInInspector] public bool Cleared;

    enum Mode { Title, Playing, Clear }

    Mode mode = Mode.Title;
    readonly List<TerraformOrb> orbs = new();
    TerraformWalker? walker;
    TerraformSculptor? sculptor;
    TerraformCamera? rig;

    GameObject? banner;
    GameObject? hud;
    GameObject? gate;
    UIText? orbText;
    UIText? timeText;
    UIText? hintText;
    UIText? bannerTitle;
    UIText? bannerBody;

    void Awake()
    {
        var playerObject = GameObject.Find("Player");
        if (playerObject != null)
        {
            // 同じ GameObject に付いた 2 つの MonoBehaviour を、同じ入口で引く。
            walker = playerObject.GetComponent<TerraformWalker>();
            sculptor = playerObject.GetComponent<TerraformSculptor>();
        }

        var rigObject = GameObject.Find("CameraRig");
        rig = rigObject != null ? rigObject.GetComponent<TerraformCamera>() : null;

        var field = GameObject.Find("Orbs");
        if (field != null) orbs.AddRange(field.GetComponentsInChildren<TerraformOrb>());
        TotalOrbs = orbs.Count;

        gate = GameObject.Find("Gate");
        banner = GameObject.Find("Banner");
        hud = GameObject.Find("Hud");
        orbText = FindText("OrbValue");
        timeText = FindText("TimeValue");
        hintText = FindText("HintText");
        bannerTitle = FindText("BannerTitle");
        bannerBody = FindText("BannerBody");

        Debug.Log($"Terraform: orbs={orbs.Count}", this);
    }

    void Start() => EnterTitle();

    void Update()
    {
        switch (mode)
        {
            case Mode.Title:
                if (Input.GetKeyDown(KeyCode.Space)) StartRun();
                if (Input.GetKeyDown(KeyCode.Escape)) Application.Quit("Terraform title");
                break;

            case Mode.Playing:
                Elapsed += Time.deltaTime;
                if (timeText != null) timeText.text = Format(Elapsed);
                UpdateHint();
                if (Input.GetKeyDown(KeyCode.R)) StartRun();
                if (Input.GetKeyDown(KeyCode.Escape)) EnterTitle();
                break;

            case Mode.Clear:
                if (Input.GetKeyDown(KeyCode.Space)) EnterTitle();
                if (Input.GetKeyDown(KeyCode.Escape)) Application.Quit("Terraform clear");
                break;
        }
    }

    // 一番近い未取得オーブのヒントを出す。
    // 解き方は書かない。「そこに何があるか」だけ伝える。
    void UpdateHint()
    {
        if (hintText == null) return;

        TerraformOrb? nearest = null;
        var best = float.MaxValue;
        foreach (var orb in orbs)
        {
            if (orb.Taken) continue;
            if (orb.DistanceToPlayer < best)
            {
                best = orb.DistanceToPlayer;
                nearest = orb;
            }
        }

        hintText.text = nearest != null && nearest.Hint.Length > 0
            ? nearest.Hint + "   （" + (int)best + "m）"
            : "";
    }

    // ---- 進行 -----------------------------------------------------------------

    void EnterTitle()
    {
        mode = Mode.Title;
        Phase = "Title";
        Cleared = false;
        ResetRun();

        hud?.SetActive(false);
        ShowBanner("TERRAFORM",
            "SPACE ではじめる\n\n" +
            "WASD 移動    マウス 視点    ホイール ブラシの大きさ\n" +
            "左クリック 盛る    右クリック 削る    F ならす\n\n" +
            "地面を作りかえて、4 つのオーブに手を届かせる。\n" +
            "解き方は決まっていない。");
    }

    // 自動確認から開始させる入口。人が SPACE を押すのと同じ経路を通す。
    public void BeginRunFromDemo() => StartRun();

    void StartRun()
    {
        mode = Mode.Playing;
        Phase = "Playing";
        Cleared = false;
        ResetRun();

        if (walker != null) walker.ControlEnabled = true;
        if (sculptor != null) sculptor.ControlEnabled = true;

        HideBanner();
        hud?.SetActive(true);
        RefreshHud();
    }

    void ResetRun()
    {
        Collected = 0;
        Elapsed = 0.0f;

        foreach (var orb in orbs) orb.ResetOrb();

        if (walker != null)
        {
            walker.ControlEnabled = false;
            walker.MoveTo(new Vector3(0.0f, 0.0f, -24.0f));
        }
        if (sculptor != null) sculptor.ControlEnabled = false;
        rig?.SnapBehindTarget();

        gate?.SetActive(true);
        RefreshHud();
    }

    void EnterClear()
    {
        mode = Mode.Clear;
        Phase = "Clear";
        Cleared = true;

        if (walker != null) walker.ControlEnabled = false;
        if (sculptor != null) sculptor.ControlEnabled = false;

        gate?.SetActive(false);
        hud?.SetActive(false);
        ShowBanner("CLEAR",
            $"TIME {Format(Elapsed)}\n\n" +
            $"盛った {sculptor?.RaiseTicks ?? 0}   削った {sculptor?.LowerTicks ?? 0}   " +
            $"ならした {sculptor?.SmoothTicks ?? 0}\n\nSPACE でタイトルへ");
        Debug.Log($"Terraform clear: time={Elapsed:0.0}", this);
    }

    // ---- 通知 -----------------------------------------------------------------

    public void OnOrbTaken(TerraformOrb orb)
    {
        if (mode != Mode.Playing) return;
        ++Collected;
        RefreshHud();
        Debug.Log($"orb {Collected}/{Mathf.Min(requiredOrbs, orbs.Count)}", this);

        if (Collected >= Mathf.Min(requiredOrbs, orbs.Count)) StartCoroutine(OpenGate());
    }

    // 少し余韻を置いてからクリアへ。取った瞬間に画面が変わらないようにする。
    IEnumerator OpenGate()
    {
        yield return new WaitForSeconds(0.9f);
        if (mode == Mode.Playing) EnterClear();
    }

    // ---- UI -------------------------------------------------------------------

    void RefreshHud()
    {
        var goal = Mathf.Min(requiredOrbs, orbs.Count);
        if (orbText != null) orbText.text = Collected + " / " + goal;
        if (timeText != null) timeText.text = Format(Elapsed);
    }

    void ShowBanner(string title, string body)
    {
        banner?.SetActive(true);
        if (bannerTitle != null) bannerTitle.text = title;
        if (bannerBody != null) bannerBody.text = body;
    }

    void HideBanner() => banner?.SetActive(false);

    UIText? FindText(string name)
    {
        var found = GameObject.Find(name);
        return found != null ? found.GetComponent<UIText>() : null;
    }

    static string Format(float seconds)
    {
        var clamped = Mathf.Max(0.0f, seconds);
        var minutes = (int)(clamped / 60.0f);
        var rest = (int)(clamped - minutes * 60.0f);
        return minutes + ":" + (rest < 10 ? "0" : "") + rest;
    }
}
