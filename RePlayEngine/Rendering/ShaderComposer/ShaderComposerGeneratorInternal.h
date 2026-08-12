#pragma once

// これは ShaderComposerGenerator の分割内部で共有する実装であり、外部から使うものではない。

#include "ShaderComposerGenerator.h"

#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace ReplayEngine::Rendering::Detail
{
    struct Value final
    {
        ShaderComposerValueType type = ShaderComposerValueType::Invalid;
        std::string expression;
        bool valid = false;
    };

    int Components(ShaderComposerValueType type);
    const char* TypeName(ShaderComposerValueType type);
    std::string FloatLiteral(float value);
    std::string Escape(const std::string& text);
    bool IsIdentifier(const std::string& name);
    std::string Convert(const Value& value, ShaderComposerValueType target);
    ShaderComposerValueType MergeTypes(ShaderComposerValueType a,
        ShaderComposerValueType b);

    class Generator final
    {
    public:
        explicit Generator(const ShaderComposerAsset& source);
        ShaderComposerGenerateResult Run();

    private:
        Value Input(const ShaderComposerNode& node, std::uint32_t pin,
            const Value& fallback);
        Value Build(std::uint64_t id);
        void AddError(std::uint64_t id, std::string message);
        ShaderComposerGenerateResult Finish(bool ok, std::string hlsl);

        const ShaderComposerAsset& asset;
        std::unordered_map<std::uint64_t, const ShaderComposerConnection*> inputs;
        std::unordered_map<std::uint64_t, Value> memo;
        std::unordered_set<std::uint64_t> visiting;
        std::ostringstream statements;
        std::vector<ShaderComposerDiagnostic> diagnostics;
    };
}
