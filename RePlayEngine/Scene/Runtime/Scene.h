#pragma once

#include "../../Object/GameObject/GameObject.h"
#include "../Services/SceneServices.h"

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ReplayEngine::Scene
{
    // GameObject を所有・管理する単位。
    //
    // 既存の IScene / SceneManager は「起動ロゴ → ロード画面 → ゲーム」という
    // 画面遷移を担当するもので、この Scene とは責任が違う。両者は併存する。
    //   IScene / SceneManager … 画面の切り替え
    //   Scene (このクラス)     … GameObject の入れ物
    //
    // 所有関係:
    //   Scene       が GameObject を unique_ptr で所有する（唯一の所有者）
    //   GameObject  が Component  を unique_ptr で所有する
    //   親 GameObject は子を非所有の生ポインタで参照するだけ
    //
    // 遅延操作について:
    //   生成・削除の要求をコマンドキューへ積む方式は採らない。
    //   キューに積んだポインタが、実行前に破棄済みのオブジェクトを指す危険があるため。
    //   代わりに GameObject / Component が自分に予約フラグを立て、
    //   Scene が同期点で全体を走査して回収する。
    //   宙に浮いたポインタが構造的に発生しないぶん、こちらの方が安全性を保証しやすい。
    //
    // スレッドについて:
    //   現時点では全 Component をメインスレッドで順番に更新する。
    //   RePlayEngine には汎用の JobSystem / ThreadPool が存在しないため、
    //   新しく作らずメインスレッド実行に統一した。
    //   Component::GetThreadPolicy() の区分は将来の並列化に備えた宣言として保持している。
    class Scene final
    {
    public:
        Scene();
        explicit Scene(std::string name);
        ~Scene();

        // コピー・ムーブ禁止。
        // GameObject が Scene への生ポインタを持っているため、Scene の実体が動くと
        // その参照が壊れる。Scene の複製が必要な場合は SceneData 経由で行うこと
        // （Play Mode 開始時の複製がこれに当たる）。
        Scene(const Scene&) = delete;
        Scene& operator=(const Scene&) = delete;
        Scene(Scene&&) = delete;
        Scene& operator=(Scene&&) = delete;

        // ---- 基本情報 ------------------------------------------------------

        const std::string& Name() const noexcept { return name_; }
        void SetName(std::string name) { name_ = std::move(name); }

        // ---- GameObject の生成・削除 ---------------------------------------

        Core::GameObject* CreateGameObject(const std::string& name);

        // 保存されていた ObjectID を復元しながら生成する。Scene 読み込み専用。
        // 既に同じ ID が存在する場合は新しい ID を採番し直し、重複を防いだうえで生成する
        // （壊れたファイルでも読み込みを止めない）。
        Core::GameObject* CreateGameObjectWithID(Core::ObjectID id, const std::string& name);

        // 削除を予約する。子も再帰的に予約される。
        // 実際の破棄は ProcessPendingOperations() まで起きない。
        void DestroyGameObject(Core::GameObject* object) noexcept;
        void DestroyGameObject(Core::ObjectID id) noexcept;

        // 全 GameObject を即座に破棄する。Update 中に呼ばないこと。
        // SetParent 経由でコンテナ操作を行うため noexcept は付けない。
        void Clear();

        // ---- 検索 ----------------------------------------------------------

        Core::GameObject* FindGameObjectByID(Core::ObjectID id) const noexcept;

        // 同名が複数ある場合は最初に見つかったものを返す。
        Core::GameObject* FindGameObjectByName(const std::string& name) const noexcept;

        std::size_t GameObjectCount() const noexcept { return objects_.size(); }
        Core::GameObject* GameObjectAt(std::size_t index) const noexcept;

        // 親を持たない GameObject。Hierarchy の描画開始点として使う。
        std::vector<Core::GameObject*> RootGameObjects() const;

        // ---- 更新 ----------------------------------------------------------

        // Scene を開始する。以降 OnStart が呼ばれるようになる。
        void Start();
        bool Started() const noexcept { return started_; }

        void Update(float delta_time);
        void FixedUpdate(float fixed_delta_time);
        void LateUpdate(float delta_time);

        // 生成・削除の予約をまとめて反映する。
        // Update / FixedUpdate / LateUpdate の末尾で自動的に呼ばれるので、
        // 通常は外から呼ぶ必要はない。Editor 操作の直後に即時反映したい場合だけ使う。
        void ProcessPendingOperations();

        // ---- 読み込み中の保護 ----------------------------------------------
        //
        // 読み込み処理は BeginLoad() / EndLoad() で挟む。
        // この間は Update / FixedUpdate / LateUpdate が何もしないので、
        // 途中まで構築された Scene が更新されることがない。

        void BeginLoad() noexcept { loading_ = true; }
        void EndLoad() noexcept { loading_ = false; }
        bool Loading() const noexcept { return loading_; }

        // ---- 外部サービス --------------------------------------------------
        //
        // カメラの向き・地形問い合わせ・操作対象 ObjectID を Component へ渡す窓口。
        // 実体は framework が所有し、ここは非所有参照だけを持つ。
        SceneServices& Services() noexcept { return services_; }
        const SceneServices& Services() const noexcept { return services_; }

        Core::ObjectIDGenerator& IDGenerator() noexcept { return id_generator_; }
        const Core::ObjectIDGenerator& IDGenerator() const noexcept { return id_generator_; }

    private:
        // 全 Component の OnEnable / OnDisable / OnStart を必要に応じて呼ぶ。
        void SynchronizeStates();

        void RemoveFromLookup(const Core::GameObject* object) noexcept;

        std::string name_{ "Scene" };

        // GameObject の唯一の所有者。unique_ptr なので要素の並べ替えや追加で
        // GameObject 本体のアドレスは動かない。生ポインタで参照して安全。
        std::vector<std::unique_ptr<Core::GameObject>> objects_;

        // ObjectID からの検索表。objects_ と常に同期させる。
        std::unordered_map<Core::ObjectID, Core::GameObject*> id_lookup_;

        Core::ObjectIDGenerator id_generator_;

        SceneServices services_;

        bool started_ = false;
        bool loading_ = false;

        // Update 中に Clear() などの破壊的操作が呼ばれるのを検出するための番兵。
        bool updating_ = false;
    };
}
