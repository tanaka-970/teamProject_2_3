using System;
using System.Collections;
using ReplayEngine;

namespace Game.Terraform;

// アタッチ先: Terraform.replayscene の Director。
// 担当: 人が操作せずにゲームを進める自動確認。
//
// TERRAFORM_DEMO=1 のときだけ動く。通常プレイでは何もしない。
// 目的は 2 つ:
//   1. 地形を C# から彫る動きを、撮影とヘッドレス実行で確かめる
//   2. 「盛れば届く」という設計が実際に成立するかを機械的に確かめる
[ReplayGuid("7d4e9fa146bc825ad3f7e9c01b6e2835")]
public class TerraformDemo : MonoBehaviour
{
    [ReadOnly] public bool Running;
    [ReadOnly] public int SculptCalls;
    [ReadOnly] public float HeightGain;

    [HideInInspector] public bool Finished;

    TerraformGame? director;
    TerraformSculptor? sculptor;
    TerraformWalker? walker;
    TerraformCamera? rig;

    static bool DemoRequested =>
        Environment.GetEnvironmentVariable("TERRAFORM_DEMO") == "1";

    void Awake()
    {
        director = GetComponent<TerraformGame>();

        var player = GameObject.Find("Player");
        if (player != null)
        {
            sculptor = player.GetComponent<TerraformSculptor>();
            walker = player.GetComponent<TerraformWalker>();
        }

        var rigObject = GameObject.Find("CameraRig");
        rig = rigObject != null ? rigObject.GetComponent<TerraformCamera>() : null;
    }

    void Start()
    {
        if (!DemoRequested) return;
        Running = true;
        StartCoroutine(RunDemo());
    }

    IEnumerator RunDemo()
    {
        // タイトルが出るまで待ってから始める。
        yield return new WaitForSeconds(0.6f);
        director?.BeginRunFromDemo();
        yield return new WaitForSeconds(0.3f);

        // 一番低いオーブの真下へ立ち、そこを盛って高さを稼ぐ。
        // 「届かない所へは地面を持ち上げて行く」という設計そのものを確かめる。
        var target = new Vector3(-20.0f, 0.0f, 14.0f);
        walker?.MoveTo(new Vector3(target.X, 0.0f, target.Z - 6.0f));
        rig?.SnapBehindTarget();
        yield return null;

        var before = sculptor != null ? sculptor.SampleGroundHeight(target) : 0.0f;

        // 1 回の呼び出しで確実に量が入るよう deltaTime を明示する。
        for (var step = 0; step < 90; ++step)
        {
            if (sculptor != null &&
                sculptor.SculptAt(target, SculptMode.Raise, 5.0f, 9.0f, 1.0f / 30.0f))
            {
                ++SculptCalls;
            }

            // 盛った地面へ自分も乗る。Walker が毎フレーム足元を取り直す。
            walker?.MoveTo(new Vector3(target.X, 0.0f, target.Z - 3.0f));
            if (rig != null) rig.Yaw += 1.6f;
            yield return null;
        }

        var after = sculptor != null ? sculptor.SampleGroundHeight(target) : 0.0f;
        HeightGain = after - before;

        Debug.Log($"Terraform demo: calls={SculptCalls} gain={HeightGain:0.00}", this);

        // 盛った頂上へ立たせる。撮影で「作った山の上にいる」絵になる。
        walker?.MoveTo(target);
        rig?.SnapBehindTarget();

        Running = false;
        Finished = true;
    }
}
