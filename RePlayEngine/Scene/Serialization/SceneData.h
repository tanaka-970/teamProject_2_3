#pragma once

#include "../../Core/ObjectID/ObjectID.h"
#include "../../Object/Component/ComponentTypeID.h"
#include "../../Reflection/Property/PropertyBag.h"

#include <DirectXMath.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ReplayEngine::Scene
{
    class Scene;
}

namespace ReplayEngine::Core
{
    class GameObject;
}

namespace ReplayEngine::Scene::Serialization
{
    // Scene とファイルの間に挟む中間データ。
    //
    // なぜ中間層を置くか:
    //   1. Scene クラスへファイル入出力を持ち込まないため。
    //      Scene の責任は GameObject の所有と実行時管理だけに保つ。
    //   2. 将来 JSON 出力を足すとき、SceneData から先を差し替えるだけで済むため。
    //      Scene -> SceneData の変換はそのまま使い回せる。
    //   3. Play Mode の複製に使えるため。
    //      編集 Scene から SceneData を取り、実行用 Scene へ流し込めば、
    //      Play 中の変更が編集 Scene へ戻らない。
    //   4. 保存中にワーカースレッドが Scene を触らないようにできるため。
    //      メインスレッドで SceneData を作り、書き出しだけを後回しにできる形にしてある。
    //      （今回は書き出しも同期実行。並列化はしていない。）
    //
    // SceneData は素のデータだけを持ち、GPU リソースも生ポインタも保持しない。

    struct ComponentData
    {
        // 保存の主キー。ComponentRegistry へ登録した型名と一致する。
        std::string type_name;

        // 読み込み時に type_name から引き直す補助キー。ファイルには書かない。
        Core::ComponentTypeID type_id = Core::invalid_component_type_id;

        bool enabled = true;

        Reflection::PropertyBag properties;
    };

    struct GameObjectData
    {
        Core::ObjectID id;

        // 無効 ID なら Scene 直下。
        Core::ObjectID parent_id;

        std::string name{ "GameObject" };
        bool enabled = true;

        // ローカル値。回転はラジアンのオイラー角（Transform の内部表現と同じ）。
        DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 rotation{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 scale{ 1.0f, 1.0f, 1.0f };

        std::vector<ComponentData> components;
    };

    struct SceneData
    {
        // 現在のファイル形式バージョン。
        // v1〜v6 は旧 SceneDocument 形式で、互換性を持たせない方針。
        // 将来 v10 へ上げる余地を残すため、読み込み側はバージョン判定を必ず通す。
        // v8 で「操作対象 ObjectID」を追加した。v7 も読める（操作対象なしとして扱う）。
        // v9 で「Collision Backend Mode」と「旧 Stage の移行済み集合」を追加した。
        //
        // 【旧 Player 移行状態について】
        //   v8 / v9 の SCENE_STATE 行には「旧 Player の移行が済んでいるか」という
        //   項目が並んでいた。旧 Player 経路そのものを撤去したため、この項目は
        //   意味を失ったので SceneData から削除した。
        //   既に書き出されたファイルを読めなくしないよう、SCENE_STATE の読み取りは
        //   行単位で行い、余分な項目が並んでいても読み飛ばす。
        //   書き出しは操作対象 ObjectID だけになるので、v9 のまま項目が減る。
        //
        // v8 以前をどう扱うか:
        //   v7 … 操作対象なし・Backend Mode なしとして読む
        //   v8 … 操作対象は読む。Backend Mode は既定値 Hybrid とする
        //   どちらも読み込みは成功し、保存すると v9 になる。
        //
        //   Backend Mode の既定を Hybrid にする理由:
        //   既存 Scene は旧 Stage の衝突で動いていた。ここで
        //   Scene Colliders Only を既定にすると、開いた瞬間に床が消える。
        //   Hybrid なら「MeshCollider があればそれを使い、無ければ旧 Stage」
        //   となり、開く前と同じ挙動から始められる。
        static constexpr int current_version = 9;
        static constexpr int minimum_supported_version = 7;

        int version = current_version;
        std::string scene_name{ "Scene" };
        std::vector<GameObjectData> objects;

        // ---- Scene 単位の状態 (v8 以降) ------------------------------------
        //
        // 操作対象。Component の有無ではなく、この ID が操作対象を決める。
        // 無効なら「この Scene には操作対象が設定されていない」という状態であり、
        // 何かを自動生成したり別の GameObject を自動で選んだりはしない。
        Core::ObjectID controlled_object;

