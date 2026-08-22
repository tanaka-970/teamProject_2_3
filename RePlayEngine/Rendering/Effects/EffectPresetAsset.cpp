#include "EffectPresetAsset.h"

#include "../../Assets/AssetDatabase.h"
#include "../../Components/UI/UIEffectStackComponent.h"
#include "../../Reflection/Property/PropertyBag.h"
#include "../../Scene/Serialization/SceneSerializerInternal.h"

#include <fstream>
#include <iomanip>
#include <locale>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace ReplayEngine::Rendering::Effects
{
    namespace
    {
        constexpr const char* magic_token = "REPLAY_EFFECT_PRESET";
        struct CacheEntry final
        {
            std::filesystem::path path;
            std::filesystem::file_time_type write_time{};
            EffectPresetAsset asset;
            bool valid = false;
            bool attempted = false;
        };
        std::unordered_map<std::string, CacheEntry> cache;
        std::mutex cache_mutex;
    }

    bool EffectPresetAsset::SaveToFile(const std::filesystem::path& path,
        std::string& error) const
    {
        Components::UIEffectStackComponent stack;
        stack.effect_count = static_cast<int>(effects.size());
        stack.effects = effects;
        Reflection::PropertyBag properties;
        stack.OnSerialize(properties);
        // capture_backdrop は UI 要素固有で、Model / Screen と共有する Preset の
        // 構成値ではない。共通 Asset へ UI 専用設定を混ぜない。
        properties.Remove("capture_backdrop");
        // 範囲制限は Stack 側の選択状態（全体/個別/反転）として保持する。
        // Preset は Effect の並びとパラメータだけを共有する。
        properties.Remove("effect_region_enabled");
        properties.Remove("effect_region_shape");
        properties.Remove("effect_region_scope");
        properties.Remove("effect_region_invert");
        properties.Remove("effect_region_center");
        properties.Remove("effect_region_size");
        properties.Remove("effect_region_rotation");
        properties.Remove("effect_region_feather");
        properties.Remove("effect_region_strength");
        properties.Remove("effect_region_mask");
        properties.Remove("effect_region_additional_count");

        std::error_code filesystem_error;
        if (!path.parent_path().empty())
        {
            std::filesystem::create_directories(path.parent_path(), filesystem_error);
            if (filesystem_error)
            {
                error = "Effect Preset の保存先を作成できません。";
                return false;
            }
        }
        const std::filesystem::path temporary = path.string() + ".tmp";
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            error = "Effect Preset を作成できません。";
            return false;
        }
        stream.imbue(std::locale::classic());
        stream << magic_token << ' ' << current_version << '\n';
        stream << "PROPERTY_COUNT " << properties.Size() << '\n';
        for (const Reflection::PropertyBag::Entry& entry : properties.Entries())
        {
            if (!Scene::Serialization::Detail::WriteProperty(stream, entry.name, entry.value))
            {
                error = "Effect Preset の Property を書き込めません: " + entry.name;
                return false;
            }
        }
        stream << "END_EFFECT_PRESET\n";
        stream.close();
        if (!stream)
        {
            error = "Effect Preset の書き込みに失敗しました。";
            return false;
        }
        std::filesystem::rename(temporary, path, filesystem_error);
        if (filesystem_error)
        {
            filesystem_error.clear();
            std::filesystem::copy_file(temporary, path,
                std::filesystem::copy_options::overwrite_existing, filesystem_error);
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            if (filesystem_error)
            {
                error = "Effect Preset を差し替えられません。";
                return false;
            }
        }
        error.clear();
        return true;
    }

    bool EffectPresetAsset::LoadFromFile(const std::filesystem::path& path,
        std::string& error)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            error = "Effect Preset を開けません: " + path.generic_string();
            return false;
        }
        stream.imbue(std::locale::classic());
        // UTF-8 BOM 付き asset も許容する。
        if (stream.peek() == static_cast<unsigned char>(0xEF))
        {
            char bom[3]{};
            stream.read(bom, 3);
            if (!stream || static_cast<unsigned char>(bom[0]) != 0xEFu ||
                static_cast<unsigned char>(bom[1]) != 0xBBu ||
                static_cast<unsigned char>(bom[2]) != 0xBFu)
            {
                stream.clear();
                stream.seekg(0);
            }
        }
        std::string magic;
        int version = 0;
        if (!(stream >> magic >> version) || magic != magic_token || version != current_version)
        {
            error = "Effect Preset の形式またはバージョンが不正です。";
            return false;
        }
        std::string property_count_token;
        std::size_t property_count = 0;
        if (!(stream >> property_count_token >> property_count) ||
            property_count_token != "PROPERTY_COUNT" || property_count > 4096)
        {
            error = "Effect Preset の PROPERTY_COUNT が不正です。";
            return false;
        }
        Reflection::PropertyBag properties;
        for (std::size_t index = 0; index < property_count; ++index)
        {
            std::string token;
            if (!(stream >> token) || token != "PROPERTY")
            {
                error = "Effect Preset の PROPERTY 行が不足しています。";
                return false;
            }
            if (!Scene::Serialization::Detail::ReadProperty(stream, properties, error))
                return false;
        }
        std::string end;
        if (!(stream >> end) || end != "END_EFFECT_PRESET")
        {
            error = "Effect Preset の終端がありません。";
            return false;
        }
        Components::UIEffectStackComponent stack;
        stack.OnDeserialize(properties);
        effects = stack.effects;
        error.clear();
        return true;
    }

    const std::vector<UI::UIEffect>* EffectPresetAsset::Resolve(
        const Assets::AssetDatabase* database,
        const Reflection::AssetReference& reference) noexcept
    {
        if (database == nullptr || !reference.IsAssigned()) return nullptr;
        const Assets::AssetRecord* record = database->FindByGuid(reference.guid);
        if (record == nullptr || record->kind != Assets::AssetKind::EffectPreset)
            return nullptr;
        std::lock_guard<std::mutex> lock(cache_mutex);
        CacheEntry& entry = cache[reference.guid];
        const std::filesystem::path path = record->source_path;
        std::error_code file_error;
        const auto write_time = std::filesystem::last_write_time(path, file_error);
        // 失敗結果も更新時刻単位でキャッシュする。編集中の一時的な壊れた Preset が
        // 参照されても毎 Effect・毎フレーム同じファイルを再パースしない。
        if (entry.attempted && entry.path == path && !file_error &&
            entry.write_time == write_time)
        {
            return entry.valid ? &entry.asset.effects : nullptr;
        }
        if (entry.attempted && entry.path == path && file_error)
            return entry.valid ? &entry.asset.effects : nullptr;

        entry.path = path;
        entry.attempted = true;
        if (!file_error) entry.write_time = write_time;
        EffectPresetAsset candidate;
        std::string error;
        if (!candidate.LoadFromFile(path, error))
        {
            entry.valid = false;
            return nullptr;
        }
        entry.asset = std::move(candidate);
        entry.valid = true;
        return &entry.asset.effects;
    }

    void EffectPresetAsset::Invalidate(const std::string& guid) noexcept
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        cache.erase(guid);
    }
}
