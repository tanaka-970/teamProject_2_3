using System.Collections;

namespace ReplayEngine;

// ゲーム制作者が継承する基底クラス。
//
// 【なぜ ScriptBehaviour を継承しないか】
//   ScriptBehaviour には既に public virtual Awake() / Update(float) がある。
//   そこから継承すると、ユーザーが Unity と同じ `void Awake()` を書いた瞬間に
//   「基底のメンバーを隠しています（new が必要）」の警告が出る。
//   override も new も書かせないという目的と両立しない。
//   そこで MonoBehaviour は独立した Public 基底にし、
//   内部の MonoBehaviourHost（ScriptBehaviour 派生）が駆動する。
//   C++ 側の更新経路は 1 本のままで、二つ目の Update システムは作っていない。
//
// ライフサイクルは名前で見つける。override は要らない。
//
//   void Awake()          void OnEnable()      void Start()
//   void FixedUpdate()    void Update()        void LateUpdate()
//   void OnDisable()      void OnDestroy()
//   void OnCollisionEnter(Collision collision)
//   void OnTriggerEnter(Collider other)
//
// private でも protected でも public でも拾う。
public abstract class MonoBehaviour : Behaviour
{
    private MonoBehaviourHost? host;

    internal void Bind(MonoBehaviourHost owner) => host = owner;

    internal MonoBehaviourHost? Host => host;

    public override GameObject gameObject
        => host?.OwnerGameObject ?? throw new System.InvalidOperationException(
            "MonoBehaviour が GameObject へ接続される前に gameObject を使いました。" +
            "フィールド初期化子ではなく Awake() 以降で使ってください。");

    internal override bool IsAlive => host != null && host.IsInstanceAlive &&
        NativeBridge.IsComponentAlive(host.Component);

    internal override bool SameTarget(Object other) => ReferenceEquals(this, other);

    internal override int IdentityHash() => System.Runtime.CompilerServices
        .RuntimeHelpers.GetHashCode(this);

    internal override void DestroySelf() => host?.DestroyComponent();

    // Behaviour の有効・無効。内部では ScriptComponent の enabled を読み書きする。
    public override bool enabled
    {
        get => host != null && host.ComponentEnabled;
        set { if (host != null) host.ComponentEnabled = value; }
    }

    // ---- Coroutine ------------------------------------------------------------
    //
    // 実体は既存の CoroutineRunner。ここは入口を Unity と同じ名前にしているだけ。

    public Coroutine? StartCoroutine(IEnumerator routine) => host?.StartRoutine(routine);

    public void StopCoroutine(Coroutine? routine) => routine?.Cancel();

    public void StopAllCoroutines() => host?.StopRoutines();

    // ---- ログ -----------------------------------------------------------------
    //
    // Debug.Log でも書けるが、発生元 GameObject を自動で付けたいときはこちら。

    protected void Log(string message) => Debug.Log(message, this);
    protected void LogWarning(string message) => Debug.LogWarning(message, this);
    protected void LogError(string message) => Debug.LogError(message, this);
}

// OnCollisionEnter などに渡る接触の情報。
public readonly struct Collision
{
    internal Collision(CollisionInfo source)
    {
        Source = source;
        gameObject = GameObject.Wrap(source.Other);
    }

    internal CollisionInfo Source { get; }

    // 相手の GameObject。相手が既に壊れている場合は null。
    public GameObject? gameObject { get; }

    public Transform? transform => gameObject?.transform;

    public Vector3 relativeVelocity => Source.RelativeVelocity;
    public Vector3 contactPoint => Source.ContactPoint;
    public Vector3 contactNormal => Source.ContactNormal;
    public float penetrationDepth => Source.PenetrationDepth;

    // 相手の Collider。Collider Component を特定できない場合も
    // gameObject は使えるようにしておく。
    // 当たった Collider そのもの。相手が複数の Collider を持っていても、
    // イベントが持つ ColliderID から正しいものを引く。
    public Collider? collider => gameObject != null
        ? Collider.For(gameObject, unchecked((uint)Source.OtherColliderId)) : null;
}
