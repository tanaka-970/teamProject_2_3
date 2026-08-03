#pragma once

#include "../../Reflection/Property/PropertyBag.h"
#include "../../Reflection/Property/PropertyDesc.h"
#include "../../Reflection/Property/PropertyValue.h"

#include <string>
#include <string_view>

namespace ReplayEngine::Scripting
{
    // ---- 値の型 -------------------------------------------------------------
    //
    // 独自の variant は作らない。
    //
    // 既存の PropertyValue が、スクリプトに必要な型をすべて持っているうえに、
    // 次のすべてが既にこれへ紐付いている。
    //   - Scene ファイルの読み書き（SceneSerializer）
    //   - Inspector の入力欄（PropertyDrawer）
    //   - 型が変わったときの救済変換（ConvertTo）
    //   - NaN / Inf の検出（IsFinite）
    //   - 許容差つきの等値比較（ValuesEqual）
    //
    // 別の型を起こすと、この 5 つを全部書き直したうえで
    // 「どちらが正か」が場所ごとに変わる状態になる。
    using ScriptValue = Reflection::PropertyValue;
    using ScriptValueType = Reflection::PropertyType;

    // Field 値の入れ物。順序を保つので、保存したファイルの差分が安定する。
    using ScriptFieldStorage = Reflection::PropertyBag;

    // ---- 保存名の約束 -------------------------------------------------------
    //
    // ScriptComponent の管理情報と、ユーザーが宣言した Field を
    // 同じ PropertyBag へ並べるため、名前空間を接頭辞で分ける。
    //
    // 【なぜ警告ではなく接頭辞なのか】
    //   ユーザーが language や class_name という名前の Field を宣言することは
    //   十分にありうる。衝突してから警告を出す方式だと、
    //     - 警告を見落とすと管理情報が Field に上書きされる
    //     - スクリプト側は正しい名前を使っているのに直させることになる
    //   接頭辞で分けておけば、そもそも衝突が起きない。
    //
    // 【Inspector には出ない】
    //   PropertyDrawer はラベルに PropertyDesc::DisplayName() を使う。
    //   Schema が display_name を必ず埋めるので、接頭辞が画面へ出ることはない。
    //
    // 【ドットを含む名前を保存できる根拠】
    //   SceneSerializer は PROPERTY 行の名前を std::quoted で囲んで書く。
    //   空白でもドットでも、そのまま往復する。
    namespace ScriptNames
    {
        // ScriptComponent 自身の管理情報。ユーザーは宣言できない。
        inline constexpr const char* internal_prefix = "__script.";

        inline constexpr const char* language = "__script.language";
        inline constexpr const char* asset = "__script.asset";
        inline constexpr const char* class_name = "__script.class";
        inline constexpr const char* execution_order = "__script.execution_order";

        // Script Asset が一時的に見つからない状態でも
        // 「どのスクリプト型だったか」を保てるようにするため保存する。
        // Lua では asset と同じ値になる。
        inline constexpr const char* type_id = "__script.type_id";

        // ユーザーが宣言した Field。
        inline constexpr const char* field_prefix = "field.";

        // "RotationSpeed" -> "field.RotationSpeed"
        std::string MakeFieldSavedName(std::string_view field_name);

        // "field.RotationSpeed" かどうか。
        bool IsFieldSavedName(std::string_view saved_name) noexcept;

        // "field.RotationSpeed" -> "RotationSpeed"。接頭辞が無ければそのまま返す。
        std::string_view StripFieldPrefix(std::string_view saved_name) noexcept;

        // "__script.language" などの管理情報かどうか。
        bool IsInternalSavedName(std::string_view saved_name) noexcept;
    }

    // ---- 表示名の組み立て ---------------------------------------------------
    //
    // スクリプトが display 指定を書かなかったときに使う。
    //   "RotationSpeed"  -> "Rotation Speed"
    //   "openAngle"      -> "Open Angle"
    //   "HP"             -> "HP"          （連続する大文字は割らない）
    //   "maxHP"          -> "Max HP"
    //   "target_object"  -> "Target Object"
    //
    // Editor の既存表示は日本語だが、スクリプトの Field 名は
    // ユーザーが書いた識別子なので、機械的に読みやすくするだけに留める。
    std::string HumanizeFieldName(std::string_view field_name);
}
