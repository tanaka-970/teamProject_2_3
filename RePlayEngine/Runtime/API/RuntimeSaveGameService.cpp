#include "RuntimeSaveGameService.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <utility>

namespace ReplayEngine::Runtime
{
    namespace
    {
        constexpr std::array<char, 8> magic = { 'R', 'P', 'S', 'A', 'V', 'E', '1', '\0' };
        constexpr std::uint32_t version = 1;
        constexpr std::uint32_t maximum_entries = 4096;
        constexpr std::uint32_t maximum_key_bytes = 256;
        constexpr std::uint32_t maximum_string_bytes = 64 * 1024;

        bool WriteBytes(std::ostream& stream, const void* data, std::size_t size)
        {
            stream.write(static_cast<const char*>(data),
                static_cast<std::streamsize>(size));
            return static_cast<bool>(stream);
        }

        template<class T>
        bool WritePod(std::ostream& stream, const T& value)
        {
            return WriteBytes(stream, &value, sizeof(T));
        }

        bool ReadBytes(std::istream& stream, void* data, std::size_t size)
        {
            stream.read(static_cast<char*>(data), static_cast<std::streamsize>(size));
            return static_cast<bool>(stream);
        }

        template<class T>
        bool ReadPod(std::istream& stream, T& value)
        {
            return ReadBytes(stream, &value, sizeof(T));
        }

        bool SizeToU32(std::size_t size, std::uint32_t& out) noexcept
        {
            if (size > (std::numeric_limits<std::uint32_t>::max)()) return false;
            out = static_cast<std::uint32_t>(size);
            return true;
        }
    }

    bool RuntimeSaveGameService::ValidSlotName(const std::string& slot) noexcept
    {
        if (slot.empty() || slot.size() > 64) return false;
        return std::all_of(slot.begin(), slot.end(), [](unsigned char value)
        {
            return (value >= 'a' && value <= 'z') ||
                (value >= 'A' && value <= 'Z') ||
                (value >= '0' && value <= '9') || value == '_' || value == '-';
        });
    }

    bool RuntimeSaveGameService::ValidKey(const std::string& key) noexcept
    {
        return !key.empty() && key.size() <= maximum_key_bytes &&
            key.find('\0') == std::string::npos;
    }

    bool RuntimeSaveGameService::ValidStringValue(const std::string& value) noexcept
    {
        return value.size() <= maximum_string_bytes &&
            value.find('\0') == std::string::npos;
    }

