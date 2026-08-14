#pragma once

#include "../../Core/ObjectID/ObjectID.h"
#include "../../Core/ObjectID/RuntimeIdentity.h"
#include "../../Object/Component/ComponentTypeID.h"
#include "../../Reflection/Property/PropertyBag.h"
#include "../../Reflection/Registry/TypeGUID.h"

#include <DirectXMath.h>

#include <cstdint>
#include <string>
#include <unordered_map>
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
    // SceneData.cpp の保存データ用参照付け替えと同じ規則を、
    // Scene 遷移で生きた Persistent 階層へ適用するための対応表。
    // 型をここへ出して、Scene.cpp が別の参照付け替え規則を持たないようにする。
    using ObjectRemap = std::unordered_map<Core::ObjectID, Core::GameObject*>;

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
        // 人が読める型名。v10 まではこれが唯一の主キーだった。
        // v11 以降も、GUID を持たない既存 Component 用に読み書きを続ける。
        std::string type_name;

        // 読み込み時に type_name から引き直す補助キー。ファイルには書かない。
        Core::ComponentTypeID type_id = Core::invalid_component_type_id;

        bool enabled = true;

        // ---- v11 で追加 ---------------------------------------------------

        // 永続的な型 ID。設定されていれば type_name より優先して解決する。
        // クラス名・名前空間・ファイル位置を変えても参照が切れない。
        Reflection::TypeGUID type_guid;

        // 提供元モジュール。型が見つからないときに何が欠けているか示すために保存する。
        std::string module_id;

        // 型のデータ形式バージョン。0 なら未記録（v10 以前のファイル）。
        int type_version = 0;

        // 所有 GameObject の中で安定した Component の ID。
        // ComponentReference の解決先。並び替えでも型名変更でも壊れない。
        // 0 なら未記録で、読み込み側が採番し直す。
        Core::ComponentStableID stable_id = Core::invalid_component_stable_id;

        // プロパティ。型が解決できなかった場合も、ここへ読み込んだまま保持する
        // （MissingComponent が丸ごと預かる）。
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

        // v10: Prefab instance identity. Empty source means ordinary GameObject.
        // local_id is stable inside the Prefab asset; instance_root is a Scene ObjectID.
        std::string prefab_source_guid;
        std::uint64_t prefab_local_id = 0;
        Core::ObjectID prefab_instance_root;

        std::vector<ComponentData> components;
    };

    struct SceneData
    {
        // 現在のファイル形式バージョン。
        // v1〜v6は構造が異なるため読み込み対象外。
        // 将来 v10 へ上げる余地を残すため、読み込み側はバージョン判定を必ず通す。
        // v8 で「操作対象 ObjectID」を追加した。v7 も読める（操作対象なしとして扱う）。
        // v9 で追加された COLLISION_STATE は v10 でも予約行として読み書きする。
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
        //   v7 … 操作対象なしとして読む
        //   v8 … 操作対象を読む
        //   どちらも読み込みは成功し、保存すると現行バージョンになる。
        //
        // 【v11 で追加したもの】
        //   COMPONENT ブロックへ次の行が増えた。どれも省略可能な追加行なので、
        //   v10 以前のファイルは今まで通りそのまま読める。
        //     STABLE_ID    … GameObject 内で安定した Component の ID
        //     TYPE_GUID    … 型名を変えても壊れない永続的な型 ID
        //     TYPE_MODULE  … 提供元モジュール
        //     TYPE_VERSION … 型のデータ形式バージョン
        //
        //   PROPERTY へ次の型が増えた。
        //     int64 / uint64 / assetref / sceneref / compref / array
        //
        // 【v10 を書き換えなかった理由】
        //   同じバージョン番号のまま意味を変えると、既に v10 として保存された
        //   ファイルが「読めるが中身の解釈が違う」状態になる。
        //   バージョンを上げれば、読み手は必ず分岐を通る。
        //
        // 既存 Scene は開いただけでは変換しない。保存したときに初めて v11 になる。
        static constexpr int current_version = 11;
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

        void Clear()
        {
            version = current_version;
            scene_name = "Scene";
            objects.clear();
            controlled_object = Core::ObjectID::Invalid();
        }
    };

    // 読み込み時に起きた「致命的ではない問題」の報告。
    //
    // 未登録の Component、未知のプロパティ、存在しない親などは
    // Scene 全体の読み込みを止めず、ここへ記録して続行する。
    struct SceneLoadReport
    {
        std::vector<std::string> warnings;
        int skipped_components = 0;   // 生成そのものに失敗した Component
        int repaired_parents = 0;     // 親が見つからず Scene 直下へ寄せた GameObject
        int repaired_ids = 0;         // ID が重複して採番し直した GameObject
        int unknown_properties = 0;   // 型が知らなかったプロパティ（捨てずに保持している）

        // v11 で追加。
        //
        // 型が見つからず MissingComponent として保持した数。
        // skipped_components と分けているのは意味がまったく違うため。
        //   missing_components … データは保持されている。保存し直しても失われない。
        //   skipped_components … 生成に失敗した。復元できていない。
        int missing_components = 0;

        // 採番し直した ComponentStableID の数（保存値が 0 または重複していた）。
        int repaired_component_ids = 0;

        // required_components に従って保存データ外から補った Component 数と、
        // 補えずに残った依存辺の数。欠落していても Scene 読み込みは継続する。
        int automatically_added_components = 0;
        int unresolved_component_dependencies = 0;

        bool Clean() const noexcept { return warnings.empty(); }
        void Clear() noexcept
        {
            warnings.clear();
            skipped_components = 0;
            repaired_parents = 0;
            repaired_ids = 0;
            unknown_properties = 0;
            missing_components = 0;
            repaired_component_ids = 0;
            automatically_added_components = 0;
            unresolved_component_dependencies = 0;
        }
    };

    // Scene の現在の状態を SceneData へ写し取る（メインスレッドで実行すること）。
    //
    // 削除予約中の GameObject / Component は保存しない。
    // ComponentRegistry で serializable=false の型も保存しない
    // （TransformComponent は GameObject 側の transform として保存済みのため）。
    void CaptureScene(const Scene& scene, SceneData& output);

    // 生きている GameObject が持つ ObjectID 参照を付け替える。
    // Scene 遷移で Persistent 階層を新しい Scene の ID 空間へ移すときに使う。
    void RemapLiveObjectReferences(const std::vector<Core::GameObject*>& objects,
        const ObjectRemap& remap);

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
        SceneLoadReport& report, const std::string& prefab_source_guid = {});

    // Prefab asset dataを既存instanceへ戻す。local IDが一致するObjectIDは維持し、
    // 追加・削除された子やComponentもasset状態へ同期する。
    bool ApplyPrefabInstanceData(const SceneData& data, Scene& scene,
        Core::ObjectID instance_root, const std::string& prefab_source_guid,
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
