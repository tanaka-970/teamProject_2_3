using ReplayEngine;

namespace Game;

// 動作確認用スクリプト。
//
// 【見かた】
// Play 中に Inspector でこのコンポーネントを見る。
//
//   Frames  … Update が呼ばれた回数。増えていれば動いている
//   Elapsed … 累計経過秒。増えていれば動いている
//   MoveOk  … 位置の書き込みが成功した回数
//   LastStatus … 直近の書き込み結果（0 が成功）
//
// Frames が増えない  -> Update が呼ばれていない（インスタンスが無い）
// Frames は増えるが MoveOk が 0 -> Update は動くが Runtime API が失敗
// 両方増えるのに見た目が変わらない -> 他の Component が位置を上書きしている
//
// オブジェクトが見えている必要はない。数字だけで判断できる。
[ReplayGuid("9c41d7a25b8e4f13a6d0e28c74f5b301")]
public sealed class MoveTest : ScriptBehaviour
{
    // Play 前に Inspector で変えられる。2〜10 くらいが見やすい。
    public float Speed = 2.0f;

    // ---- ここから下は Play 中に Inspector で増えていくのを見るためのもの ----

    // Update が呼ばれた回数。
    public int Frames = 0;

    // 累計経過秒。
    public float Elapsed = 0.0f;

    // 位置の書き込みに成功した回数。
    public int MoveOk = 0;

    // 直近の書き込み結果。0 が成功。0 以外なら Runtime API が失敗している。
    public int LastStatus = -1;

    public override void Awake()
    {
        Frames = 0;
        Elapsed = 0.0f;
        MoveOk = 0;
        LastStatus = -1;
    }

    public override void Update(float deltaTime)
    {
        Frames += 1;
        Elapsed += deltaTime;

        // 絶対位置で書く。読み取り結果には依存しない。
        var status = Runtime.SetLocalPosition(GameObject, new Vector3(
            Elapsed * Speed,
            1.0f,
            0.0f));

        LastStatus = (int)status;
        if (status == RuntimeStatus.Ok) MoveOk += 1;

        // 回転も付ける。位置が他に打ち消されても回っていれば分かる。
        Runtime.SetLocalRotationEuler(GameObject, new Vector3(
            0.0f,
            Elapsed * 90.0f,
            0.0f));
    }
}
