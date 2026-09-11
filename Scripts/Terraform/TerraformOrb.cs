using ReplayEngine;

namespace Game.Terraform;

// アタッチ先: Terraform.replayscene の Orb0..Orb3。
// 担当: 集める対象。位置は動かさない。届く方法はプレイヤーが地形で作る。
//
// 「高い所にある」「地面の下に埋まっている」のどちらも、
// 解き方を 1 つに決めない。盛って登っても、周りを削って下ろしても取れる。
[ReplayGuid("4a1b6c8d13e95f27a0c4b69d8e3bf502")]
public class TerraformOrb : MonoBehaviour
{
    [SerializeField]
    [Tooltip("これより近づくと取れる")]
    [Range(0.5, 8.0)]
    float pickupRange = 2.6f;

    [SerializeField]
    [Tooltip("解き方のヒント")]
    string hint = "";

    [SerializeField]
    float spinDegreesPerSecond = 70.0f;

    [ReadOnly] public bool Taken;
    [ReadOnly] public float DistanceToPlayer;

    public string Hint => hint;

    Transform? player;
    TerraformGame? director;
    MeshRenderer? shell;
    Vector3 home;
    float phase;

    void Awake()
    {
        home = transform.position;
        shell = GetComponent<MeshRenderer>();

        var playerObject = GameObject.Find("Player");
        player = playerObject != null ? playerObject.transform : null;

        var host = GameObject.Find("Director");
        director = host != null ? host.GetComponent<TerraformGame>() : null;

        phase = home.X * 0.9f + home.Z * 1.7f;
    }

    void Update()
    {
        if (Taken || player == null) return;

        // その場で回って漂う。位置は地形に影響されない。
        transform.Rotate(0.0f, spinDegreesPerSecond * Time.deltaTime, 0.0f);
        phase += Time.deltaTime * 1.8f;
        transform.position = home + Vector3.Up * (Mathf.Sin(phase) * 0.22f);

        DistanceToPlayer = (player.position - transform.position).Magnitude;

        // 近いほど明るくする。どこまで近づけたかが目で分かる。
        if (shell != null)
        {
            var near = Mathf.Clamp01(1.0f - DistanceToPlayer / 14.0f);
            shell.color = new Color(0.35f + near * 0.65f, 0.85f, 0.55f + near * 0.45f, 1.0f);
        }

        if (DistanceToPlayer <= pickupRange) Take();
    }

    void Take()
    {
        Taken = true;
        gameObject.SetActive(false);
        director?.OnOrbTaken(this);
    }

    public void ResetOrb()
    {
        Taken = false;
        transform.position = home;
        gameObject.SetActive(true);
    }
}
