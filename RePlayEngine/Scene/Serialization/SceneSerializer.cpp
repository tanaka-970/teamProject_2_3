// SceneSerializer の責務のうち、バージョン判定メッセージだけを持つ。
//
//   SceneSerializer.cpp                … バージョン判定メッセージ（このファイル）
//   SceneSerializerInternal.cpp       … 読み書きで共有する形式ヘルパ
//   SceneSerializerInternal.h          … 分割内部ヘルパと形式定数の宣言
//   SceneSerializerWrite.cpp           … Scene保存形式の書き出し
//   SceneSerializerRead.cpp            … Scene保存形式の読み込み

#include "SceneSerializer.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <istream>
#include <limits>
#include <locale>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

namespace ReplayEngine::Scene::Serialization
{
    using Reflection::PropertyBag;
    using Reflection::PropertyType;
    using Reflection::PropertyValue;


    std::string SceneSerializer::UnsupportedVersionMessage(int version)
    {
        if (version > 0 && version < SceneData::minimum_supported_version)
        {
            return "この Scene ファイルは旧形式 (v" + std::to_string(version) +
                ") です。GameObject / Component 基盤の刷新にともない非対応となりました。"
                "対応しているのは v" + std::to_string(SceneData::minimum_supported_version) +
                " 〜 v" + std::to_string(SceneData::current_version) + " です。";
        }
        if (version > SceneData::current_version)
        {
            return "この Scene ファイルは新しい形式 (v" + std::to_string(version) +
                ") です。このビルドは v" + std::to_string(SceneData::current_version) +
                " までしか読み込めません。";
        }
        return "Scene ファイルのバージョンを判別できません。";
    }
}
