#pragma once

namespace ReplayEngine::Core
{
    // Component をどのスレッドで更新してよいかの区分。
    //
    // 現状の RePlayEngine には ThreadPool / JobSystem に相当する汎用基盤が存在しない。
    // 実在するのは AsyncAssetManager（ファイル読み込み専用のワーカー 1 本）と
    // LoadingScene の std::async だけで、いずれも汎用ジョブを受け付けない。
    // そのため今回の実装では「全 Component をメインスレッドで更新する」。
    //
    // ここで区分だけ先に定義しておく理由は、将来並列化する余地を構造として残すため。
    // Component 側は自分がどちらに属するかを宣言できるが、Scene は現時点では
    // 区分にかかわらず順番にメインスレッドで実行する。
    enum class ThreadPolicy
    {
        // メインスレッドでのみ実行してよい。既定値。
        //   - Editor UI からの参照がある
        //   - Scene 構造（GameObject / Component / 親子）を変更する
        //   - D3D11 の DeviceContext に触れる
        //   - 他の Component や共有状態へ書き込む
        MainThreadOnly,

        // ワーカースレッドから呼んでも安全。
        //   - 自分の GameObject 以外へ書き込まない
        //   - Scene 構造を直接変更しない（変更は Scene の遅延操作キューへ積む）
        //   - D3D11 に触れない
        //   - 他 Component の状態を読むだけで書き換えない
        ParallelSafe,
    };

    inline const char* ToString(ThreadPolicy policy) noexcept
    {
        switch (policy)
        {
        case ThreadPolicy::ParallelSafe:   return "ParallelSafe";
        case ThreadPolicy::MainThreadOnly: return "MainThreadOnly";
        }
        return "MainThreadOnly";
    }
}
