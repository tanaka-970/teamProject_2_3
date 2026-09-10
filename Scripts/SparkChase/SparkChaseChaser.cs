using ReplayEngine;

namespace Game.SparkChase;

// アタッチ先: SparkChase.replayscene の Chaser0..Chaser2。
// 担当: プレイヤーを追いかける敵。接触でダメージを与える。
//
// 追跡は素朴な直進。強化は今回の対象ではない。
[ReplayGuid("6b4cae3027d56f89cb03ae517f942d63")]
public class SparkChaseChaser : MonoBehaviour
{
    [SerializeField]
    [Tooltip("追跡の速さ")]
    [Range(0.5, 12.0)]
    float chaseSpeed = 3.1f;

    [SerializeField]
    [Tooltip("これより近いと当たり判定を出す")]
    float hitRadius = 1.05f;

    [SerializeField]
    [Tooltip("当ててから次に当てるまでの間隔")]
    float hitCooldown = 1.2f;

    [ReadOnly]
    public float DistanceToPlayer;

    [HideInInspector]
    public bool Active;

    Transform? target;
    SparkChaseGame? director;
    MeshRenderer? shell;
    Vector3 homePosition;
    float cooldown;

    void Awake()
    {
        homePosition = transform.position;
        shell = GetComponent<MeshRenderer>();

        var player = GameObject.Find("Player");
        target = player != null ? player.transform : null;

        var host = GameObject.Find("Director");
        director = host != null ? host.GetComponent<SparkChaseGame>() : null;
    }

    void Update()
    {
        if (!Active || target == null) return;

        var toPlayer = target.position - transform.position;
        toPlayer = new Vector3(toPlayer.X, 0.0f, toPlayer.Z);
        DistanceToPlayer = toPlayer.Magnitude;

        if (DistanceToPlayer > 0.05f)
        {
            var step = toPlayer.Normalized * chaseSpeed * Time.deltaTime;
            transform.position += step;
            // 進む向きへ体を向ける。
            transform.LookAt(new Vector3(target.position.X, transform.position.Y,
                target.position.Z));
        }

        if (cooldown > 0.0f) cooldown -= Time.deltaTime;

        // 近づいたら 1 回だけ当てる。Trigger を待たずに距離で判定するのは、
        // どちらも動いていて Enter を取りこぼす場合があるため。
        if (cooldown <= 0.0f && DistanceToPlayer <= hitRadius)
        {
            cooldown = hitCooldown;
            director?.OnPlayerHit(this);
        }

        // 見た目で危険度を伝える。
        if (shell != null)
        {
            var danger = Mathf.Clamp01(1.0f - DistanceToPlayer / 8.0f);
            shell.color = new Color(0.62f + danger * 0.38f, 0.24f - danger * 0.12f,
                0.36f - danger * 0.14f, 1.0f);
        }
    }

    public void ResetToHome()
    {
        transform.position = homePosition;
        cooldown = 0.0f;
        DistanceToPlayer = 0.0f;
    }
}
