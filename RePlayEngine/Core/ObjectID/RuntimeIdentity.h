#pragma once

#include <cstdint>

namespace ReplayEngine::Core
{
    // Runtime の「安全な参照」を組み立てるための素の識別子。
    //
    // ここには合成型 (ObjectHandle / ComponentHandle) を置かない。
    // Scene / GameObject / Component はこのヘッダだけを見れば済むようにして、
    // Core 層から Runtime 層への依存が生まれないようにするため。
    // 合成型は Runtime/Handles/RuntimeHandles.h にある。
    //
    // どれも整数の別名にとどめてある。将来 C ABI / C# へそのまま渡せるようにするため、
    // クラスにして不変条件を持たせることはしない。

    // ---- World -----------------------------------------------------------
    //
    // Runtime World の「実体」を識別する番号。プロセス内で単調増加し、再利用しない。
    //
    // 何のためにあるか:
    //   Scene を読み直すと ObjectID は同じ値が復元される。
    //   ObjectID だけを持った古い参照は、読み直したあとの別物を指してしまう。
    //   World 側の番号を一緒に持たせておけば、Scene 切り替え・再読み込みをまたいだ
    //   参照を「別の World のもの」として確実に弾ける。
    using WorldInstanceID = std::uint64_t;

    inline constexpr WorldInstanceID invalid_world_instance_id = 0;

    // 新しい World 実体番号を採番する。Scene が自分の実体を作り直すたびに呼ぶ。
    WorldInstanceID AcquireWorldInstanceID() noexcept;

    // ---- GameObject ------------------------------------------------------
    //
    // 同じ ObjectID が作り直されたことを見分ける世代番号。1 から始まる。
    //
    // 同一 World の中で ObjectID が再利用される経路が実在する:
    //   CreateGameObjectWithID() は保存されていた ID をそのまま復元するため、
    //   一度破棄した ID と同じ値を後からもう一度使うことがありうる。
    //   世代番号を突き合わせれば、破棄前に取った古い Handle を確実に無効にできる。
    using ObjectGeneration = std::uint32_t;

    inline constexpr ObjectGeneration invalid_object_generation = 0;

    // ---- Component -------------------------------------------------------
    //
    // World 内で Component の実体へ一度だけ振られる通し番号。1 から始まり、絶対に再利用しない。
    //
    // 再利用しないので、これ自体が世代番号を兼ねる。
    // 「Component を消して同じ場所へ作り直した」場合も新しい番号になるため、
    // 古い ComponentHandle は自動的に解決に失敗する。
    // 実行時専用。ファイルへは保存しない。
    using ComponentInstanceID = std::uint64_t;

    inline constexpr ComponentInstanceID invalid_component_instance_id = 0;

    // GameObject の中で安定する Component の保存用 ID。1 から始まる。
    //
    // ComponentInstanceID との違い:
    //   ComponentInstanceID … 実行時専用。World 単位。保存しない。
    //   ComponentStableID   … 保存する。GameObject 単位。Scene / Prefab をまたいで同じ値。
    //
    // GameObject 単位にしてあるのは、Prefab を配置したときに
    // 付け替えが必要なのが「所有 GameObject の ObjectID だけ」で済むようにするため。
    // Scene 全体で一意にすると、配置のたびに Component 側も振り直す必要が出てしまう。
    //
    // 並び順に依存しないので、Inspector で Component を並べ替えても参照が壊れない。
    using ComponentStableID = std::uint32_t;

    inline constexpr ComponentStableID invalid_component_stable_id = 0;
}
