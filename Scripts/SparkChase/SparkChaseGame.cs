using System.Collections;
using System.Collections.Generic;
using ReplayEngine;

namespace Game.SparkChase;

// アタッチ先: SparkChase.replayscene の Director。
// 担当: 進行、スコア、UI、波の生成。1 シーンの中で
//       タイトル / プレイ / リザルトを切り替える。
//
// 新しい Authoring API だけで書いてある。
[ReplayGuid("4f2a8c1e05b34d67a9e18c3f5d720b41")]
public class SparkChaseGame : MonoBehaviour
{
    [SerializeField]
    [Tooltip("1 試合の秒数")]
    [Range(10.0, 300.0)]
    float matchSeconds = 75.0f;

    [SerializeField]
    [Tooltip("開始時の体力")]
    [Range(1.0, 9.0)]
    int startLife = 3;

    [SerializeField]
    [Tooltip("集めものを置く半径")]
    float fieldRadius = 15.5f;

    [Header("実行中の状態")]
    [ReadOnly]
    public int Score;

    [ReadOnly]
    public int Life;

    [ReadOnly]
    public int Wave;

    [ReadOnly]
    public float Remaining;

    // 自動確認から結果を読むための集計。
    [HideInInspector]
    public int SparksTaken;

    [HideInInspector]
    public int HitsTaken;

    [HideInInspector]
    public string Phase = "Title";

    enum Mode { Title, Playing, Result }

    Mode mode = Mode.Title;
    SparkChasePlayer? player;
    SparkChaseCamera? rig;
    readonly List<SparkChasePickup> sparks = new();
    readonly List<SparkChaseChaser> chasers = new();

    // UI
    UIText? scoreText;
    UIText? timerText;
    UIText? lifeText;
    UIText? bannerTitle;
    UIText? bannerBody;
    GameObject? banner;
    GameObject? hud;

    // 乱数は種を固定する。自動確認の結果が毎回変わらないようにするため。
    int randomState = 20260907;

    void Awake()
    {
        var playerObject = GameObject.Find("Player");
        player = playerObject != null ? playerObject.GetComponent<SparkChasePlayer>() : null;

        var rigObject = GameObject.Find("CameraRig");
        rig = rigObject != null ? rigObject.GetComponent<SparkChaseCamera>() : null;

        // 集めものと敵はシーンに置いてある分を集める。
        // 名前で 1 つずつ引くより、種類で数えられる方が増減に強い。
        var field = GameObject.Find("Field");
        if (field != null)
        {
            sparks.AddRange(field.GetComponentsInChildren<SparkChasePickup>());
            chasers.AddRange(field.GetComponentsInChildren<SparkChaseChaser>());
        }

        banner = GameObject.Find("Banner");
        hud = GameObject.Find("Hud");
        scoreText = FindText("ScoreValue");
        timerText = FindText("TimerValue");
        lifeText = FindText("LifeValue");
        bannerTitle = FindText("BannerTitle");
        bannerBody = FindText("BannerBody");

        Debug.Log($"SparkChase: spark={sparks.Count} chaser={chasers.Count}", this);
    }

    void Start() => EnterTitle();

    void Update()
    {
        switch (mode)
        {
            case Mode.Title:
                if (Input.GetKeyDown(KeyCode.Space)) StartMatch();
                if (Input.GetKeyDown(KeyCode.Escape)) Application.Quit("SparkChase title");
                break;

            case Mode.Playing:
                UpdateMatch();
                break;

            case Mode.Result:
                if (Input.GetKeyDown(KeyCode.Space)) EnterTitle();
                if (Input.GetKeyDown(KeyCode.Escape)) Application.Quit("SparkChase result");
                break;
        }
    }

    void UpdateMatch()
    {
        Remaining -= Time.deltaTime;
        if (timerText != null) timerText.text = Format(Remaining);

        // 落ちたら戻す。落下死にはしない。
        if (player != null && player.FellOffStage())
        {
            player.Respawn();
            rig?.SnapBehindTarget();
            ApplyDamage();
        }

        if (Remaining <= 0.0f) EnterResult("TIME UP");
    }

    // ---- 進行 -----------------------------------------------------------------

    void EnterTitle()
    {
        mode = Mode.Title;
        Phase = "Title";
        StopAllCoroutines();

        Score = 0;
        Life = startLife;
        Wave = 0;
        Remaining = matchSeconds;

        if (player != null)
        {
            player.ControlEnabled = false;
            player.Respawn();
        }
        rig?.SnapBehindTarget();

        foreach (var chaser in chasers)
        {
            chaser.Active = false;
            chaser.ResetToHome();
        }
        foreach (var spark in sparks) spark.gameObject.SetActive(false);

        hud?.SetActive(false);
        ShowBanner("SPARK CHASE", "SPACE ではじめる\nWASD 移動   SPACE ジャンプ   マウス 視点");
    }