        // ---- Scene 単位の状態 (v9 以降) ------------------------------------
        //
        // 問い合わせ先の構成。0=Legacy Only / 1=Hybrid / 2=Scene Colliders Only。
        // Component へ重複して保存しない。Scene に 1 つだけ持つ。
        int collision_backend_mode = 1;

        // 旧 Stage の衝突から MeshCollider へ移行済みの「移行元 ID」。
        // 単一 bool ではないので、一部だけ移行した状態も表せる。
        std::vector<std::uint64_t> migrated_legacy_sources;

        void Clear()
        {
            version = current_version;
            scene_name = "Scene";
            objects.clear();
            controlled_object = Core::ObjectID::Invalid();
            collision_backend_mode = 1;
            migrated_legacy_sources.clear();
        }
    };

    // 読み込み時に起きた「致命的ではない問題」の報告。
    //
    // 未登録の Component、未知のプロパティ、存在しない親などは
    // Scene 全体の読み込みを止めず、ここへ記録して続行する。
    struct SceneLoadReport
    {
        std::vector<std::string> warnings;
        int skipped_components = 0;   // 未登録で生成できなかった Component
        int repaired_parents = 0;     // 親が見つからず Scene 直下へ寄せた GameObject
        int repaired_ids = 0;         // ID が重複して採番し直した GameObject
        int unknown_properties = 0;   // 定義が見つからず無視したプロパティ

        bool Clean() const noexcept { return warnings.empty(); }
        void Clear() noexcept
        {
            warnings.clear();
            skipped_components = 0;
            repaired_parents = 0;
            repaired_ids = 0;
            unknown_properties = 0;
        }
    };

    // Scene の現在の状態を SceneData へ写し取る（メインスレッドで実行すること）。
    //
    // 削除予約中の GameObject / Component は保存しない。
    // ComponentRegistry で serializable=false の型も保存しない
    // （TransformComponent は GameObject 側の transform として保存済みのため）。
    void CaptureScene(const Scene& scene, SceneData& output);

    // SceneData の内容で Scene を作り直す（メインスレッドで実行すること）。
    //
    // 手順:
    //   1. Scene を読み込み中にして更新を止める
    //   2. 既存の GameObject を全消去
    //   3. 保存された ObjectID のまま全 GameObject を生成
    //   4. 保存 ID から実 ID への対応表を作る（重複で採番し直した場合に備える）
    //   5. 親子関係を復元する
    //   6. ComponentRegistry で Component を生成する（ここで OnAttach が呼ばれる）
    //   7. PropertyRegistry でプロパティを反映する
    //   8. 読み込み中を解除する
    //
    // OnStart / OnEnable はここでは呼ばれない。呼び出し側が Scene::Start() を呼ぶ。
    // 読み込みが途中で中断されることはなく、常に true を返す。
    // 問題があった箇所は report へ記録される。
    bool ApplySceneData(const SceneData& data, Scene& scene, SceneLoadReport& report);

    // 指定した GameObject とその子孫だけを SceneData へ写し取る。
    //
    // 起点の GameObject は親なし（Scene 直下）として書き出すため、
    // どの階層のオブジェクトを渡しても独立した部分木になる。Prefab 保存に使う。
    // root が見つからない場合は false を返し、output は空のまま。
    bool CaptureGameObjectSubtree(const Scene& scene, Core::ObjectID root, SceneData& output);

    // SceneData の内容を「既存の Scene へ追加」する。
    //
    // ApplySceneData との違い:
    //   ApplySceneData       … Scene を消してから、保存 ObjectID のまま復元する（Scene 読み込み）
    //   InstantiateSceneData … Scene を消さずに追加し、ObjectID は必ず採番し直す（Prefab 配置）
    //
    // ID を振り直すのは、同じ Prefab を 2 回配置したときに衝突させないため。
    // 戻り値は最初に見つかった親なし GameObject（部分木の起点）。何も作れなければ nullptr。
    Core::GameObject* InstantiateSceneData(const SceneData& data, Scene& scene,
        SceneLoadReport& report);

    // GameObject を複製する。
    //
    // 複製先には必ず新しい ObjectID が振られる。保存 ID をそのまま持ち回らない。
    // Component は ComponentRegistry で作り直し、値は PropertyRegistry で写す。
    // 型ごとの複製処理を書かずに済むので、Component が増えてもここは変更不要。
    //
    // include_children が true なら子孫も再帰的に複製し、階層構造を保つ。
    // 失敗した場合は nullptr を返す。
    Core::GameObject* DuplicateGameObject(Scene& scene,
        const Core::GameObject& source, bool include_children);
}
