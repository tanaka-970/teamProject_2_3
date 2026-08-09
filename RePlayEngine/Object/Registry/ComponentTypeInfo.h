#pragma once

#include "../Component/ComponentTypeID.h"
#include "../../Reflection/Registry/TypeGUID.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ReplayEngine::Core
{
    class Component;

    // ComponentRegistry へ 1 つの Component 型を登録するときの情報。
    //
    // ここへ登録した内容が、そのまま次のすべてへ反映される。
    // 型ごとの if / switch を Editor や Serializer 側へ書き足さないための単一の定義点。
    //   - Editor の Add Component 一覧（カテゴリ分けと検索）
    //   - Scene 読み込み時の生成
    //   - Scene 保存時の型名
    //   - Inspector のヘッダ表示と削除ボタンの有効・無効
    //   - GameObject の複製
    struct ComponentTypeInfo
    {
        using Factory = std::function<std::unique_ptr<Component>()>;

        // 保存に使う正式な型名。C++ のクラス名と一致させる（例 "RotatorComponent"）。
        // これが Scene ファイルの主キーになるので、一度決めたら安易に変えない。
        std::string type_name;

        // 型名から計算した 32bit ID。実行時の検索用。
        ComponentTypeID type_id = invalid_component_type_id;

        // ---- 永続的な型 ID (v11 で追加) --------------------------------------
        //
        // type_name / type_id との役割の違い:
        //   type_name … 人が読める名前。Scene ファイルの互換のため今も主キー。
        //   type_id   … type_name から計算する 32bit。実行時の検索専用。
        //   type_guid … 型ごとに人が 1 回だけ決める 128bit。クラス名を変えても不変。
        //
        // Engine 組み込み Component へ一斉に GUID を振る必要はない。
        // 未設定 (Invalid) なら、これまで通り type_name が主キーとして働く。
        // Behaviour と、将来の C# Script 型では必須にする。
        //
        // GUID が設定されている型は、読み込み時に
        //   type_guid -> alias_guids -> type_name
        // の順で解決される。クラス名を変えても Scene の参照が切れない。
        Reflection::TypeGUID type_guid;

        // 旧 GUID。型を統合・移行したときに、古い Scene から引き続き解決するために使う。
        // 空でよい。ここへ並べた GUID はどれも type_guid の型へ解決される。
        std::vector<Reflection::TypeGUID> alias_guids;

        // どのモジュールが提供する型か（"RePlayEngine.BuiltIn" / "Game.Behaviours" など）。
        // Missing Component になったとき「どのモジュールが欠けているか」を表示するために保存する。
        std::string module_id;

        // 型のデータ形式のバージョン。プロパティ構成を変えたときに上げる。
        // 保存データ側にも書き出すので、将来ここを見て移行処理を分岐できる。
        int type_version = 1;

        // 旧プロパティ名 -> 現プロパティ名 の対応。
        // Field を Rename しても保存済みの値を失わないために使う。
        // first が保存ファイル側の名前、second が現在の登録名。
        std::vector<std::pair<std::string, std::string>> property_aliases;

        // Editor に出す読みやすい名前（例 "Rotator"）。空なら type_name を使う。
        std::string display_name;

        // Add Component のカテゴリ（例 "Core" / "Rendering" / "Physics" / "Gameplay"）。
        std::string category{ "Gameplay" };

        // Inspector のヘッダに出す補足説明。空でよい。
        std::string tooltip;

        // 同じ GameObject へ同じ型を複数付けられるか。
        //   false … Transform, PlayerController のように 1 つだけ意味を持つもの
        //   true  … Collider, AudioSource のように複数あって自然なもの
        bool allow_multiple = false;

        // Component 間の関係。Add Component / Inspector が共通で使う。
        // required_components は成立に必須、recommended_components は通常用途で推奨。
        std::vector<ComponentTypeID> required_components;
        std::vector<ComponentTypeID> recommended_components;

        // Editor の Add Component 一覧へ出すか。内部用の型を隠したいときに false。
        bool editor_visible = true;

        // Scene ファイルへ保存するか。
        // TransformComponent のように「GameObject 側に保存済みで二重になる」型は false。
        bool serializable = true;

        // Inspector から削除できるか。false なら削除ボタンを無効化する。
        bool removable = true;

        // GameObject を作った時点で自動的に付くか。TransformComponent 用。
        bool built_in = false;

        // Runtime World に実体化するか。
        // false は Scene/Prefab には保存するが、RuntimeSceneService が構築する
        // World では生成しない Editor Annotation 等に使う。
        bool runtime_available = true;

        // 実体を作る関数。owner への結線は GameObject 側が行うので、ここでは生成だけ。
        Factory factory;

        const std::string& DisplayName() const noexcept
        {
            return display_name.empty() ? type_name : display_name;
        }

        // ---- 登録時の記述を短くするための連結設定 --------------------------
        //
        // C++20 の指定イニシャライザ ({ .display_name = "..." }) は使わない。
        // 本プロジェクトは /std:c++17 でビルドしており、MSVC の C++17 モードでは
        // 指定イニシャライザが通らないため。
        //
        //   ComponentRegistry::Register<RotatorComponent>(
        //       ComponentTypeInfo::Describe("Rotator", "Gameplay"));
        //
        //   ComponentRegistry::Register<TransformComponent>(
        //       ComponentTypeInfo::Describe("Transform", "Core")
        //           .AsBuiltIn().NotRemovable().NotSerializable());

        static ComponentTypeInfo Describe(std::string display, std::string category)
        {
            ComponentTypeInfo info;
            info.display_name = std::move(display);
            info.category = std::move(category);
            return info;
        }

        ComponentTypeInfo& WithTooltip(std::string value)
        {
            tooltip = std::move(value);
            return *this;
        }

        ComponentTypeInfo& AllowMultipleInstances()
        {
            allow_multiple = true;
            return *this;
        }

        template<class T>
        ComponentTypeInfo& Requires()
        {
            required_components.push_back(T::StaticTypeID());
            return *this;
        }

        template<class T>
        ComponentTypeInfo& Recommends()
        {
            recommended_components.push_back(T::StaticTypeID());
            return *this;
        }

        ComponentTypeInfo& HiddenInEditor()
        {
            editor_visible = false;
            return *this;
        }

        ComponentTypeInfo& NotSerializable()
        {
            serializable = false;
            return *this;
        }

        ComponentTypeInfo& NotRemovable()
        {
            removable = false;
            return *this;
        }

        ComponentTypeInfo& AsBuiltIn()
        {
            built_in = true;
            return *this;
        }

        ComponentTypeInfo& EditorOnly()
        {
            runtime_available = false;
            return *this;
        }

        // ---- v11 で追加した連結設定 ------------------------------------------

        // 永続的な型 GUID を設定する。
        //
        //   ComponentRegistry::Register<DoorBehaviour>(
        //       ComponentTypeInfo::Describe("Door", "Behaviours")
        //           .WithTypeGUID(Reflection::MakeTypeGUID("....32文字...."))
        //           .InModule("Game.Behaviours"));
        ComponentTypeInfo& WithTypeGUID(Reflection::TypeGUID guid)
        {
            type_guid = guid;
            return *this;
        }

        // 旧 GUID からの移行を受け付ける。
        ComponentTypeInfo& WithAliasGUID(Reflection::TypeGUID guid)
        {
            alias_guids.push_back(guid);
            return *this;
        }

        ComponentTypeInfo& InModule(std::string value)
        {
            module_id = std::move(value);
            return *this;
        }

        ComponentTypeInfo& WithVersion(int value)
        {
            type_version = value;
            return *this;
        }

        // 保存済みの旧プロパティ名を、現在の登録名へ読み替える。
        ComponentTypeInfo& WithPropertyAlias(std::string saved_name, std::string current_name)
        {
            property_aliases.emplace_back(std::move(saved_name), std::move(current_name));
            return *this;
        }
    };
}
