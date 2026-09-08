using ReplayEngine;

namespace Game.SparkChase;

// アタッチ先: SparkChase.replayscene の Spark0..Spark17。
// 担当: 回転して漂う集めもの。Trigger で拾われる。
[ReplayGuid("7c5dbf4138e67a9adc14bf6280a53e74")]
public class SparkChasePickup : MonoBehaviour
{
    [SerializeField]
    [Tooltip("1 個あたりの得点")]
    int score = 1;

    [SerializeField]
    float spinDegreesPerSecond = 150.0f;

    [SerializeField]
    float bobHeight = 0.28f;

    [ReadOnly]
    public bool Taken;

    public int Score => score;

    SparkChaseGame? director;
    MeshRenderer? shell;
    Vector3 basePosition;
    float phase;

    void Awake()
    {
        basePosition = transform.position;
        shell = GetComponent<MeshRenderer>();

        var host = GameObject.Find("Director");
        director = host != null ? host.GetComponent<SparkChaseGame>() : null;

        // 同じ位置に重ならないよう、初期位相を座標から作る。
        phase = basePosition.X * 0.7f + basePosition.Z * 1.3f;
    }

    void Update()
    {
        if (Taken) return;

        // 回転と上下の漂い。degree で扱えるのが Unity と同じ点。
        transform.Rotate(0.0f, spinDegreesPerSecond * Time.deltaTime, 0.0f);
        phase += Time.deltaTime * 2.2f;
        transform.position = basePosition + Vector3.Up * (Mathf.Sin(phase) * bobHeight);
    }

    // Player の Collider が触れると呼ばれる。
    void OnTriggerEnter(Collider other)
    {
        if (Taken || other.gameObject == null) return;
        if (other.gameObject.GetComponent<SparkChasePlayer>() == null) return;

        Taken = true;
        gameObject.SetActive(false);
        director?.OnSparkTaken(this);
    }

    // 次の波で置き直す。Destroy せず使い回すのは、
    // Native 側の遅延破棄と生成を毎波くり返さないため。
    public void Respawn(Vector3 position)
    {
        Taken = false;
        basePosition = position;
        transform.position = position;
        gameObject.SetActive(true);
        if (shell != null) shell.visible = true;
    }
}
