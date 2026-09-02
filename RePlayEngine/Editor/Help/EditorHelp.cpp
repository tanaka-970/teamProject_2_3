#include "EditorHelp.h"

#include "../Commands/FileEditHistory.h"

#include "imgui/imgui.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace ReplayEngine::Editor
{
    namespace
    {
        constexpr const char* file_magic = "REPLAY_EDITOR_HELP";
        constexpr int file_version = 1;
        constexpr const char* popup_id = "##EditorHelpEditPopup";

        struct EditorHelpState final
        {
            std::filesystem::path path;
            FileEditHistory* history = nullptr;
            std::map<std::string, std::string> defaults;
            std::map<std::string, std::string> overrides;
            std::string active_key;
            std::string active_default;
            std::array<char, 4096> edit_buffer{};
            std::string save_error;
        };

        EditorHelpState& State()
        {
            static EditorHelpState state;
            return state;
        }

        std::string Escape(const std::string& value)
        {
            std::string result;
            result.reserve(value.size());
            for (const char character : value)
            {
                switch (character)
                {
                case '\\': result += "\\\\"; break;
                case '\"': result += "\\\""; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default: result += character; break;
                }
            }
            return result;
        }

        std::string Unescape(const std::string& value)
        {
            std::string result;
            result.reserve(value.size());
            bool escaped = false;
            for (const char character : value)
            {
                if (!escaped)
                {
                    if (character == '\\') escaped = true;
                    else result += character;
                    continue;
                }
                switch (character)
                {
                case '\\': result += '\\'; break;
                case '\"': result += '\"'; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                default:
                    result += '\\';
                    result += character;
                    break;
                }
                escaped = false;
            }
            if (escaped) result += '\\';
            return result;
        }

        bool ReadBytes(const std::filesystem::path& path,
            std::vector<std::uint8_t>& bytes, std::string& error)
        {
            bytes.clear();
            std::error_code filesystem_error;
            if (!std::filesystem::exists(path, filesystem_error) || filesystem_error)
            {
                error.clear();
                return true;
            }
            std::ifstream stream(path, std::ios::binary);
            if (!stream)
            {
                error = u8"EditorHelp ファイルを開けません。";
                return false;
            }
            bytes.assign(std::istreambuf_iterator<char>(stream),
                std::istreambuf_iterator<char>());
            if (!stream.eof() && stream.fail())
            {
                error = u8"EditorHelp ファイルを読み込めません。";
                bytes.clear();
                return false;
            }
            error.clear();
            return true;
        }

        bool WriteAtomic(const std::filesystem::path& path,
            const std::string& text, std::string& error)
        {
            std::error_code filesystem_error;
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path(), filesystem_error);
                if (filesystem_error)
                {
                    error = u8"EditorHelp の保存先フォルダーを作成できません。";
                    return false;
                }
            }

            const std::filesystem::path temporary = path.string() + ".tmp";
            {
                std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
                if (!stream)
                {
                    error = u8"EditorHelp の一時ファイルを作成できません。";
                    return false;
                }
                stream.write(text.data(), static_cast<std::streamsize>(text.size()));
                if (!stream)
                {
                    error = u8"EditorHelp の書き込みに失敗しました。";
                    return false;
                }
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
                    error = u8"EditorHelp の保存先を差し替えられません。";
                    return false;
                }
            }
            error.clear();
            return true;
        }

        std::string EffectiveText(const std::string& key, const std::string& fallback)
        {
            const auto found = State().overrides.find(key);
            return found == State().overrides.end() ? fallback : found->second;
        }

        const std::string& RegisteredDefault(const std::string& key)
        {
            static const std::string empty;
            const auto found = State().defaults.find(key);
            return found == State().defaults.end() ? empty : found->second;
        }

        void CopyToBuffer(const std::string& text)
        {
            EditorHelpState& state = State();
            std::snprintf(state.edit_buffer.data(), state.edit_buffer.size(), "%s", text.c_str());
        }
    }

    void EditorHelp::Configure(const std::filesystem::path& path,
        FileEditHistory* history) noexcept
    {
        EditorHelpState& state = State();
        state.path = path;
        state.history = history;
        state.defaults.clear();
        state.overrides.clear();
        state.active_key.clear();
        state.active_default.clear();
        state.edit_buffer.fill('\0');
        state.save_error.clear();
    }

    bool EditorHelp::Load(std::string& error)
    {
        EditorHelpState& state = State();
        std::vector<std::uint8_t> bytes;
        if (!ReadBytes(state.path, bytes, error)) return false;
        if (bytes.empty())
        {
            error.clear();
            return true;
        }

        std::string text(bytes.begin(), bytes.end());
        if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xef &&
            static_cast<unsigned char>(text[1]) == 0xbb &&
            static_cast<unsigned char>(text[2]) == 0xbf)
        {
            text.erase(0, 3);
        }

        std::istringstream stream(text);
        std::string line;
        if (!std::getline(stream, line))
        {
            error = u8"EditorHelp のヘッダーがありません。";
            return false;
        }
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream header(line);
        std::string magic;
        int version = 0;
        if (!(header >> magic >> version) || magic != file_magic || version != file_version)
        {
            error = u8"EditorHelp のバージョンを読めません。";
            return false;
        }

        state.overrides.clear();
        while (std::getline(stream, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.rfind("HELP\t", 0) != 0) continue;
            const std::size_t key_begin = 5;
            const std::size_t separator = line.find('\t', key_begin);
            if (separator == std::string::npos || separator == key_begin) continue;
            state.overrides[line.substr(key_begin, separator - key_begin)] =
                Unescape(line.substr(separator + 1));
        }
        error.clear();
        return true;
    }

    bool EditorHelp::Save(std::string& error)
    {
        EditorHelpState& state = State();
        if (state.path.empty())
        {
            error = u8"EditorHelp の保存先が設定されていません。";
            return false;
        }

        std::vector<std::uint8_t> before;
        std::error_code filesystem_error;
        if (std::filesystem::exists(state.path, filesystem_error) && !filesystem_error)
        {
            if (!FileEditHistory::ReadFile(state.path, before, error)) return false;
        }

        std::ostringstream output;
        output << file_magic << ' ' << file_version << '\n';
        for (const auto& entry : state.overrides)
        {
            if (entry.first.empty() || entry.second.empty()) continue;
            output << "HELP\t" << entry.first << '\t' << Escape(entry.second) << '\n';
        }
        const std::string text = output.str();
        if (!WriteAtomic(state.path, text, error)) return false;

        if (state.history != nullptr)
        {
            std::string history_error;
            state.history->RecordSavedChange(state.path, u8"Editor Help を変更", before,
                history_error);
            if (!history_error.empty())
            {
                error = history_error;
                return false;
            }
        }
        error.clear();
        return true;
    }

    void EditorHelp::Item(const char* key)
    {
        Item(key, nullptr);
    }

    void EditorHelp::Item(const char* key, const char* default_text)
    {
        if (key == nullptr || key[0] == '\0') return;
        const std::string key_text(key);
        if (default_text != nullptr)
            State().defaults[key_text] = default_text;
        const std::string fallback = default_text == nullptr
            ? RegisteredDefault(key_text) : std::string(default_text);
        const std::string effective = EffectiveText(key_text, fallback);
        const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);

        if (hovered)
        {
            ImGui::BeginTooltip();
            if (effective.empty())
                ImGui::TextUnformatted(u8"説明が未記入です。右クリックで説明を書けます。");
            else
                ImGui::TextUnformatted(effective.c_str());
            ImGui::EndTooltip();
        }

        EditorHelpState& state = State();
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            state.active_key = key_text;
            state.active_default = fallback;
            CopyToBuffer(effective);
            state.save_error.clear();
            ImGui::OpenPopup(popup_id);
        }

        if (!ImGui::BeginPopup(popup_id)) return;
        ImGui::TextUnformatted(u8"説明を編集");
        ImGui::TextDisabled("Key: %s", state.active_key.c_str());
        ImGui::InputTextMultiline("##EditorHelpText", state.edit_buffer.data(),
            state.edit_buffer.size(), ImVec2(420.0f, 120.0f));
        if (ImGui::Button(u8"保存"))
        {
            const std::string edited = state.edit_buffer.data();
            if (edited.empty() || edited == state.active_default)
                state.overrides.erase(state.active_key);
            else
                state.overrides[state.active_key] = edited;
            if (Save(state.save_error))
            {
                state.save_error.clear();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(u8"既定文へ戻す"))
        {
            state.overrides.erase(state.active_key);
            if (Save(state.save_error))
            {
                CopyToBuffer(state.active_default);
                state.save_error.clear();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(u8"書いた内容を消す"))
        {
            state.overrides.erase(state.active_key);
            if (Save(state.save_error))
            {
                state.edit_buffer.fill('\0');
                state.save_error.clear();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(u8"キャンセル")) ImGui::CloseCurrentPopup();
        if (!state.save_error.empty())
            ImGui::TextWrapped(u8"保存失敗: %s", state.save_error.c_str());
        ImGui::EndPopup();
    }

    bool EditorHelp::ValidateRoundTrip(std::string& report)
    {
        EditorHelpState& state = State();
        const std::filesystem::path previous_path = state.path;
        FileEditHistory* const previous_history = state.history;
        const auto stamp = static_cast<unsigned long long>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        const std::filesystem::path test_path = std::filesystem::path("Saved") /
            "Validation" / ("EditorHelpRoundTrip_" + std::to_string(stamp) + ".replayhelp");
        const std::string key = "validation.multiline.japanese";
        const std::string sample = u8"一行目\n二行目\n三行目";
        std::string error;
        bool missing_file_loaded = false;
        bool saved = false;
        bool no_bom = false;
        bool restored = false;

        std::error_code filesystem_error;
        std::filesystem::remove(test_path, filesystem_error);
        Configure(test_path, nullptr);
        missing_file_loaded = Load(error);
        if (missing_file_loaded)
        {
            state.overrides[key] = sample;
            saved = Save(error);
        }

        std::vector<std::uint8_t> bytes;
        if (saved)
        {
            std::ifstream stream(test_path, std::ios::binary);
            bytes.assign(std::istreambuf_iterator<char>(stream),
                std::istreambuf_iterator<char>());
            no_bom = bytes.size() < 3 || bytes[0] != 0xef ||
                bytes[1] != 0xbb || bytes[2] != 0xbf;
            if (Load(error))
            {
                const auto found = state.overrides.find(key);
                restored = found != state.overrides.end() && found->second == sample;
            }
        }

        std::filesystem::remove(test_path, filesystem_error);
        Configure(previous_path, previous_history);
        std::ostringstream result;
        result << "MISSING_FILE_LOAD " << (missing_file_loaded ? "OK" : "NG")
            << " SAVE " << (saved ? "OK" : "NG")
            << " BOM " << (no_bom ? "ABSENT" : "PRESENT")
            << " UTF8_MULTILINE_RESTORE " << (restored ? "OK" : "NG");
        if (!error.empty()) result << " ERROR " << error;
        report = result.str();
        return missing_file_loaded && saved && no_bom && restored;
    }
}
