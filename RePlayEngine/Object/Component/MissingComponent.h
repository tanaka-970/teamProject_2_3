#pragma once

#include "Component.h"
#include "../../Reflection/Property/PropertyBag.h"
#include "../../Reflection/Registry/TypeGUID.h"

#include <string>

namespace ReplayEngine::Core
{
    // 読み込めなかった Component の中身を、そのまま預かるための入れ物。
    //
    // 何を解決するか:
    //   これまでは、ComponentRegistry に登録されていない型が Scene に含まれていると
    //   読み飛ばされていた。読み飛ばした Scene を保存し直すと、その Component は
    //   ファイルからも消える。
    //
    //   C++ だけで開発している間は「型を消したのだから消えて当然」で済んでいたが、
    //   C# Script を載せると話が変わる。
    //     - Script が Compile できていない
    //     - Assembly の読み込みに失敗した
    //     - 別ブランチにしか無い Script を含む Scene を開いた
    //   どれも一時的な状態でしかないのに、Scene を開いて保存しただけで
    //   設定値が全部消えることになる。実質的なデータ破壊になる。
    //
    // 何をするか:
    //   型が見つからなかった場合、この Component を代わりに作り、
    //   元の型名・型 GUID・モジュール・バージョン・プロパティを丸ごと保持する。
    //   保存時は「元の型として」書き戻すため、ファイルの内容は往復しても変わらない。
    //   型が使えるようになれば、次に読み込んだ時点で本来の Component として復元される。
    //
    // 動作について:
    //   仮想関数を 1 つも override しない。
    //   Update も FixedUpdate も Trigger も届かないので、
    //   壊れた型が中途半端に動くことはない。
    //   Scene の更新ループには乗るが、実行されるのは何もしない基底実装だけ。
    class MissingComponent final : public Component
    {
        REPLAY_COMPONENT_BODY(MissingComponent)

    public:
        // 読み込めなかった Component の元データ。
        struct Record
        {
            // 元の型名。Editor の表示に使う。
            std::string type_name;

            // 元の型 GUID。設定されていれば、こちらが復元の主キーになる。
            Reflection::TypeGUID type_guid;

            // どのモジュールが欠けているか。"Game.Behaviours" など。
            std::string module_id;

            // 元のデータ形式バージョン。0 なら未記録。
            int type_version = 0;

            // 元のプロパティ。中身を解釈せずそのまま保持する。
            Reflection::PropertyBag properties;
        };

        const Record& Original() const noexcept { return original_; }
        Record& MutableOriginal() noexcept { return original_; }

        void SetOriginal(Record record) { original_ = std::move(record); }

        // Editor の表示用。"Door (Missing)" のような形にする。
        std::string DescribeMissingType() const;

        // 読み込めなかった理由を人が読める形で返す。Inspector の詳細表示に使う。
        std::string DescribeReason() const;

    private:
        Record original_;
    };
}
