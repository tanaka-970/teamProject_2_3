#include "LandscapeComponent.h"

#include "../../Reflection/Property/PropertyBag.h"
#include "../../Reflection/Property/PropertyValue.h"

namespace ReplayEngine::Components
{
    LandscapeComponent::LandscapeComponent()
    {
        // Add Component 直後から必ず有効な geometry を持たせる。
        // 33x33 = 1024 cells なので編集確認に十分、Scene 保存も軽い。
        data_.Initialize(default_resolution, default_resolution, default_cell_size, 0.0f);
    }

    bool LandscapeComponent::GenerateFlat(int width, int height, float cell_size,
        float height_value)
    {
        if (!data_.Initialize(width, height, cell_size, height_value)) return false;
        default_resolution = width == height ? width : default_resolution;
        default_cell_size = cell_size;
        deserialize_error_.clear();
        return true;
    }

    void LandscapeComponent::OnSerialize(Reflection::PropertyBag& output) const
    {
        output.Set("mesh_data",
            Reflection::PropertyValue::MakeString(data_.SerializeInline()));
    }

    void LandscapeComponent::OnDeserialize(const Reflection::PropertyBag& input)
    {
        const Reflection::PropertyValue* mesh_data = input.Find("mesh_data");
        if (mesh_data == nullptr || mesh_data->AsString().empty()) return;
        std::string error;
        if (!data_.DeserializeInline(mesh_data->AsString(), error))
        {
            // 壊れた Scene を開いても Component 自体は残す。
            // constructor の安全な flat mesh を維持し、Inspector 診断へ理由を出せる。
            deserialize_error_ = std::move(error);
            return;
        }
        deserialize_error_.clear();
    }
}