    void StartMatch()
    {
        mode = Mode.Playing;
        Phase = "Playing";

        Score = 0;
        Life = startLife;
        Wave = 0;
        Remaining = matchSeconds;
        SparksTaken = 0;
        HitsTaken = 0;

        if (player != null)
        {
            player.Respawn();
            player.ControlEnabled = true;
        }
        rig?.SnapBehindTarget();

        HideBanner();
        hud?.SetActive(true);
        RefreshHud();

        StartCoroutine(RunWaves());
    }

    void EnterResult(string reason)
    {
        mode = Mode.Result;
        Phase = "Result";
        StopAllCoroutines();

        if (player != null) player.ControlEnabled = false;
        foreach (var chaser in chasers) chaser.Active = false;
        foreach (var spark in sparks) spark.gameObject.SetActive(false);

        hud?.SetActive(false);
        ShowBanner(reason,
            $"SCORE {Score}\n集めた {SparksTaken}   被弾 {HitsTaken}   WAVE {Wave}\n\nSPACE でタイトルへ");
        Debug.Log($"SparkChase result: score={Score} sparks={SparksTaken} hits={HitsTaken}", this);
    }

    // ---- 波 -------------------------------------------------------------------
    //
    // Coroutine で「置く → 全部取られるまで待つ → 少し休む」を繰り返す。

    IEnumerator RunWaves()
    {
        yield return new WaitForSeconds(0.4f);

        while (mode == Mode.Playing)
        {
            ++Wave;
            SpawnWave();

            // 敵はこの波の数だけ起こす。
            for (var index = 0; index < chasers.Count; ++index)
            {
                chasers[index].Active = index < Mathf.Min(Wave, chasers.Count);
            }

            yield return new WaitUntil(() => AllSparksTaken() || mode != Mode.Playing);
            if (mode != Mode.Playing) yield break;

            yield return new WaitForSeconds(0.8f);
        }
    }

    void SpawnWave()
    {
        // 波が進むほど数を増やす。用意した分を超えない。
        var count = Mathf.Min(6 + Wave * 2, sparks.Count);
        for (var index = 0; index < sparks.Count; ++index)
        {
            if (index < count) sparks[index].Respawn(RandomFieldPoint());
            else sparks[index].gameObject.SetActive(false);
        }
    }

    bool AllSparksTaken()
    {
        foreach (var spark in sparks)
        {
            if (spark.gameObject.activeSelf && !spark.Taken) return false;
        }
        return true;
    }

    // ---- 通知 -----------------------------------------------------------------

    public void OnSparkTaken(SparkChasePickup spark)
    {
        if (mode != Mode.Playing) return;
        Score += spark.Score;
        ++SparksTaken;
        RefreshHud();
    }

    public void OnPlayerHit(SparkChaseChaser chaser)
    {
        if (mode != Mode.Playing) return;
        ++HitsTaken;
        ApplyDamage();

        // 当たった敵は一度下がる。張り付いて連続で削らないため。
        chaser.ResetToHome();
    }

    void ApplyDamage()
    {
        if (mode != Mode.Playing) return;
        --Life;
        RefreshHud();
        if (Life <= 0) EnterResult("GAME OVER");
    }

    // ---- UI -------------------------------------------------------------------

    void RefreshHud()
    {
        if (scoreText != null) scoreText.text = Score.ToString();
        if (lifeText != null) lifeText.text = new string('♥', Mathf.Max(0, Life));
        if (timerText != null) timerText.text = Format(Remaining);
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

    // ---- 乱数 -----------------------------------------------------------------
    //
    // System.Random を使わないのは、種を固定して自動確認の結果を
    // 毎回同じにしたいから。線形合同法で十分。

    Vector3 RandomFieldPoint()
    {
        var angle = NextFloat() * Mathf.PI * 2.0f;
        var radius = Mathf.Sqrt(NextFloat()) * fieldRadius;
        return new Vector3(Mathf.Sin(angle) * radius, 1.05f, Mathf.Cos(angle) * radius);
    }

    float NextFloat()
    {
        randomState = randomState * 1103515245 + 12345;
        return ((randomState >> 8) & 0xFFFF) / 65535.0f;
    }
}
