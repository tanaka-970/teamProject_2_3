#pragma once

#include "MaterialAsset.h"
#include "../Shaders/ShaderCatalog.h"

namespace ReplayEngine::Rendering
{
    // MaterialAsset と ShaderPropertySchema の橋渡し。
    //
    // Editor 専用にしない理由:
    //   ・将来 Shader Composer / C# / Importer から同じ規則で Material を作れる
    //   ・Shader 切替時の「値を保持する」規則を UI ごとに重複させない
    //   ・Missing Shader でも PropertyBag を消さない契約を 1 か所に置ける
    class MaterialSchema final
    {
    public:
        // ShaderProperty が Material の PropertyBag で使う型。
        static Reflection::PropertyType PropertyTypeFor(
            ShaderPropertyKind kind) noexcept;

        // Shader 宣言から Material 用の既定値を作る。
        static Reflection::PropertyValue DefaultValueFor(
            const ShaderProperty& property);

        // Schema に存在する項目が Material に無ければ既定値を追加する。
        // 既存値は互換型へ寄せられる場合だけ正規化し、未知 Property は絶対に削除しない。
        // 何か変更した場合 true。
        static bool EnsureProperties(MaterialAsset& material,
            const ShaderPropertySchema& schema);

        // Material の Shader を切り替える。
        // 同名 Property は保持、足りないものだけ default を追加、旧 Shader 固有値は保持。
        // built-in の場合だけ旧 shading_model も互換用に同期する。
        static bool SelectShader(MaterialAsset& material,
            const ShaderCatalog::Entry& entry);

        // GUID 文字列から Catalog を引いて SelectShader する。
        // Missing のときは false を返し Material を変更しない。
        static bool SelectShader(MaterialAsset& material,
            const ShaderCatalog& catalog, const std::string& shader_guid);
    };
}
