#pragma once

namespace ReplayEngine::Core { class Component; }
namespace ReplayEngine::Reflection { class PropertyDesc; }
namespace ReplayEngine::Scene { class Scene; }
namespace ReplayEngine::Assets { class AssetDatabase; }

namespace ReplayEngine::Editor
{
    // PropertyRegistry の定義 1 件から ImGui の入力欄を作る。
    //
    // ここに Component 型ごとの分岐は一切書かない。
    // 型ごとの見た目は PropertyType と PropertyDesc のメタ情報だけで決まる。
    // 新しい Component を足しても、このファイルは変更不要。
    //
    // 描画のみを担当し、Undo 履歴や Dirty 管理には関与しない。
    // 値が変わったかどうかを戻り値で返すので、呼び出し側（InspectorPanel）が
    // EditorContext のトランザクションと結び付ける。
    class PropertyDrawer final
    {
    public:
        PropertyDrawer() = delete;

        // 1 件描く。値が変更されたら true。
        // assets が null なら Asset 選択は手入力のみになる。
        // scene が null なら ObjectID 参照は数値入力のみになる。
        static bool Draw(const Reflection::PropertyDesc& desc,
            Core::Component& component,
            const Assets::AssetDatabase* assets,
            const Scene::Scene* scene);

        // Component に登録された全プロパティを順に描く。
        // いずれか 1 つでも変更されたら true。
        static bool DrawAll(Core::Component& component,
            const Assets::AssetDatabase* assets,
            const Scene::Scene* scene);
    };
}
