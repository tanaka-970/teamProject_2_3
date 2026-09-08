using System.Collections;
using ReplayEngine;

namespace ValidationScripts;

// 新しい Authoring API の検証用。依頼書 31 節の完成イメージと同じ書き方をする。
//
//   override を書かない / deltaTime 引数を取らない
//   ObjectHandle / ComponentHandle / RuntimeResult / RuntimeContext を書かない
[ReplayGuid("a7c1e93b52f04d18b6c25f8a4d3e7b10")]
public class ValidationMonoBehaviour : MonoBehaviour
{
    [SerializeField]
    float moveSpeed = 5.0f;

    [SerializeField]
    private float jumpPower = 8.0f;

    public int AwakeCalls;
    public int StartCalls;
    public int UpdateCalls;
    public int LateUpdateCalls;
    public int FixedUpdateCalls;
    public int EnableCalls;

    [HideInInspector]
    public float LastDeltaTime;

    [HideInInspector]
    public string ObserverName = "";

    Rigidbody? body;

    void Awake()
    {
        ++AwakeCalls;
        body = GetComponent<Rigidbody>();
    }

    void OnEnable() => ++EnableCalls;

    void Start()
    {
        ++StartCalls;
        Debug.Log("ValidationMonoBehaviour started on " + gameObject.name, this);
    }

    void Update()
    {
        ++UpdateCalls;
        LastDeltaTime = Time.deltaTime;

        // Transform の読み書き。degree で扱う。
        var position = transform.position;
        transform.position = position;

        if (Input.GetKeyDown(KeyCode.Space) && body != null)
        {
            body.AddForce(Vector3.Up * jumpPower);
        }

        // 同じ GameObject の別の MonoBehaviour を同じ API で引く。
        var observer = GetComponent<ValidationMonoBehaviourObserver>();
        ObserverName = observer != null ? observer.Label : "";
    }

    void FixedUpdate() => ++FixedUpdateCalls;

    void LateUpdate() => ++LateUpdateCalls;

    // 移動速度を使うだけの補助。moveSpeed が未使用にならないようにする。
    public float StepDistance() => moveSpeed * Time.deltaTime;

    public IEnumerator WaitOneSecond()
    {
        yield return new WaitForSeconds(1.0f);
    }
}

// GetComponent<T>() が Managed Behaviour も同じ入口で引けることの確認用。
[ReplayGuid("b8d2fa4c63015e29c7d36f9b5e4f8c21")]
public class ValidationMonoBehaviourObserver : MonoBehaviour
{
    public string Label = "observer";
}
