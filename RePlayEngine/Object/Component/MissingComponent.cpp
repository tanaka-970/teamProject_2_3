#include "MissingComponent.h"

namespace ReplayEngine::Core
{
    std::string MissingComponent::DescribeMissingType() const
    {
        if (!original_.type_name.empty()) return original_.type_name + " (Missing)";
        if (original_.type_guid.IsValid())
        {
            return "(Missing " + original_.type_guid.ToString() + ")";
        }
        return "(Missing Component)";
    }

    std::string MissingComponent::DescribeReason() const
    {
        std::string reason = "この Component の型が見つかりません。";

        if (!original_.module_id.empty())
        {
            reason += " モジュール: " + original_.module_id + "。";
        }
        if (original_.type_guid.IsValid())
        {
            reason += " Type GUID: " + original_.type_guid.ToString() + "。";
        }

        reason += " 保存されていた値 " + std::to_string(original_.properties.Size()) +
            " 件はそのまま保持しています。この Scene を保存し直しても失われません。"
            " 型が使えるようになれば自動的に元の Component へ戻ります。";
        return reason;
    }
}