    RuntimeStatus RuntimeSaveGameService::ValidateSlotOnly(
        const std::string& slot) const noexcept
    {
        if (!Available() || !ValidSlotName(slot)) return RuntimeStatus::InvalidArgument;
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeSaveGameService::ValidateNames(const std::string& slot,
        const std::string& key) const noexcept
    {
        const RuntimeStatus slot_status = ValidateSlotOnly(slot);
        if (slot_status != RuntimeStatus::Ok) return slot_status;
        return ValidKey(key) ? RuntimeStatus::Ok : RuntimeStatus::InvalidArgument;
    }

    const RuntimeSaveValue* RuntimeSaveGameService::FindValue(
        const std::string& slot, const std::string& key) const noexcept
    {
        const auto slot_it = slots_.find(slot);
        if (slot_it == slots_.end()) return nullptr;
        const auto value_it = slot_it->second.find(key);
        return value_it != slot_it->second.end() ? &value_it->second : nullptr;
    }

    RuntimeSaveValue* RuntimeSaveGameService::FindOrCreateValue(
        const std::string& slot, const std::string& key)
    {
        return &slots_[slot][key];
    }

    std::filesystem::path RuntimeSaveGameService::PathFor(
        const std::string& slot) const
    {
        return root_ / (slot + ".replaysave");
    }

    RuntimeStatus RuntimeSaveGameService::SetBool(const std::string& slot,
        const std::string& key, bool value)
    {
        const RuntimeStatus status = ValidateNames(slot, key);
        if (status != RuntimeStatus::Ok) return status;
        RuntimeSaveValue* target = FindOrCreateValue(slot, key);
        target->type = RuntimeSaveValueType::Bool;
        target->bool_value = value;
        target->string_value.clear();
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeSaveGameService::SetInt(const std::string& slot,
        const std::string& key, std::int64_t value)
    {
        const RuntimeStatus status = ValidateNames(slot, key);
        if (status != RuntimeStatus::Ok) return status;
        RuntimeSaveValue* target = FindOrCreateValue(slot, key);
        target->type = RuntimeSaveValueType::Int64;
        target->int_value = value;
        target->string_value.clear();
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeSaveGameService::SetDouble(const std::string& slot,
        const std::string& key, double value)
    {
        const RuntimeStatus status = ValidateNames(slot, key);
        if (status != RuntimeStatus::Ok || !std::isfinite(value))
            return status != RuntimeStatus::Ok ? status : RuntimeStatus::InvalidArgument;
        RuntimeSaveValue* target = FindOrCreateValue(slot, key);
        target->type = RuntimeSaveValueType::Double;
        target->double_value = value;
        target->string_value.clear();
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeSaveGameService::SetString(const std::string& slot,
        const std::string& key, const std::string& value)
    {
        const RuntimeStatus status = ValidateNames(slot, key);
        if (status != RuntimeStatus::Ok) return status;
        if (!ValidStringValue(value)) return RuntimeStatus::InvalidArgument;
        RuntimeSaveValue* target = FindOrCreateValue(slot, key);
        target->type = RuntimeSaveValueType::String;
        target->string_value = value;
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeSaveGameService::GetBool(const std::string& slot,
        const std::string& key, bool& out) const
    {
        const RuntimeStatus status = ValidateNames(slot, key);
        if (status != RuntimeStatus::Ok) return status;
        const RuntimeSaveValue* value = FindValue(slot, key);
        if (value == nullptr) return RuntimeStatus::SaveKeyNotFound;
        if (value->type != RuntimeSaveValueType::Bool) return RuntimeStatus::SaveTypeMismatch;
        out = value->bool_value;
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeSaveGameService::GetInt(const std::string& slot,
        const std::string& key, std::int64_t& out) const
    {
        const RuntimeStatus status = ValidateNames(slot, key);
        if (status != RuntimeStatus::Ok) return status;
        const RuntimeSaveValue* value = FindValue(slot, key);
        if (value == nullptr) return RuntimeStatus::SaveKeyNotFound;
        if (value->type != RuntimeSaveValueType::Int64) return RuntimeStatus::SaveTypeMismatch;
        out = value->int_value;
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeSaveGameService::GetDouble(const std::string& slot,
        const std::string& key, double& out) const
    {
        const RuntimeStatus status = ValidateNames(slot, key);
        if (status != RuntimeStatus::Ok) return status;
        const RuntimeSaveValue* value = FindValue(slot, key);
        if (value == nullptr) return RuntimeStatus::SaveKeyNotFound;
        if (value->type != RuntimeSaveValueType::Double) return RuntimeStatus::SaveTypeMismatch;
        out = value->double_value;
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeSaveGameService::GetString(const std::string& slot,
        const std::string& key, std::string& out) const
    {
        const RuntimeStatus status = ValidateNames(slot, key);
        if (status != RuntimeStatus::Ok) return status;
        const RuntimeSaveValue* value = FindValue(slot, key);
        if (value == nullptr) return RuntimeStatus::SaveKeyNotFound;
        if (value->type != RuntimeSaveValueType::String) return RuntimeStatus::SaveTypeMismatch;
        out = value->string_value;
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeSaveGameService::HasKey(const std::string& slot,
        const std::string& key, bool& out) const
    {
        const RuntimeStatus status = ValidateNames(slot, key);
        if (status != RuntimeStatus::Ok) return status;
        out = FindValue(slot, key) != nullptr;
        return slots_.find(slot) != slots_.end() ? RuntimeStatus::Ok
            : RuntimeStatus::SaveSlotNotFound;
    }

    RuntimeStatus RuntimeSaveGameService::DeleteKey(const std::string& slot,
        const std::string& key)
    {
        const RuntimeStatus status = ValidateNames(slot, key);
        if (status != RuntimeStatus::Ok) return status;
        const auto slot_it = slots_.find(slot);
        if (slot_it == slots_.end()) return RuntimeStatus::SaveSlotNotFound;
        const auto erased = slot_it->second.erase(key);
        return erased != 0 ? RuntimeStatus::Ok : RuntimeStatus::SaveKeyNotFound;
    }

    RuntimeStatus RuntimeSaveGameService::Save(const std::string& slot)
    {
        const RuntimeStatus status = ValidateSlotOnly(slot);
        if (status != RuntimeStatus::Ok) return status;
        const auto slot_it = slots_.find(slot);
        const SlotValues empty;
        return WriteFile(slot, slot_it != slots_.end() ? slot_it->second : empty);
    }

    RuntimeStatus RuntimeSaveGameService::Load(const std::string& slot)
    {
        const RuntimeStatus status = ValidateSlotOnly(slot);
        if (status != RuntimeStatus::Ok) return status;
        SlotValues loaded;
        const RuntimeStatus read_status = ReadFile(slot, loaded);
        if (read_status != RuntimeStatus::Ok) return read_status;
        slots_[slot] = std::move(loaded);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeSaveGameService::DeleteSlot(const std::string& slot)
    {
        const RuntimeStatus status = ValidateSlotOnly(slot);
        if (status != RuntimeStatus::Ok) return status;
        slots_.erase(slot);
        std::error_code error;
        const bool existed = std::filesystem::exists(PathFor(slot), error) && !error;
        if (!existed) return RuntimeStatus::SaveSlotNotFound;
        std::filesystem::remove(PathFor(slot), error);
        return error ? RuntimeStatus::SaveIOFailure : RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeSaveGameService::WriteFile(const std::string& slot,
        const SlotValues& values) const
    {
        if (values.size() > maximum_entries) return RuntimeStatus::InvalidArgument;

        std::error_code error;
        std::filesystem::create_directories(root_, error);
        if (error) return RuntimeStatus::SaveIOFailure;

        const std::filesystem::path destination = PathFor(slot);
        const std::filesystem::path temporary = destination.string() + ".tmp";
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) return RuntimeStatus::SaveIOFailure;

        if (!WriteBytes(stream, magic.data(), magic.size()) ||
            !WritePod(stream, version)) return RuntimeStatus::SaveIOFailure;

        const std::uint32_t count = static_cast<std::uint32_t>(values.size());
        if (!WritePod(stream, count)) return RuntimeStatus::SaveIOFailure;

        for (const auto& [key, value] : values)
        {
            std::uint32_t key_size = 0;
            if (!SizeToU32(key.size(), key_size) || key_size > maximum_key_bytes ||
                !ValidKey(key) || !WritePod(stream, key_size) ||
                !WriteBytes(stream, key.data(), key.size()))
            {
                return RuntimeStatus::SaveIOFailure;
            }

            const auto type = static_cast<std::uint8_t>(value.type);
            if (!WritePod(stream, type)) return RuntimeStatus::SaveIOFailure;
            switch (value.type)
            {
            case RuntimeSaveValueType::Bool:
            {
                const std::uint8_t boolean = value.bool_value ? 1u : 0u;
                if (!WritePod(stream, boolean)) return RuntimeStatus::SaveIOFailure;
                break;
            }
            case RuntimeSaveValueType::Int64:
                if (!WritePod(stream, value.int_value)) return RuntimeStatus::SaveIOFailure;
                break;
            case RuntimeSaveValueType::Double:
                if (!std::isfinite(value.double_value) ||
                    !WritePod(stream, value.double_value))
                    return RuntimeStatus::SaveIOFailure;
                break;
            case RuntimeSaveValueType::String:
            {
                std::uint32_t string_size = 0;
                if (!ValidStringValue(value.string_value) ||
                    !SizeToU32(value.string_value.size(), string_size) ||
                    !WritePod(stream, string_size) ||
                    !WriteBytes(stream, value.string_value.data(), value.string_value.size()))
                    return RuntimeStatus::SaveIOFailure;
                break;
            }
            default:
                return RuntimeStatus::SaveCorrupt;
            }
        }
        stream.flush();
        stream.close();
        if (!stream) return RuntimeStatus::SaveIOFailure;

        std::filesystem::remove(destination, error);
        error.clear();
        std::filesystem::rename(temporary, destination, error);
        if (error)
        {
            std::filesystem::remove(temporary, error);
            return RuntimeStatus::SaveIOFailure;
        }
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeSaveGameService::ReadFile(const std::string& slot,
        SlotValues& out) const
    {
        const std::filesystem::path path = PathFor(slot);
        std::error_code filesystem_error;
        if (!std::filesystem::exists(path, filesystem_error) || filesystem_error)
            return RuntimeStatus::SaveSlotNotFound;

        std::ifstream stream(path, std::ios::binary);
        if (!stream) return RuntimeStatus::SaveIOFailure;

        std::array<char, magic.size()> actual{};
        std::uint32_t file_version = 0;
        std::uint32_t count = 0;
        if (!ReadBytes(stream, actual.data(), actual.size()) ||
            actual != magic || !ReadPod(stream, file_version) ||
            file_version != version || !ReadPod(stream, count) ||
            count > maximum_entries)
        {
            return RuntimeStatus::SaveCorrupt;
        }

        SlotValues loaded;
        for (std::uint32_t index = 0; index < count; ++index)
        {
            std::uint32_t key_size = 0;
            if (!ReadPod(stream, key_size) || key_size == 0 ||
                key_size > maximum_key_bytes)
                return RuntimeStatus::SaveCorrupt;
            std::string key(key_size, '\0');
            if (!ReadBytes(stream, key.data(), key.size()) || !ValidKey(key))
                return RuntimeStatus::SaveCorrupt;

            std::uint8_t raw_type = 0;
            if (!ReadPod(stream, raw_type)) return RuntimeStatus::SaveCorrupt;
            RuntimeSaveValue value;
            value.type = static_cast<RuntimeSaveValueType>(raw_type);
            switch (value.type)
            {
            case RuntimeSaveValueType::Bool:
            {
                std::uint8_t boolean = 0;
                if (!ReadPod(stream, boolean) || boolean > 1)
                    return RuntimeStatus::SaveCorrupt;
                value.bool_value = boolean != 0;
                break;
            }
            case RuntimeSaveValueType::Int64:
                if (!ReadPod(stream, value.int_value)) return RuntimeStatus::SaveCorrupt;
                break;
            case RuntimeSaveValueType::Double:
                if (!ReadPod(stream, value.double_value) ||
                    !std::isfinite(value.double_value))
                    return RuntimeStatus::SaveCorrupt;
                break;
            case RuntimeSaveValueType::String:
            {
                std::uint32_t string_size = 0;
                if (!ReadPod(stream, string_size) || string_size > maximum_string_bytes)
                    return RuntimeStatus::SaveCorrupt;
                value.string_value.resize(string_size);
                if (!ReadBytes(stream, value.string_value.data(), string_size) ||
                    !ValidStringValue(value.string_value))
                    return RuntimeStatus::SaveCorrupt;
                break;
            }
            default:
                return RuntimeStatus::SaveCorrupt;
            }
            if (!loaded.emplace(std::move(key), std::move(value)).second)
                return RuntimeStatus::SaveCorrupt;
        }
        out = std::move(loaded);
        return RuntimeStatus::Ok;
    }
}
