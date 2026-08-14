#pragma once

#include "SerializationValidation.h"

#include "../../Object/Component/MissingComponent.h"
#include "../../Object/Registry/BuiltInComponents.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../../Reflection/Registry/PropertyRegistry.h"
#include "../../Scene/Runtime/Scene.h"
#include "../../Scene/Serialization/SceneData.h"
#include "../../Scene/Serialization/SceneSerializer.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// SerializationValidation の分割内部で共有する検証ヘルパであり、外部から使うものではない。
namespace ReplayEngine::Runtime::Validation::Detail::SerializationValidation
{
        using Core::MissingComponent;
        using Core::ObjectID;
        using Reflection::PropertyBag;
        using Reflection::PropertyType;
        using Reflection::PropertyValue;
        using Reflection::TypeGUID;
        namespace Serialization = Scene::Serialization;

        // 検査の記録係。最初の失敗で打ち切らず、全項目を実行してから
        // 最初の失敗番号を返す。1 回のビルド確認で見つかる不具合を増やすため。
        class Checker final
        {
        public:
            explicit Checker(int first_code) : next_code_(first_code) {}

            void Expect(bool condition, const char* what)
            {
                const int code = next_code_++;
                ++total_;
                if (condition) return;

                ++failures_;
                if (first_failure_ == 0) first_failure_ = code;
                std::fprintf(stderr, "  [FAIL %d] %s\n", code, what);
            }

            int Result() const noexcept { return first_failure_; }
            int Total() const noexcept { return total_; }
            int Failures() const noexcept { return failures_; }

            int Report(const char* title, int fallback_code) const
            {
                if (first_failure_ == 0)
                {
                    std::fprintf(stderr, "%s OK: %d checks passed\n", title, total_);
                    return 0;
                }
                std::fprintf(stderr, "%s FAILED: %d/%d checks failed (first=%d)\n",
                    title, failures_, total_, first_failure_);
                return first_failure_ != 0 ? first_failure_ : fallback_code;
            }

        private:
            int next_code_ = 0;
            int first_failure_ = 0;
            int total_ = 0;
            int failures_ = 0;
        };

    bool RoundTrip(const Serialization::SceneData& source,
        Serialization::SceneData& restored, std::string& text, std::string& error);
    Serialization::ComponentData MakeAllTypesComponent();
    bool BagsEqual(const PropertyBag& a, const PropertyBag& b);
    std::string MakeLegacyScene(int version);
}
