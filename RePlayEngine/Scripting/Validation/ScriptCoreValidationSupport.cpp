#include "ScriptCoreValidationInternal.h"

namespace ReplayEngine::Scripting::Validation::Detail
{
        void EnsureRegistries()
        {
            // 二重に呼んでも ComponentRegistry が重複を弾く。
            Core::RegisterBuiltInComponents();
        }
}
