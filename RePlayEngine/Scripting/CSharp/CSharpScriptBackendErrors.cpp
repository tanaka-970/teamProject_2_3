#include "CSharpScriptBackend.h"

#include "../Core/ScriptValue.h"
#include "../../Runtime/API/RuntimeContext.h"
#include "../../Runtime/Core/RuntimeResult.h"
#include "../../Runtime/Events/EventBus.h"

#include <DirectXMath.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstring>
#include <deque>
#include <cstdlib>
#include <iterator>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif
#include "CSharpScriptBackendHostInternal.h"
#include "CSharpScriptBackendInternal.h"
#include "CSharpScriptBackendNativeInternal.h"
namespace ReplayEngine::Scripting::CSharp
{
    using namespace Detail;

// Backend エラー情報取得の関数本体

    void CSharpScriptBackend::RefreshLastError() const
    {
        if (last_error_function_ == nullptr) return;

        std::array<char, text_buffer_size> message{};
        std::array<char, 1024> file{};
        int line = 0;
        reinterpret_cast<last_error_fn>(last_error_function_)(
            message.data(), static_cast<int>(message.size()),
            file.data(), static_cast<int>(file.size()), &line);
        last_error_ = message.data();
        last_error_file_ = file.data();
        last_error_line_ = line;
    }

    void CSharpScriptBackend::SetLastError(std::string message,
        std::string file, int line) const
    {
        last_error_ = std::move(message);
        last_error_file_ = std::move(file);
        last_error_line_ = line;
    }

}
