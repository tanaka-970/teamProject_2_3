#include "RigClip.h"

#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <system_error>

namespace ReplayEngine::Motion
{
    namespace
    {
        // 行頭の語を取り出す。MotionAsset の読み取りと同じ組み立て方にしてある。
        bool ReadKeyword(std::istringstream& stream, std::string& keyword)
        {
            return static_cast<bool>(stream >> keyword);
        }
    }

    bool RigClip::SaveToFile(const std::filesystem::path& path, const RigClip& clip,
        std::string& error)
    {
        if (path.has_parent_path())
        {
            std::error_code code;
            std::filesystem::create_directories(path.parent_path(), code);
            if (code)
            {
                error = "Rig Clip の保存先を作成できません: " + code.message();
                return false;
            }
        }

        std::ofstream file(path);
        file.imbue(std::locale::classic());
        if (!file)
        {
            error = "Rig Clip を書き込めません: " + path.string();
            return false;
        }

        file << std::setprecision(std::numeric_limits<float>::max_digits10);
        file << "RIGCLIP " << std::quoted(clip.name) << '\n';
        file << "RIGCLIP_VERSION " << RigClip::current_version << '\n';
        file << "MODEL " << std::quoted(clip.model_path) << '\n';
        file << "SKELETON " << std::quoted(clip.skeleton_hash) << '\n';
        file << "DURATION " << clip.duration << '\n';
        file << "FRAME_RATE " << clip.frame_rate << '\n';
        file << "LOOP " << (clip.loop ? 1 : 0) << '\n';

        for (const RigTrack& track : clip.tracks)
        {
            file << '\n';
            file << "TRACK " << std::quoted(track.bone_path) << '\n';
            file << "INTERP " << static_cast<int>(track.interpolation) << '\n';
            for (const RigKey& key : track.keys)
            {
                // 回転はクォータニオン。オイラーへ落とすと補間で破綻する。
                file << "KEY " << key.time
                    << ' ' << key.transform.scale.x
                    << ' ' << key.transform.scale.y
                    << ' ' << key.transform.scale.z
                    << ' ' << key.transform.rotation.x
                    << ' ' << key.transform.rotation.y
                    << ' ' << key.transform.rotation.z
                    << ' ' << key.transform.rotation.w
                    << ' ' << key.transform.translation.x
                    << ' ' << key.transform.translation.y
                    << ' ' << key.transform.translation.z << '\n';
            }
            file << "END_TRACK\n";
        }
        return true;
    }

    bool RigClip::LoadFromFile(const std::filesystem::path& path, RigClip& clip,
        std::string& error)
    {
        std::ifstream file(path);
        file.imbue(std::locale::classic());
        if (!file)
        {
            error = "Rig Clip を読み込めません: " + path.string();
            return false;
        }

        clip = RigClip{};
        RigTrack* track = nullptr;
        std::string line;
        while (std::getline(file, line))
        {
            std::istringstream stream(line);
            stream.imbue(std::locale::classic());
            std::string keyword;
            if (!ReadKeyword(stream, keyword) || keyword.empty()) continue;

            if (keyword == "RIGCLIP") stream >> std::quoted(clip.name);
            else if (keyword == "RIGCLIP_VERSION") stream >> clip.version;
            else if (keyword == "MODEL") stream >> std::quoted(clip.model_path);
            else if (keyword == "SKELETON") stream >> std::quoted(clip.skeleton_hash);
            else if (keyword == "DURATION") stream >> clip.duration;
            else if (keyword == "FRAME_RATE") stream >> clip.frame_rate;
            else if (keyword == "LOOP")
            {
                int loop = 1;
                stream >> loop;
                clip.loop = loop != 0;
            }
            else if (keyword == "TRACK")
            {
                clip.tracks.emplace_back();
                track = &clip.tracks.back();
                stream >> std::quoted(track->bone_path);
            }
            else if (keyword == "INTERP" && track != nullptr)
            {
                int interpolation = 1;
                stream >> interpolation;
                track->interpolation = static_cast<RigInterpolation>(interpolation);
            }
            else if (keyword == "KEY" && track != nullptr)
            {
                RigKey key{};
                stream >> key.time
                    >> key.transform.scale.x >> key.transform.scale.y >> key.transform.scale.z
                    >> key.transform.rotation.x >> key.transform.rotation.y
                    >> key.transform.rotation.z >> key.transform.rotation.w
                    >> key.transform.translation.x >> key.transform.translation.y
                    >> key.transform.translation.z;
                if (stream.fail())
                {
                    error = "Rig Clip の KEY 行が壊れています: " + path.string();
                    return false;
                }
                track->keys.push_back(key);
            }
            else if (keyword == "END_TRACK") track = nullptr;
        }

        if (clip.version > RigClip::current_version)
        {
            error = "この Rig Clip は新しい版です: " + path.string();
            return false;
        }
        return true;
    }
}
