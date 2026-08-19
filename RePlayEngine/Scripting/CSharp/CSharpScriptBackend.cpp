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

// 分割一覧（CSharpScriptBackend.cpp）:
//   CSharpScriptBackendHostInternal.h       hostfxr／Managed API 型と定数
//   CSharpScriptBackendNativeInternal.h     Native API 共通型・宣言・共有状態
//   CSharpScriptBackendInternal.h           C# 値／スキーマ／パス変換の共通補助
//   CSharpScriptBackendHost.cpp              Backend 初期化・hostfxr 接続
//   CSharpScriptBackendAssembly.cpp          Assembly のロード／アンロード／リロード
//   CSharpScriptBackendInstances.cpp         型・インスタンス・Invoke／Field 操作
//   CSharpScriptBackendNative.cpp            Native API table と共有状態
//   CSharpScriptBackendNativeScene.cpp       Scene／Object／Raycast callback
//   CSharpScriptBackendNativeMotion.cpp      Motion callback
//   CSharpScriptBackendNativeEvents.cpp      Event callback
//   CSharpScriptBackendErrors.cpp            エラー情報取得
