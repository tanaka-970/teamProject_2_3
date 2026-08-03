#pragma once

#include "ComponentTypeInfo.h"
#include "../../Core/ObjectID/RuntimeIdentity.h"

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace ReplayEngine::Core
{
    class Component;
    class GameObject;

    // Component 型の登録簿。型名 / 型 ID から Component を生成できるようにする。
    //
    // 静的関数として公開している理由:
    //   型のメタ情報は本質的にプロセス全体で 1 つしかない不変の表であり、
    //   Scene や Editor ごとに別々の内容を持つ意味がない。
    //   可変の状態を抱えるサービス的な Singleton とは性質が違うため、
    //   ここだけは静的な表として扱う。実体は関数ローカル static なので、
    //   翻訳単位をまたぐ静的初期化順序の問題も起きない。
    //
    // 登録は静的初期化に頼らず、RegisterBuiltInComponents() から明示的に行う。
    // 静的オブジェクトの初期化順に依存しないぶん、動作を追いやすい。
    class ComponentRegistry final
    {
    public:
        ComponentRegistry() = delete;

        // ---- 登録 ----------------------------------------------------------

        // 型 T を登録する。type_name / type_id / factory は T から自動で埋める。
        // 呼び出し側は表示名・カテゴリ・重複可否などの意味情報だけを指定する。
        //
        //   ComponentRegistry::Register<RotatorComponent>(
        //       { .display_name = "Rotator", .category = "Gameplay" });
        template<class T>
        static bool Register(ComponentTypeInfo info)
        {
            static_assert(std::is_base_of_v<Component, T>,
                "T must derive from ReplayEngine::Core::Component");

            info.type_name = T::StaticTypeName();
            info.type_id = T::StaticTypeID();
            info.factory = []() -> std::unique_ptr<Component>
            {
                return std::make_unique<T>();
            };
            return RegisterInfo(std::move(info));
        }

        // 組み立て済みの情報をそのまま登録する。
        // 既に同じ型 ID が登録済みなら false を返して何もしない（上書きしない）。
        static bool RegisterInfo(ComponentTypeInfo info);

        static void Clear() noexcept;

        // ---- 検索 ----------------------------------------------------------

        static const ComponentTypeInfo* Find(ComponentTypeID type_id) noexcept;
        static const ComponentTypeInfo* Find(const std::string& type_name) noexcept;

        // 永続 GUID から引く。type_guid で見つからなければ alias_guids も見る。
        // 無効 GUID を渡された場合は nullptr。
        static const ComponentTypeInfo* Find(Reflection::TypeGUID type_guid) noexcept;

        // 保存データから型を解決する唯一の入口。
        //
        // 優先順位:
        //   1. type_guid       … 型名を変えても壊れない主キー
        //   2. alias_guids     … 統合・移行された旧 GUID
        //   3. type_name       … GUID を持たない既存 Component 用の従来経路
        //
        // どれにも当たらなければ nullptr。呼び出し側は Missing Component として
        // データを保持すること。決して読み飛ばさない。
        static const ComponentTypeInfo* Resolve(Reflection::TypeGUID type_guid,
            const std::string& type_name) noexcept;

        // ---- GUID の重複検出 -----------------------------------------------
        //
        // 登録時に既存と同じ GUID を使っていた場合、その登録は拒否され、
        // 内容がここへ記録される。起動時 / Validation で表示する。
        // 黙って上書きすると、Scene の参照が別の型へ化ける。
        static const std::vector<std::string>& TypeGUIDConflicts() noexcept;

        static bool IsRegistered(ComponentTypeID type_id) noexcept
        {
            return Find(type_id) != nullptr;
        }

        // 登録順のまま全件返す。Editor 側でカテゴリ分けと絞り込みを行う。
        static const std::vector<ComponentTypeInfo>& All() noexcept;

        // カテゴリの一覧を登録順の重複なしで返す。Add Component パネル用。
        static std::vector<std::string> Categories();

        // ---- 生成 ----------------------------------------------------------

        // owner へ結線しないただの実体生成。未登録なら nullptr。
        static std::unique_ptr<Component> Instantiate(ComponentTypeID type_id);
        static std::unique_ptr<Component> Instantiate(const std::string& type_name);

        // 生成して owner へ結線するところまで行う。
        // 未登録、または重複禁止の型が既にある場合は nullptr を返す（クラッシュしない）。
        static Component* Create(ComponentTypeID type_id, GameObject& owner);
        static Component* Create(const std::string& type_name, GameObject& owner);

        // 保存されていた ComponentStableID を保ったまま生成して結線する。
        // Scene / Prefab の読み込みと複製が使う。
        //
        // stable_id が 0 や重複の場合は GameObject 側が採番し直すため、
        // 壊れたファイルでも読み込みは止まらない。
        static Component* CreateWithStableID(ComponentTypeID type_id, GameObject& owner,
            ComponentStableID stable_id);

        // ---- 問い合わせ ----------------------------------------------------
        // 未登録の型 ID を渡された場合は、安全側（追加を許さない・削除できる・保存する）へ倒す。

        static bool AllowsMultiple(ComponentTypeID type_id) noexcept;
        static bool IsRemovable(ComponentTypeID type_id) noexcept;
        static bool IsSerializable(ComponentTypeID type_id) noexcept;
        static bool IsEditorVisible(ComponentTypeID type_id) noexcept;

        // 表示名。未登録なら "(未登録: 12345678)" のような文字列を返す。
        static std::string DisplayNameOf(ComponentTypeID type_id);
    };
}
