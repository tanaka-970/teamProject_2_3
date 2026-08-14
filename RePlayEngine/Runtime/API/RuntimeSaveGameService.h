#pragma once

#include "../Core/RuntimeResult.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>

namespace ReplayEngine::Runtime
{
    enum class RuntimeSaveValueType : std::uint8_t
    {
        Bool = 1,
        Int64 = 2,
        Double = 3,
        String = 4,
    };

    struct RuntimeSaveValue final
    {
        RuntimeSaveValueType type = RuntimeSaveValueType::String;
        bool bool_value = false;
        std::int64_t int_value = 0;
        double double_value = 0.0;
        std::string string_value;
    };

    // Runtime用のKey-Valueセーブ窓口。Scene/Editor autosaveとは別系統。
    class ISaveGameService
    {
    public:
        virtual ~ISaveGameService() = default;

        virtual bool Available() const noexcept { return true; }

        virtual RuntimeStatus SetBool(const std::string& slot,
            const std::string& key, bool value) = 0;
        virtual RuntimeStatus SetInt(const std::string& slot,
            const std::string& key, std::int64_t value) = 0;
        virtual RuntimeStatus SetDouble(const std::string& slot,
            const std::string& key, double value) = 0;
        virtual RuntimeStatus SetString(const std::string& slot,
            const std::string& key, const std::string& value) = 0;

        virtual RuntimeStatus GetBool(const std::string& slot,
            const std::string& key, bool& out) const = 0;
        virtual RuntimeStatus GetInt(const std::string& slot,
            const std::string& key, std::int64_t& out) const = 0;
        virtual RuntimeStatus GetDouble(const std::string& slot,
            const std::string& key, double& out) const = 0;
        virtual RuntimeStatus GetString(const std::string& slot,
            const std::string& key, std::string& out) const = 0;

        virtual RuntimeStatus HasKey(const std::string& slot,
            const std::string& key, bool& out) const = 0;
        virtual RuntimeStatus DeleteKey(const std::string& slot,
            const std::string& key) = 0;

        virtual RuntimeStatus Save(const std::string& slot) = 0;
        virtual RuntimeStatus Load(const std::string& slot) = 0;
        virtual RuntimeStatus DeleteSlot(const std::string& slot) = 0;
    };

    class RuntimeSaveGameService final : public ISaveGameService
    {
    public:
        RuntimeSaveGameService() = default;
        explicit RuntimeSaveGameService(std::filesystem::path root)
            : root_(std::move(root)) {}

        void SetRoot(std::filesystem::path root) { root_ = std::move(root); }
        const std::filesystem::path& Root() const noexcept { return root_; }
        bool Available() const noexcept override { return !root_.empty(); }

        RuntimeStatus SetBool(const std::string& slot,
            const std::string& key, bool value) override;
        RuntimeStatus SetInt(const std::string& slot,
            const std::string& key, std::int64_t value) override;
        RuntimeStatus SetDouble(const std::string& slot,
            const std::string& key, double value) override;
        RuntimeStatus SetString(const std::string& slot,
            const std::string& key, const std::string& value) override;

        RuntimeStatus GetBool(const std::string& slot,
            const std::string& key, bool& out) const override;
        RuntimeStatus GetInt(const std::string& slot,
            const std::string& key, std::int64_t& out) const override;
        RuntimeStatus GetDouble(const std::string& slot,
            const std::string& key, double& out) const override;
        RuntimeStatus GetString(const std::string& slot,
            const std::string& key, std::string& out) const override;

        RuntimeStatus HasKey(const std::string& slot,
            const std::string& key, bool& out) const override;
        RuntimeStatus DeleteKey(const std::string& slot,
            const std::string& key) override;

        RuntimeStatus Save(const std::string& slot) override;
        RuntimeStatus Load(const std::string& slot) override;
        RuntimeStatus DeleteSlot(const std::string& slot) override;

    private:
        using SlotValues = std::unordered_map<std::string, RuntimeSaveValue>;

        static bool ValidSlotName(const std::string& slot) noexcept;
        static bool ValidKey(const std::string& key) noexcept;
        static bool ValidStringValue(const std::string& value) noexcept;

        RuntimeStatus ValidateNames(const std::string& slot,
            const std::string& key) const noexcept;
        RuntimeStatus ValidateSlotOnly(const std::string& slot) const noexcept;
        const RuntimeSaveValue* FindValue(const std::string& slot,
            const std::string& key) const noexcept;
        RuntimeSaveValue* FindOrCreateValue(const std::string& slot,
            const std::string& key);
        std::filesystem::path PathFor(const std::string& slot) const;

        RuntimeStatus ReadFile(const std::string& slot, SlotValues& out) const;
        RuntimeStatus WriteFile(const std::string& slot, const SlotValues& values) const;

        std::filesystem::path root_;
        std::unordered_map<std::string, SlotValues> slots_;
    };
}
