#include "ScriptTypes.h"

#include <string>

namespace ReplayEngine::Scripting
{
    namespace
    {
        // FNV-1a を 64bit ずつ 2 本回して 128bit を作る。
        //
        // 暗号強度は要らない。必要なのは次の 3 つだけ。
        //   - 実行ごとに値が変わらない（保存できる）
        //   - 環境やコンパイラで値が変わらない（他人の PC でも同じ Scene が読める）
        //   - 実用的な入力数で衝突しない
        //
        // 2 本目は初期値と乗数を変えて独立させる。
        // 同じ関数を 2 回回すと 2 本が完全に相関し、実質 64bit になってしまう。
        struct Fnv128 final
        {
            std::uint64_t high = 14695981039346656037ull;
            std::uint64_t low = 1099511628211ull;

            void Feed(std::string_view text) noexcept
            {
                for (const char character : text)
                {
                    const auto byte = static_cast<std::uint64_t>(
                        static_cast<unsigned char>(character));

                    high ^= byte;
                    high *= 1099511628211ull;

                    low ^= byte + 0x9e3779b97f4a7c15ull;
                    low *= 1099511628213ull;
                }
            }

            ScriptTypeID Finish() const noexcept
            {
                ScriptTypeID result;
                result.high = high;
                result.low = low;

                // 0 は「無効」の予約値。衝突したらずらす。
                // ComponentTypeID が同じ理由で 1 へずらしているのと揃える。
                if (!result.IsValid()) result.low = 1;
                return result;
            }
        };
    }

    ScriptTypeID MakeLuaScriptTypeID(std::string_view asset_guid) noexcept
    {
        if (asset_guid.empty()) return InvalidScriptTypeID();

        // AssetGUID をそのまま ScriptTypeID として読む。ハッシュしない。
        //
        // TryParse はハイフン入りも大文字も受け付けるので、
        // AssetDatabase の表記が将来変わっても追従できる。
        ScriptTypeID parsed;
        if (Reflection::TypeGUID::TryParse(asset_guid, parsed) && parsed.IsValid())
        {
            return parsed;
        }

        // 32 桁 16 進として読めない文字列だった場合だけハッシュへ落とす。
        // ここで無効値を返してしまうと、GUID の表記が想定外だっただけで
        // スクリプトが「型なし」になり、Field 値の対応付けが切れる。
        Fnv128 hasher;
        hasher.Feed("lua:");
        hasher.Feed(asset_guid);
        return hasher.Finish();
    }

    ScriptTypeID MakeCSharpScriptTypeID(std::string_view asset_guid,
        std::string_view full_class_name) noexcept
    {
        // C# は 1 ファイルに複数クラスを書けるので、両方そろって初めて一意になる。
        if (asset_guid.empty() || full_class_name.empty()) return InvalidScriptTypeID();

        Fnv128 hasher;
        hasher.Feed(asset_guid);
        hasher.Feed("#");
        hasher.Feed(full_class_name);
        return hasher.Finish();
    }

    ScriptTypeID MakeScriptTypeID(ScriptLanguage language,
        std::string_view asset_guid, std::string_view full_class_name) noexcept
    {
        switch (language)
        {
        case ScriptLanguage::Lua:
            return MakeLuaScriptTypeID(asset_guid);
        case ScriptLanguage::CSharp:
            return MakeCSharpScriptTypeID(asset_guid, full_class_name);
        }
        return InvalidScriptTypeID();
    }

    const char* ToString(ScriptCallback callback) noexcept
    {
        switch (callback)
        {
        case ScriptCallback::Awake:       return "Awake";
        case ScriptCallback::OnEnable:    return "OnEnable";
        case ScriptCallback::Start:       return "Start";
        case ScriptCallback::FixedUpdate: return "FixedUpdate";
        case ScriptCallback::Update:      return "Update";
        case ScriptCallback::LateUpdate:  return "LateUpdate";
        case ScriptCallback::OnDisable:   return "OnDisable";
        case ScriptCallback::OnDestroy:   return "OnDestroy";
        }
        return "(unknown)";
    }

    bool ScriptCallbackTakesDeltaTime(ScriptCallback callback) noexcept
    {
        return callback == ScriptCallback::Update ||
            callback == ScriptCallback::FixedUpdate ||
            callback == ScriptCallback::LateUpdate;
    }

    const char* ToString(ScriptStatus status) noexcept
    {
        switch (status)
        {
        case ScriptStatus::Unassigned: return "Unassigned";
        case ScriptStatus::Unresolved: return "Unresolved";
        case ScriptStatus::Loaded:     return "Loaded";
        case ScriptStatus::Running:    return "Running";
        case ScriptStatus::Error:      return "Error";
        }
        return "(unknown)";
    }

    const char* ToString(ScriptInvokeResult result) noexcept
    {
        switch (result)
        {
        case ScriptInvokeResult::Ok:                 return "Ok";
        case ScriptInvokeResult::NotImplemented:     return "NotImplemented";
        case ScriptInvokeResult::NoInstance:         return "NoInstance";
        case ScriptInvokeResult::BackendUnavailable: return "BackendUnavailable";
        case ScriptInvokeResult::RuntimeError:       return "RuntimeError";
        }
        return "(unknown)";
    }
}
