#pragma once

#include "../Component/ComponentTypeID.h"

#include <functional>
#include <memory>
#include <string>

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

        // Editor の Add Component 一覧へ出すか。内部用の型を隠したいときに false。
        bool editor_visible = true;

        // Scene ファイルへ保存するか。
        // TransformComponent のように「GameObject 側に保存済みで二重になる」型は false。
        bool serializable = true;

        // Inspector から削除できるか。false なら削除ボタンを無効化する。
        bool removable = true;

        // GameObject を作った時点で自動的に付くか。TransformComponent 用。
        bool built_in = false;

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
    };
}
