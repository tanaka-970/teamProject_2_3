#include "RuntimeResult.h"

namespace ReplayEngine::Runtime
{
    const char* ToString(RuntimeStatus status) noexcept
    {
        switch (status)
        {
        case RuntimeStatus::Ok:                        return "Ok";
        case RuntimeStatus::InvalidHandle:             return "InvalidHandle";
        case RuntimeStatus::WrongWorld:                return "WrongWorld";
        case RuntimeStatus::ObjectDestroyed:           return "ObjectDestroyed";
        case RuntimeStatus::ComponentDestroyed:        return "ComponentDestroyed";
        case RuntimeStatus::ComponentNotFound:         return "ComponentNotFound";
        case RuntimeStatus::TypeMismatch:              return "TypeMismatch";
        case RuntimeStatus::AssetMissing:              return "AssetMissing";
        case RuntimeStatus::InvalidAssetType:          return "InvalidAssetType";
        case RuntimeStatus::SceneMissing:              return "SceneMissing";
        case RuntimeStatus::SceneLoadFailed:           return "SceneLoadFailed";
        case RuntimeStatus::TransitionInProgress:      return "TransitionInProgress";
        case RuntimeStatus::UnsupportedOperation:      return "UnsupportedOperation";
        case RuntimeStatus::ServiceUnavailable:        return "ServiceUnavailable";
        case RuntimeStatus::InvalidArgument:           return "InvalidArgument";
        case RuntimeStatus::DeferredOperationRejected: return "DeferredOperationRejected";
        case RuntimeStatus::SaveSlotNotFound:          return "SaveSlotNotFound";
        case RuntimeStatus::SaveKeyNotFound:           return "SaveKeyNotFound";
        case RuntimeStatus::SaveTypeMismatch:          return "SaveTypeMismatch";
        case RuntimeStatus::SaveCorrupt:               return "SaveCorrupt";
        case RuntimeStatus::SaveIOFailure:             return "SaveIOFailure";
        case RuntimeStatus::ComponentDependencyMissing:return "ComponentDependencyMissing";
        case RuntimeStatus::ComponentHasDependents:    return "ComponentHasDependents";
        }
        return "UnknownStatus";
    }

    const char* DescribeJapanese(RuntimeStatus status) noexcept
    {
        switch (status)
        {
        case RuntimeStatus::Ok:
            return u8"成功";
        case RuntimeStatus::InvalidHandle:
            return u8"空、または未初期化の Handle です。";
        case RuntimeStatus::WrongWorld:
            return u8"別の World で作られた Handle です。Scene を切り替える前に取得した参照の可能性があります。";
        case RuntimeStatus::ObjectDestroyed:
            return u8"GameObject が破棄済み、または削除予約済みです。";
        case RuntimeStatus::ComponentDestroyed:
            return u8"Component が破棄済み、または削除予約済みです。";
        case RuntimeStatus::ComponentNotFound:
            return u8"指定した Component が見つかりません。";
        case RuntimeStatus::TypeMismatch:
            return u8"期待した型と実際の型が違います。";
        case RuntimeStatus::AssetMissing:
            return u8"Asset が見つかりません。";
        case RuntimeStatus::InvalidAssetType:
            return u8"Asset の種類が想定と違います。";
        case RuntimeStatus::SceneMissing:
            return u8"Scene が見つかりません。";
        case RuntimeStatus::SceneLoadFailed:
            return u8"Scene の読み込みに失敗しました。";
        case RuntimeStatus::TransitionInProgress:
            return u8"Scene の遷移中です。";
        case RuntimeStatus::UnsupportedOperation:
            return u8"この操作には対応していません。";
        case RuntimeStatus::ServiceUnavailable:
            return u8"必要な Service が利用できません。まだ実装されていないか、接続されていません。";
        case RuntimeStatus::InvalidArgument:
            return u8"引数が不正です。";
        case RuntimeStatus::DeferredOperationRejected:
            return u8"遅延実行の要求が受け付けられませんでした。";
        case RuntimeStatus::SaveSlotNotFound:
            return u8"指定したセーブスロットが見つかりません。";
        case RuntimeStatus::SaveKeyNotFound:
            return u8"指定したセーブキーが見つかりません。";
        case RuntimeStatus::SaveTypeMismatch:
            return u8"セーブ値の型が要求と一致しません。";
        case RuntimeStatus::SaveCorrupt:
            return u8"セーブデータが壊れています。";
        case RuntimeStatus::SaveIOFailure:
            return u8"セーブデータの読み書きに失敗しました。";
        case RuntimeStatus::ComponentDependencyMissing:
            return u8"必須Componentの解決に失敗しました。";
        case RuntimeStatus::ComponentHasDependents:
            return u8"他のComponentが必須依存しているため削除できません。";
        }
        return u8"未知の状態です。";
    }
}
