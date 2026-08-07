using ReplayEngine;

namespace Game;


/// シーン遷移の動作確認用スクリプト。
/// Play 開始から一定時間経過後、指定したシーンへ遷移する。
/// 遷移に失敗した場合はフラグを戻して再試行可能にする。
[ReplayGuid("205ccf78170532b2f086c75618c64235")]
public sealed class NewBehaviour : ScriptBehaviour
{
	/// 遷移先シーンのアセット GUID。
	public string TargetSceneGuid = "0c2eea12c70219725867920c6020a905";

	/// Play 開始からシーン遷移を行うまでの待ち時間（秒）
	public float DelaySeconds = 3.0f;

	/// 経過時間（秒）。動作確認用。
	public float Elapsed = 0.0f;

	/// シーン読み込みを何回要求したか。動作確認用。
	public int LoadRequests = 0;

	/// <summary>最後の "Runtime.LoadScene" の結果。動作確認用。</summary>
	public int LastStatus = -1;

	/// シーン遷移を要求済みかどうか。
	private bool requested_ = false;

	/// 初期化処理。各フィールドをデフォルト状態に戻す。
	public override void Awake()
	{
		Elapsed = 0.0f;
		LoadRequests = 0;
		LastStatus = -1;
		requested_ = false;
	}

	/// 
	/// 毎フレーム呼ばれる更新処理。
	/// <param name="deltaTime">前フレームからの経過時間（秒）。
	public override void Update(float deltaTime)
	{
		Elapsed += deltaTime;

		// 未達なら何もしない。
		if (requested_ || Elapsed < DelaySeconds)
		{
			return;
		}

		requested_ = true;
		LoadRequests += 1;

		RuntimeStatus status = Runtime.LoadScene(TargetSceneGuid);
		LastStatus = (int)status;

		// 失敗した場合は再試行できるようにフラグを戻す。
		if (status != RuntimeStatus.Ok)
		{
			requested_ = false;
		}
	}
}
