#include "MotionExpression.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ReplayEngine::Motion
{
    namespace
    {
        constexpr int maximum_parse_depth = 32;

        enum class NodeKind : std::uint8_t
        {
            Constant,
            Variable,
            UnaryPlus,
            UnaryMinus,
            Add,
            Subtract,
            Multiply,
            Divide,
            Modulo,
            Less,
            LessEqual,
            Greater,
            GreaterEqual,
            Equal,
            NotEqual,
            LogicalAnd,
            LogicalOr,
            Conditional,
            Function,
        };

        enum class VariableKind : std::uint8_t
        {
            Value,
            Time,
            RawTime,
            Duration,
            Index,
            Count,
        };

        enum class FunctionKind : std::uint8_t
        {
            Abs,
            Sign,
            Min,
            Max,
            Clamp,
            Floor,
            Ceil,
            Round,
            Sqrt,
            Pow,
            Exp,
            Log,
            Mod,
            Sin,
            Cos,
            Tan,
            Asin,
            Acos,
            Atan,
            Atan2,
            Lerp,
            Step,
            Smoothstep,
            Noise,
            Wiggle,
        };

        struct Node final
        {
            NodeKind kind = NodeKind::Constant;
            double number = 0.0;
            int a = -1;
            int b = -1;
            int c = -1;
            VariableKind variable = VariableKind::Value;
            FunctionKind function = FunctionKind::Abs;
        };

        struct CompiledExpression final
        {
            std::vector<Node> nodes;
            int root = -1;
            std::string error;
        };

        struct EvaluationContext final
        {
            double value = 0.0;
            double time = 0.0;
            double raw_time = 0.0;
            double duration = 0.0;
            int component_index = 0;
            int component_count = 1;
        };

        bool IsSpace(char c) noexcept
        {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        }

        bool IsIdentifierStart(char c) noexcept
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        }

        bool IsIdentifierPart(char c) noexcept
        {
            return IsIdentifierStart(c) || (c >= '0' && c <= '9');
        }

        bool IsEffectivelyEmpty(const std::string& source) noexcept
        {
            for (const char c : source)
            {
                if (!IsSpace(c)) return false;
            }
            return true;
        }

        std::string ErrorAt(std::size_t position, const char* message)
        {
            return std::to_string(position + 1u) + u8" 文字目: " + message;
        }

        class Parser final
        {
        public:
            explicit Parser(const std::string& source) : source_(source) {}

            CompiledExpression Compile()
            {
                CompiledExpression result;
                if (IsEffectivelyEmpty(source_))
                {
                    result.error = ErrorAt(0, u8"式が空です");
                    return result;
                }

                nodes_ = &result.nodes;
                const int root = ParseConditional(0);
                SkipSpaces();
                if (error_.empty() && root >= 0 && position_ != source_.size())
                    SetError(u8"解釈できない文字があります");
                if (!error_.empty())
                {
                    result.nodes.clear();
                    result.error = error_;
                    return result;
                }
                result.root = root;
                return result;
            }

        private:
            int AddNode(const Node& node)
            {
                nodes_->push_back(node);
                return static_cast<int>(nodes_->size()) - 1;
            }

            void SkipSpaces()
            {
                while (position_ < source_.size() && IsSpace(source_[position_])) ++position_;
            }

            bool Match(char c)
            {
                SkipSpaces();
                if (position_ >= source_.size() || source_[position_] != c) return false;
                ++position_;
                return true;
            }

            bool Match(const char* text)
            {
                SkipSpaces();
                std::size_t length = 0;
                while (text[length] != '\0') ++length;
                if (source_.compare(position_, length, text) != 0) return false;
                position_ += length;
                return true;
            }

            void SetError(const char* message)
            {
                if (error_.empty()) error_ = ErrorAt(position_, message);
            }

            bool CheckDepth(int depth)
            {
                if (depth <= maximum_parse_depth) return true;
                SetError(u8"式の入れ子が深すぎます（最大 32 段）");
                return false;
            }

            int ParseConditional(int depth)
            {
                if (!CheckDepth(depth)) return -1;
                int condition = ParseLogicalOr(depth);
                if (condition < 0) return -1;
                if (!Match('?')) return condition;
                const int when_true = ParseConditional(depth + 1);
                if (when_true < 0) return -1;
                if (!Match(':'))
                {
                    SetError(u8"三項演算子の ':' がありません");
                    return -1;
                }
                const int when_false = ParseConditional(depth + 1);
                if (when_false < 0) return -1;
                Node node;
                node.kind = NodeKind::Conditional;
                node.a = condition;
                node.b = when_true;
                node.c = when_false;
                return AddNode(node);
            }

            int ParseLogicalOr(int depth)
            {
                int left = ParseLogicalAnd(depth);
                while (left >= 0 && Match("||"))
                {
                    const int right = ParseLogicalAnd(depth);
                    if (right < 0) return -1;
                    Node node;
                    node.kind = NodeKind::LogicalOr;
                    node.a = left;
                    node.b = right;
                    left = AddNode(node);
                }
                return left;
            }

            int ParseLogicalAnd(int depth)
            {
                int left = ParseEquality(depth);
                while (left >= 0 && Match("&&"))
                {
                    const int right = ParseEquality(depth);
                    if (right < 0) return -1;
                    Node node;
                    node.kind = NodeKind::LogicalAnd;
                    node.a = left;
                    node.b = right;
                    left = AddNode(node);
                }
                return left;
            }

            int ParseEquality(int depth)
            {
                int left = ParseComparison(depth);
                while (left >= 0)
                {
                    NodeKind kind;
                    if (Match("==")) kind = NodeKind::Equal;
                    else if (Match("!=")) kind = NodeKind::NotEqual;
                    else break;
                    const int right = ParseComparison(depth);
                    if (right < 0) return -1;
                    Node node;
                    node.kind = kind;
                    node.a = left;
                    node.b = right;
                    left = AddNode(node);
                }
                return left;
            }

            int ParseComparison(int depth)
            {
                int left = ParseAdditive(depth);
                while (left >= 0)
                {
                    NodeKind kind;
                    if (Match("<=")) kind = NodeKind::LessEqual;
                    else if (Match(">=")) kind = NodeKind::GreaterEqual;
                    else if (Match('<')) kind = NodeKind::Less;
                    else if (Match('>')) kind = NodeKind::Greater;
                    else break;
                    const int right = ParseAdditive(depth);
                    if (right < 0) return -1;
                    Node node;
                    node.kind = kind;
                    node.a = left;
                    node.b = right;
                    left = AddNode(node);
                }
                return left;
            }

            int ParseAdditive(int depth)
            {
                int left = ParseMultiplicative(depth);
                while (left >= 0)
                {
                    NodeKind kind;
                    if (Match('+')) kind = NodeKind::Add;
                    else if (Match('-')) kind = NodeKind::Subtract;
                    else break;
                    const int right = ParseMultiplicative(depth);
                    if (right < 0) return -1;
                    Node node;
                    node.kind = kind;
                    node.a = left;
                    node.b = right;
                    left = AddNode(node);
                }
                return left;
            }

            int ParseMultiplicative(int depth)
            {
                int left = ParseUnary(depth);
                while (left >= 0)
                {
                    NodeKind kind;
                    if (Match('*')) kind = NodeKind::Multiply;
                    else if (Match('/')) kind = NodeKind::Divide;
                    else if (Match('%')) kind = NodeKind::Modulo;
                    else break;
                    const int right = ParseUnary(depth);
                    if (right < 0) return -1;
                    Node node;
                    node.kind = kind;
                    node.a = left;
                    node.b = right;
                    left = AddNode(node);
                }
                return left;
            }

            int ParseUnary(int depth)
            {
                if (!CheckDepth(depth)) return -1;
                if (Match('+'))
                {
                    const int child = ParseUnary(depth + 1);
                    if (child < 0) return -1;
                    Node node;
                    node.kind = NodeKind::UnaryPlus;
                    node.a = child;
                    return AddNode(node);
                }
                if (Match('-'))
                {
                    const int child = ParseUnary(depth + 1);
                    if (child < 0) return -1;
                    Node node;
                    node.kind = NodeKind::UnaryMinus;
                    node.a = child;
                    return AddNode(node);
                }
                return ParsePrimary(depth);
            }

            int ParsePrimary(int depth)
            {
                if (!CheckDepth(depth)) return -1;
                SkipSpaces();
                if (position_ >= source_.size())
                {
                    SetError(u8"式が途中で終わっています");
                    return -1;
                }

                if (Match('('))
                {
                    const int value = ParseConditional(depth + 1);
                    if (value < 0) return -1;
                    if (!Match(')'))
                    {
                        SetError(u8"閉じ括弧がありません");
                        return -1;
                    }
                    return value;
                }

                if ((source_[position_] >= '0' && source_[position_] <= '9') ||
                    source_[position_] == '.')
                    return ParseNumber();
                if (IsIdentifierStart(source_[position_])) return ParseIdentifier(depth + 1);

                SetError(u8"値または関数が必要です");
                return -1;
            }

            int ParseNumber()
            {
                SkipSpaces();
                const std::size_t start = position_;
                double integer = 0.0;
                bool has_digit = false;
                while (position_ < source_.size() && source_[position_] >= '0' &&
                    source_[position_] <= '9')
                {
                    has_digit = true;
                    integer = integer * 10.0 + static_cast<double>(source_[position_] - '0');
                    ++position_;
                }

                double fraction = 0.0;
                double scale = 1.0;
                if (position_ < source_.size() && source_[position_] == '.')
                {
                    ++position_;
                    while (position_ < source_.size() && source_[position_] >= '0' &&
                        source_[position_] <= '9')
                    {
                        has_digit = true;
                        scale *= 0.1;
                        fraction += static_cast<double>(source_[position_] - '0') * scale;
                        ++position_;
                    }
                }
                if (!has_digit)
                {
                    position_ = start;
                    SetError(u8"数値が不正です");
                    return -1;
                }

                int exponent = 0;
                bool exponent_negative = false;
                if (position_ < source_.size() &&
                    (source_[position_] == 'e' || source_[position_] == 'E'))
                {
                    ++position_;
                    if (position_ < source_.size() &&
                        (source_[position_] == '+' || source_[position_] == '-'))
                    {
                        exponent_negative = source_[position_] == '-';
                        ++position_;
                    }
                    const std::size_t exponent_start = position_;
                    while (position_ < source_.size() && source_[position_] >= '0' &&
                        source_[position_] <= '9')
                    {
                        exponent = exponent * 10 + (source_[position_] - '0');
                        if (exponent > 308) exponent = 308;
                        ++position_;
                    }
                    if (position_ == exponent_start)
                    {
                        SetError(u8"指数部の数値がありません");
                        return -1;
                    }
                }

                double value = integer + fraction;
                if (exponent != 0)
                {
                    const double multiplier = std::pow(10.0,
                        exponent_negative ? -exponent : exponent);
                    value *= multiplier;
                }
                Node node;
                node.kind = NodeKind::Constant;
                node.number = value;
                return AddNode(node);
            }

            bool ResolveVariable(const std::string& name, VariableKind& out) const noexcept
            {
                if (name == "v") out = VariableKind::Value;
                else if (name == "t") out = VariableKind::Time;
                else if (name == "rt") out = VariableKind::RawTime;
                else if (name == "d") out = VariableKind::Duration;
                else if (name == "i") out = VariableKind::Index;
                else if (name == "n") out = VariableKind::Count;
                else return false;
                return true;
            }

            bool ResolveFunction(const std::string& name, FunctionKind& out,
                int& argument_count) const noexcept
            {
                argument_count = 1;
                if (name == "abs") out = FunctionKind::Abs;
                else if (name == "sign") out = FunctionKind::Sign;
                else if (name == "floor") out = FunctionKind::Floor;
                else if (name == "ceil") out = FunctionKind::Ceil;
                else if (name == "round") out = FunctionKind::Round;
                else if (name == "sqrt") out = FunctionKind::Sqrt;
                else if (name == "exp") out = FunctionKind::Exp;
                else if (name == "log") out = FunctionKind::Log;
                else if (name == "sin") out = FunctionKind::Sin;
                else if (name == "cos") out = FunctionKind::Cos;
                else if (name == "tan") out = FunctionKind::Tan;
                else if (name == "asin") out = FunctionKind::Asin;
                else if (name == "acos") out = FunctionKind::Acos;
                else if (name == "atan") out = FunctionKind::Atan;
                else if (name == "noise") out = FunctionKind::Noise;
                else if (name == "min") { out = FunctionKind::Min; argument_count = 2; }
                else if (name == "max") { out = FunctionKind::Max; argument_count = 2; }
                else if (name == "pow") { out = FunctionKind::Pow; argument_count = 2; }
                else if (name == "mod") { out = FunctionKind::Mod; argument_count = 2; }
                else if (name == "atan2") { out = FunctionKind::Atan2; argument_count = 2; }
                else if (name == "step") { out = FunctionKind::Step; argument_count = 2; }
                else if (name == "wiggle") { out = FunctionKind::Wiggle; argument_count = 2; }
                else if (name == "clamp") { out = FunctionKind::Clamp; argument_count = 3; }
                else if (name == "lerp") { out = FunctionKind::Lerp; argument_count = 3; }
                else if (name == "smoothstep") { out = FunctionKind::Smoothstep; argument_count = 3; }
                else return false;
                return true;
            }

            int ParseIdentifier(int depth)
            {
                SkipSpaces();
                const std::size_t start = position_;
                while (position_ < source_.size() && IsIdentifierPart(source_[position_]))
                    ++position_;
                const std::string name = source_.substr(start, position_ - start);

                SkipSpaces();
                if (position_ >= source_.size() || source_[position_] != '(')
                {
                    VariableKind variable;
                    if (!ResolveVariable(name, variable))
                    {
                        error_ = ErrorAt(start, u8"未知の変数です");
                        return -1;
                    }
                    Node node;
                    node.kind = NodeKind::Variable;
                    node.variable = variable;
                    return AddNode(node);
                }

                FunctionKind function;
                int expected_arguments = 0;
                if (!ResolveFunction(name, function, expected_arguments))
                {
                    error_ = ErrorAt(start, u8"未知の関数です");
                    return -1;
                }
                ++position_;

                int args[3]{ -1, -1, -1 };
                int argument_count = 0;
                SkipSpaces();
                if (position_ < source_.size() && source_[position_] != ')')
                {
                    while (argument_count < 3)
                    {
                        args[argument_count] = ParseConditional(depth + 1);
                        if (args[argument_count] < 0) return -1;
                        ++argument_count;
                        if (!Match(',')) break;
                    }
                }
                if (!Match(')'))
                {
                    SetError(u8"関数の閉じ括弧がありません");
                    return -1;
                }
                if (argument_count != expected_arguments)
                {
                    error_ = ErrorAt(start, u8"関数の引数の数が違います");
                    return -1;
                }

                Node node;
                node.kind = NodeKind::Function;
                node.function = function;
                node.a = args[0];
                node.b = args[1];
                node.c = args[2];
                return AddNode(node);
            }

            const std::string& source_;
            std::size_t position_ = 0;
            std::vector<Node>* nodes_ = nullptr;
            std::string error_;
        };

        std::unordered_map<std::string, CompiledExpression>& ExpressionCache()
        {
            static std::unordered_map<std::string, CompiledExpression> cache;
            return cache;
        }

        const CompiledExpression& CompileCached(const std::string& source)
        {
            auto& cache = ExpressionCache();
            const auto found = cache.find(source);
            if (found != cache.end()) return found->second;
            CompiledExpression compiled = Parser(source).Compile();
            const auto inserted = cache.emplace(source, std::move(compiled));
            return inserted.first->second;
        }

        double VariableValue(VariableKind variable, const EvaluationContext& context) noexcept
        {
            switch (variable)
            {
            case VariableKind::Value: return context.value;
            case VariableKind::Time: return context.time;
            case VariableKind::RawTime: return context.raw_time;
            case VariableKind::Duration: return context.duration;
            case VariableKind::Index: return static_cast<double>(context.component_index);
            case VariableKind::Count: return static_cast<double>(context.component_count);
            }
            return 0.0;
        }

        double EvaluateNode(const CompiledExpression& expression, int index,
            const EvaluationContext& context, int depth) noexcept
        {
            if (index < 0 || index >= static_cast<int>(expression.nodes.size()) ||
                depth > maximum_parse_depth + 4)
                return (std::numeric_limits<double>::quiet_NaN)();

            const Node& node = expression.nodes[static_cast<std::size_t>(index)];
            if (node.kind == NodeKind::Constant) return node.number;
            if (node.kind == NodeKind::Variable) return VariableValue(node.variable, context);
            if (node.kind == NodeKind::UnaryPlus)
                return EvaluateNode(expression, node.a, context, depth + 1);
            if (node.kind == NodeKind::UnaryMinus)
                return -EvaluateNode(expression, node.a, context, depth + 1);

            if (node.kind == NodeKind::LogicalAnd)
            {
                const double left = EvaluateNode(expression, node.a, context, depth + 1);
                if (left == 0.0) return 0.0;
                const double right = EvaluateNode(expression, node.b, context, depth + 1);
                return right != 0.0 ? 1.0 : 0.0;
            }
            if (node.kind == NodeKind::LogicalOr)
            {
                const double left = EvaluateNode(expression, node.a, context, depth + 1);
                if (left != 0.0) return 1.0;
                const double right = EvaluateNode(expression, node.b, context, depth + 1);
                return right != 0.0 ? 1.0 : 0.0;
            }
            if (node.kind == NodeKind::Conditional)
            {
                const double condition = EvaluateNode(expression, node.a, context, depth + 1);
                return EvaluateNode(expression, condition != 0.0 ? node.b : node.c,
                    context, depth + 1);
            }

            const double a = EvaluateNode(expression, node.a, context, depth + 1);
            const double b = node.b >= 0
                ? EvaluateNode(expression, node.b, context, depth + 1) : 0.0;
            const double c = node.c >= 0
                ? EvaluateNode(expression, node.c, context, depth + 1) : 0.0;

            switch (node.kind)
            {
            case NodeKind::Add: return a + b;
            case NodeKind::Subtract: return a - b;
            case NodeKind::Multiply: return a * b;
            case NodeKind::Divide: return a / b;
            case NodeKind::Modulo: return std::fmod(a, b);
            case NodeKind::Less: return a < b ? 1.0 : 0.0;
            case NodeKind::LessEqual: return a <= b ? 1.0 : 0.0;
            case NodeKind::Greater: return a > b ? 1.0 : 0.0;
            case NodeKind::GreaterEqual: return a >= b ? 1.0 : 0.0;
            case NodeKind::Equal: return a == b ? 1.0 : 0.0;
            case NodeKind::NotEqual: return a != b ? 1.0 : 0.0;
            case NodeKind::Function:
                break;
            default:
                return (std::numeric_limits<double>::quiet_NaN)();
            }

            switch (node.function)
            {
            case FunctionKind::Abs: return std::fabs(a);
            case FunctionKind::Sign: return a > 0.0 ? 1.0 : (a < 0.0 ? -1.0 : 0.0);
            case FunctionKind::Min: return (std::min)(a, b);
            case FunctionKind::Max: return (std::max)(a, b);
            case FunctionKind::Clamp: return (std::max)(b, (std::min)(c, a));
            case FunctionKind::Floor: return std::floor(a);
            case FunctionKind::Ceil: return std::ceil(a);
            case FunctionKind::Round: return std::round(a);
            case FunctionKind::Sqrt: return std::sqrt(a);
            case FunctionKind::Pow: return std::pow(a, b);
            case FunctionKind::Exp: return std::exp(a);
            case FunctionKind::Log: return std::log(a);
            case FunctionKind::Mod: return std::fmod(a, b);
            case FunctionKind::Sin: return std::sin(a);
            case FunctionKind::Cos: return std::cos(a);
            case FunctionKind::Tan: return std::tan(a);
            case FunctionKind::Asin: return std::asin(a);
            case FunctionKind::Acos: return std::acos(a);
            case FunctionKind::Atan: return std::atan(a);
            case FunctionKind::Atan2: return std::atan2(a, b);
            case FunctionKind::Lerp: return a + (b - a) * c;
            case FunctionKind::Step: return b < a ? 0.0 : 1.0;
            case FunctionKind::Smoothstep:
            {
                if (a == b) return c < a ? 0.0 : 1.0;
                const double x = (std::max)(0.0, (std::min)(1.0, (c - a) / (b - a)));
                return x * x * (3.0 - 2.0 * x);
            }
            case FunctionKind::Noise:
                return static_cast<double>(MotionExpressionEvaluator::ValueNoise(
                    static_cast<float>(a), 1.0f, 0,
                    static_cast<std::uint32_t>(context.component_index)));
            case FunctionKind::Wiggle:
            {
                MotionWiggle wiggle;
                wiggle.frequency = static_cast<float>(a);
                wiggle.seed = 0;
                wiggle.octaves = 1;
                return MotionExpressionEvaluator::WiggleNoise(wiggle,
                    static_cast<float>(context.raw_time),
                    static_cast<std::uint32_t>(context.component_index)) * b;
            }
            }
            return (std::numeric_limits<double>::quiet_NaN)();
        }

        bool EvaluateScalar(const CompiledExpression& compiled, double original,
            float time, float raw_time, float duration, int component_index,
            int component_count, double& out) noexcept
        {
            EvaluationContext context;
            context.value = original;
            context.time = static_cast<double>(time);
            context.raw_time = static_cast<double>(raw_time);
            context.duration = static_cast<double>(duration);
            context.component_index = component_index;
            context.component_count = component_count;
            out = EvaluateNode(compiled, compiled.root, context, 0);
            return std::isfinite(out);
        }

        void AppendError(std::string* error, const std::string& message)
        {
            if (error == nullptr) return;
            if (error->empty()) *error = message;
            else *error += " | " + message;
        }

        void SetRuntimeError(std::string* error, int component_index)
        {
            AppendError(error, u8"式の評価結果が有限値ではありません。成分 " +
                std::to_string(component_index) + u8" は元の値を使います。");
        }

        std::uint32_t Hash32(std::uint32_t value) noexcept
        {
            value ^= value >> 16u;
            value *= 0x7feb352du;
            value ^= value >> 15u;
            value *= 0x846ca68bu;
            value ^= value >> 16u;
            return value;
        }

        std::uint32_t WrappedLattice(double lattice) noexcept
        {
            constexpr double range = 4294967296.0;
            double wrapped = std::fmod(lattice, range);
            if (wrapped < 0.0) wrapped += range;
            return static_cast<std::uint32_t>(wrapped);
        }

        float HashSigned(int seed, std::uint32_t channel,
            std::uint32_t lattice) noexcept
        {
            std::uint32_t value = static_cast<std::uint32_t>(seed);
            value ^= 0x9e3779b9u + channel * 0x85ebca6bu;
            value ^= lattice + 0xc2b2ae35u + (value << 6u) + (value >> 2u);
            const std::uint32_t hashed = Hash32(value);
            return static_cast<float>(hashed) / 4294967295.0f * 2.0f - 1.0f;
        }
    }

    bool MotionExpressionEvaluator::SupportsType(Reflection::PropertyType type) noexcept
    {
        using Reflection::PropertyType;
        return type == PropertyType::Float || type == PropertyType::Double ||
            type == PropertyType::Vector2 || type == PropertyType::Vector3 ||
            type == PropertyType::Vector4 || type == PropertyType::Color;
    }

    bool MotionExpressionEvaluator::Validate(const std::string& source, std::string& error)
    {
        const CompiledExpression& compiled = CompileCached(source);
        error = compiled.error;
        return error.empty();
    }

    float MotionExpressionEvaluator::ValueNoise(float time, float frequency, int seed,
        std::uint32_t channel) noexcept
    {
        const double scaled = static_cast<double>(time) * static_cast<double>(frequency);
        if (!std::isfinite(scaled)) return 0.0f;
        const double lattice = std::floor(scaled);
        const float fraction = static_cast<float>(scaled - lattice);
        const float smooth = fraction * fraction * (3.0f - 2.0f * fraction);
        const float a = HashSigned(seed, channel, WrappedLattice(lattice));
        const float b = HashSigned(seed, channel, WrappedLattice(lattice + 1.0));
        return a + (b - a) * smooth;
    }

    float MotionExpressionEvaluator::WiggleNoise(const MotionWiggle& wiggle, float time,
        std::uint32_t channel) noexcept
    {
        const int octaves = (std::max)(1, (std::min)(4, wiggle.octaves));
        float frequency = (std::max)(0.0f, wiggle.frequency);
        float weight = 1.0f;
        float total = 0.0f;
        for (int octave = 0; octave < octaves; ++octave)
        {
            const std::uint32_t octave_channel = channel +
                static_cast<std::uint32_t>(octave) * 0x9e3779b9u;
            total += ValueNoise(time, frequency, wiggle.seed, octave_channel) * weight;
            frequency *= 2.0f;
            weight *= 0.5f;
        }
        return total;
    }

    bool MotionExpressionEvaluator::Apply(const MotionExpression& expression,
        Reflection::PropertyValue& value, float time, float raw_time,
        float duration, std::string* error)
    {
        if (!expression.enabled || IsEffectivelyEmpty(expression.source)) return true;
        if (!SupportsType(value.Type())) return true;

        const CompiledExpression& compiled = CompileCached(expression.source);
        if (!compiled.error.empty())
        {
            AppendError(error, compiled.error);
            return false;
        }

        bool success = true;
        using Reflection::PropertyType;
        switch (value.Type())
        {
        case PropertyType::Float:
        {
            const float original = value.AsFloat();
            double result = original;
            const bool evaluated = EvaluateScalar(compiled, original, time, raw_time, duration, 0, 1, result);
            const float converted = static_cast<float>(result);
            if (!evaluated || !std::isfinite(converted))
            {
                success = false;
                SetRuntimeError(error, 0);
            }
            else
                value = Reflection::PropertyValue::MakeFloat(static_cast<float>(result));
            break;
        }
        case PropertyType::Double:
        {
            const double original = value.AsDouble();
            double result = original;
            if (!EvaluateScalar(compiled, original, time, raw_time, duration, 0, 1, result))
            {
                success = false;
                SetRuntimeError(error, 0);
            }
            else
                value = Reflection::PropertyValue::MakeDouble(result);
            break;
        }
        case PropertyType::Vector2:
        {
            DirectX::XMFLOAT2 v = value.AsVector2();
            const float original[2]{ v.x, v.y };
            float* target[2]{ &v.x, &v.y };
            for (int i = 0; i < 2; ++i)
            {
                double result = original[i];
                const bool evaluated = EvaluateScalar(compiled, original[i], time, raw_time, duration, i, 2, result);
                const float converted = static_cast<float>(result);
                if (evaluated && std::isfinite(converted))
                    *target[i] = converted;
                else
                {
                    success = false;
                    SetRuntimeError(error, i);
                }
            }
            value = Reflection::PropertyValue::MakeVector2(v);
            break;
        }
        case PropertyType::Vector3:
        {
            DirectX::XMFLOAT3 v = value.AsVector3();
            const float original[3]{ v.x, v.y, v.z };
            float* target[3]{ &v.x, &v.y, &v.z };
            for (int i = 0; i < 3; ++i)
            {
                double result = original[i];
                const bool evaluated = EvaluateScalar(compiled, original[i], time, raw_time, duration, i, 3, result);
                const float converted = static_cast<float>(result);
                if (evaluated && std::isfinite(converted))
                    *target[i] = converted;
                else
                {
                    success = false;
                    SetRuntimeError(error, i);
                }
            }
            value = Reflection::PropertyValue::MakeVector3(v);
            break;
        }
        case PropertyType::Vector4:
        case PropertyType::Color:
        {
            DirectX::XMFLOAT4 v = value.AsVector4();
            const float original[4]{ v.x, v.y, v.z, v.w };
            float* target[4]{ &v.x, &v.y, &v.z, &v.w };
            for (int i = 0; i < 4; ++i)
            {
                double result = original[i];
                const bool evaluated = EvaluateScalar(compiled, original[i], time, raw_time, duration, i, 4, result);
                const float converted = static_cast<float>(result);
                if (evaluated && std::isfinite(converted))
                    *target[i] = converted;
                else
                {
                    success = false;
                    SetRuntimeError(error, i);
                }
            }
            value = value.Type() == PropertyType::Color
                ? Reflection::PropertyValue::MakeColor(v)
                : Reflection::PropertyValue::MakeVector4(v);
            break;
        }
        default:
            break;
        }
        return success;
    }
}
