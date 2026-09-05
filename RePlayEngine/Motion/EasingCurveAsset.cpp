#include "EasingCurveAsset.h"
#include "../Rendering/RenderStats.h"

#include "../Assets/AssetDatabase.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ReplayEngine::Motion
{
    namespace
    {
        constexpr const char* magic_token = "REPLAY_EASING_CURVE";
        constexpr int minimum_sample_count = 16;
        constexpr int maximum_sample_count = 256;

        struct CacheEntry final
        {
            std::filesystem::path path;
            std::filesystem::file_time_type write_time{};
            EasingCurveAsset asset;
            bool valid = false;
            bool attempted = false;
        };

        std::unordered_map<std::string, CacheEntry> cache;
        std::mutex cache_mutex;

        void SetLinearSamples(std::vector<float>& values, int count)
        {
            values.resize(static_cast<std::size_t>(count));
            const float denominator = static_cast<float>((std::max)(1, count - 1));
            for (int index = 0; index < count; ++index)
                values[static_cast<std::size_t>(index)] = static_cast<float>(index) / denominator;
        }

        bool RepairFiniteValues(std::vector<float>& values)
        {
            bool any_finite = false;
            for (float value : values)
            {
                if (std::isfinite(value))
                {
                    any_finite = true;
                    break;
                }
            }
            if (!any_finite) return false;

            std::size_t index = 0;
            while (index < values.size())
            {
                if (std::isfinite(values[index]))
                {
                    ++index;
                    continue;
                }
                const std::size_t begin = index;
                while (index < values.size() && !std::isfinite(values[index])) ++index;
                const bool has_left = begin > 0 && std::isfinite(values[begin - 1]);
                const bool has_right = index < values.size() && std::isfinite(values[index]);
                if (has_left && has_right)
                {
                    const float left = values[begin - 1];
                    const float right = values[index];
                    const std::size_t span = index - begin + 1;
                    for (std::size_t offset = 0; offset < index - begin; ++offset)
                    {
                        const float t = static_cast<float>(offset + 1) /
                            static_cast<float>(span);
                        values[begin + offset] = left + (right - left) * t;
                    }
                }
                else
                {
                    const float fill = has_left ? values[begin - 1] : values[index];
                    for (std::size_t offset = begin; offset < index; ++offset)
                        values[offset] = fill;
                }
            }
            return true;
        }

        std::vector<float> ResampleValues(const std::vector<float>& source, int count)
        {
            if (source.empty())
            {
                std::vector<float> linear;
                SetLinearSamples(linear, count);
                return linear;
            }
            if (source.size() == 1)
                return std::vector<float>(static_cast<std::size_t>(count), source.front());

            std::vector<float> result(static_cast<std::size_t>(count));
            const float source_last = static_cast<float>(source.size() - 1);
            const float denominator = static_cast<float>((std::max)(1, count - 1));
            for (int index = 0; index < count; ++index)
            {
                const float t = static_cast<float>(index) / denominator;
                const float scaled = t * source_last;
                const std::size_t left = static_cast<std::size_t>(std::floor(scaled));
                const std::size_t right = (std::min)(left + 1, source.size() - 1);
                const float fraction = scaled - static_cast<float>(left);
                result[static_cast<std::size_t>(index)] = source[left] +
                    (source[right] - source[left]) * fraction;
            }
            return result;
        }

        bool ReadFloat(std::istream& stream, float& value)
        {
            std::string token;
            if (!(stream >> token)) return false;
            char* end = nullptr;
            value = std::strtof(token.c_str(), &end);
            return end != token.c_str() && end != nullptr && *end == '\0';
        }

        void NormalizeControlPoints(std::vector<DirectX::XMFLOAT2>& points)
        {
            if (points.empty()) return;
            if (points.size() == 1)
            {
                points = { { 0.0f, 0.0f }, { 1.0f, 1.0f } };
                return;
            }

            std::vector<float> xs;
            std::vector<float> ys;
            xs.reserve(points.size());
            ys.reserve(points.size());
            for (const DirectX::XMFLOAT2& point : points)
            {
                xs.push_back(point.x);
                ys.push_back(point.y);
            }
            if (!RepairFiniteValues(xs))
            {
                const float denominator = static_cast<float>((std::max)(1,
                    static_cast<int>(points.size()) - 1));
                for (std::size_t index = 0; index < points.size(); ++index)
                    xs[index] = static_cast<float>(index) / denominator;
            }
            if (!RepairFiniteValues(ys))
            {
                points.clear();
                return;
            }

            float previous_x = 0.0f;
            for (std::size_t index = 0; index < points.size(); ++index)
            {
                float x = std::clamp(xs[index], 0.0f, 1.0f);
                if (index > 0 && x < previous_x) x = previous_x;
                points[index] = { x, ys[index] };
                previous_x = x;
            }
            points.front().x = 0.0f;
            points.front().y = 0.0f;
            points.back().x = 1.0f;
            points.back().y = 1.0f;
        }

        std::vector<DirectX::XMFLOAT2> CollapseDuplicateControlPoints(
            const std::vector<DirectX::XMFLOAT2>& source)
        {
            std::vector<DirectX::XMFLOAT2> result;
            result.reserve(source.size());
            for (const DirectX::XMFLOAT2& point : source)
            {
                if (!result.empty() && std::fabs(point.x - result.back().x) <= 0.000001f)
                    result.back().y = point.y;
                else
                    result.push_back(point);
            }
            return result;
        }
    }

    float EasingCurveAsset::Evaluate(float t) const noexcept
    {
        if (!std::isfinite(t)) return 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        if (samples.empty()) return t;
        if (samples.size() == 1)
            return std::isfinite(samples.front()) ? samples.front() : t;

        const float scaled = t * static_cast<float>(samples.size() - 1);
        const std::size_t left = static_cast<std::size_t>(std::floor(scaled));
        const std::size_t right = (std::min)(left + 1, samples.size() - 1);
        const float fraction = scaled - static_cast<float>(left);
        const float a = samples[left];
        const float b = samples[right];
        if (!std::isfinite(a) || !std::isfinite(b)) return t;
        return a + (b - a) * fraction;
    }

    void EasingCurveAsset::RebuildSamplesFromControlPoints() noexcept
    {
        sample_count = std::clamp(sample_count, minimum_sample_count, maximum_sample_count);
        NormalizeControlPoints(control_points);
        const std::vector<DirectX::XMFLOAT2> points =
            CollapseDuplicateControlPoints(control_points);
        if (points.size() < 2)
        {
            SetLinearSamples(samples, sample_count);
            return;
        }

        std::vector<float> slopes(points.size(), 0.0f);
        for (std::size_t index = 0; index < points.size(); ++index)
        {
            if (index == 0)
            {
                const float dx = points[1].x - points[0].x;
                slopes[index] = dx > 0.000001f ?
                    (points[1].y - points[0].y) / dx : 0.0f;
            }
            else if (index + 1 == points.size())
            {
                const float dx = points[index].x - points[index - 1].x;
                slopes[index] = dx > 0.000001f ?
                    (points[index].y - points[index - 1].y) / dx : 0.0f;
            }
            else
            {
                const float dx = points[index + 1].x - points[index - 1].x;
                slopes[index] = dx > 0.000001f ?
                    (points[index + 1].y - points[index - 1].y) / dx : 0.0f;
            }
        }

        samples.resize(static_cast<std::size_t>(sample_count));
        const float denominator = static_cast<float>((std::max)(1, sample_count - 1));
        std::size_t segment = 0;
        for (int sample = 0; sample < sample_count; ++sample)
        {
            const float x = static_cast<float>(sample) / denominator;
            while (segment + 2 < points.size() && x > points[segment + 1].x)
                ++segment;
            const std::size_t next = (std::min)(segment + 1, points.size() - 1);
            const float x0 = points[segment].x;
            const float x1 = points[next].x;
            const float width = x1 - x0;
            if (width <= 0.000001f)
            {
                samples[static_cast<std::size_t>(sample)] = points[next].y;
                continue;
            }
            const float u = std::clamp((x - x0) / width, 0.0f, 1.0f);
            const float u2 = u * u;
            const float u3 = u2 * u;
            const float h00 = 2.0f * u3 - 3.0f * u2 + 1.0f;
            const float h10 = u3 - 2.0f * u2 + u;
            const float h01 = -2.0f * u3 + 3.0f * u2;
            const float h11 = u3 - u2;
            samples[static_cast<std::size_t>(sample)] =
                h00 * points[segment].y + h10 * width * slopes[segment] +
                h01 * points[next].y + h11 * width * slopes[next];
        }
        Normalize();
    }

    void EasingCurveAsset::FitControlPointsToSamples() noexcept
    {
        Normalize();
        const int point_count = (std::max)(4, (std::min)(17, sample_count / 8 + 1));
        control_points.clear();
        control_points.reserve(static_cast<std::size_t>(point_count));
        const float denominator = static_cast<float>((std::max)(1, point_count - 1));
        for (int index = 0; index < point_count; ++index)
        {
            const float x = static_cast<float>(index) / denominator;
            control_points.push_back({ x, Evaluate(x) });
        }
        NormalizeControlPoints(control_points);
    }

    void EasingCurveAsset::Normalize() noexcept
    {
        sample_count = std::clamp(sample_count, minimum_sample_count, maximum_sample_count);
        if (samples.empty())
        {
            SetLinearSamples(samples, sample_count);
        }
        else
        {
            std::vector<float> repaired = samples;
            if (!RepairFiniteValues(repaired))
            {
                SetLinearSamples(repaired, sample_count);
            }
            else
            {
                if (static_cast<int>(repaired.size()) != sample_count)
                    repaired = ResampleValues(repaired, sample_count);
                if (!RepairFiniteValues(repaired)) SetLinearSamples(repaired, sample_count);
            }
            samples = std::move(repaired);
        }
        if (samples.empty()) SetLinearSamples(samples, sample_count);
        samples.front() = 0.0f;
        samples.back() = 1.0f;
        NormalizeControlPoints(control_points);
    }

    bool EasingCurveAsset::SaveToFile(const std::filesystem::path& path,
        std::string& error) const
    {
        EasingCurveAsset normalized = *this;
        normalized.Normalize();
        if (normalized.name.empty()) normalized.name = path.stem().u8string();

        std::error_code filesystem_error;
        if (!path.parent_path().empty())
        {
            std::filesystem::create_directories(path.parent_path(), filesystem_error);
            if (filesystem_error)
            {
                error = "Easing Curve の保存先を作成できません。";
                return false;
            }
        }

        const std::filesystem::path temporary = path.string() + ".tmp";
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            error = "Easing Curve を作成できません。";
            return false;
        }
        stream.imbue(std::locale::classic());
        stream << std::setprecision((std::numeric_limits<float>::max_digits10));
        stream << magic_token << ' ' << current_version << '\n';
        stream << "NAME " << std::quoted(normalized.name) << '\n';
        stream << "SAMPLE_COUNT " << normalized.sample_count << '\n';
        stream << "SAMPLES\n";
        for (float value : normalized.samples) stream << value << '\n';
        stream << "CONTROL_POINT_COUNT " << normalized.control_points.size() << '\n';
        for (const DirectX::XMFLOAT2& point : normalized.control_points)
            stream << "CONTROL_POINT " << point.x << ' ' << point.y << '\n';
        stream << "END_EASING_CURVE\n";
        stream.close();
        if (!stream)
        {
            error = "Easing Curve の書き込みに失敗しました。";
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
                error = "Easing Curve を差し替えられません。";
                return false;
            }
        }
        error.clear();
        return true;
    }

    bool EasingCurveAsset::LoadFromFile(const std::filesystem::path& path,
        std::string& error)
    {
        REPLAY_PROFILE_SCOPE("Asset/EasingCurve");
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            error = "Easing Curve を開けません: " + path.generic_string();
            return false;
        }
        stream.imbue(std::locale::classic());
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
            error = "Easing Curve の形式またはバージョンが不正です。";
            return false;
        }

        std::string token;
        std::string loaded_name;
        if (!(stream >> token) || token != "NAME" || !(stream >> std::quoted(loaded_name)))
        {
            error = "Easing Curve の NAME が不正です。";
            return false;
        }

        int loaded_count = 0;
        if (!(stream >> token >> loaded_count) || token != "SAMPLE_COUNT" ||
            loaded_count < minimum_sample_count || loaded_count > maximum_sample_count)
        {
            error = "Easing Curve の SAMPLE_COUNT が不正です。";
            return false;
        }
        if (!(stream >> token) || token != "SAMPLES")
        {
            error = "Easing Curve の SAMPLES がありません。";
            return false;
        }
        std::vector<float> loaded_samples(static_cast<std::size_t>(loaded_count));
        for (float& value : loaded_samples)
        {
            if (!ReadFloat(stream, value))
            {
                error = "Easing Curve の sample を読み取れません。";
                return false;
            }
        }

        std::size_t control_count = 0;
        if (!(stream >> token >> control_count) || token != "CONTROL_POINT_COUNT" ||
            control_count > 4096)
        {
            error = "Easing Curve の CONTROL_POINT_COUNT が不正です。";
            return false;
        }
        std::vector<DirectX::XMFLOAT2> loaded_points;
        loaded_points.reserve(control_count);
        for (std::size_t index = 0; index < control_count; ++index)
        {
            if (!(stream >> token) || token != "CONTROL_POINT")
            {
                error = "Easing Curve の CONTROL_POINT が不足しています。";
                return false;
            }
            DirectX::XMFLOAT2 point{};
            if (!ReadFloat(stream, point.x) || !ReadFloat(stream, point.y))
            {
                error = "Easing Curve の制御点を読み取れません。";
                return false;
            }
            loaded_points.push_back(point);
        }
        if (!(stream >> token) || token != "END_EASING_CURVE")
        {
            error = "Easing Curve の終端がありません。";
            return false;
        }

        name = std::move(loaded_name);
        sample_count = loaded_count;
        samples = std::move(loaded_samples);
        control_points = std::move(loaded_points);
        Normalize();
        error.clear();
        return true;
    }

    const EasingCurveAsset* EasingCurveAsset::Resolve(
        const Assets::AssetDatabase* database,
        const Reflection::AssetReference& reference) noexcept
    {
        if (database == nullptr || !reference.IsAssigned()) return nullptr;
        const Assets::AssetRecord* record = database->FindByGuid(reference.guid);
        if (record == nullptr || record->kind != Assets::AssetKind::EasingCurve)
            return nullptr;

        std::lock_guard<std::mutex> lock(cache_mutex);
        CacheEntry& entry = cache[reference.guid];
        const std::filesystem::path path = record->source_path;
        std::error_code file_error;
        const auto write_time = std::filesystem::last_write_time(path, file_error);
        if (entry.attempted && entry.path == path && !file_error &&
            entry.write_time == write_time)
        {
            return entry.valid ? &entry.asset : nullptr;
        }
        if (entry.attempted && entry.path == path && file_error)
            return entry.valid ? &entry.asset : nullptr;

        entry.path = path;
        entry.attempted = true;
        if (!file_error) entry.write_time = write_time;
        EasingCurveAsset candidate;
        std::string load_error;
        if (!candidate.LoadFromFile(path, load_error))
        {
            entry.valid = false;
            return nullptr;
        }
        entry.asset = std::move(candidate);
        entry.valid = true;
        return &entry.asset;
    }

    void EasingCurveAsset::Invalidate(const std::string& guid) noexcept
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        cache.erase(guid);
    }
}
