#include "UIEffectStackComponent.h"

// 共通実装は Rendering/EffectStackComponentCommon.inl に集約。
// 型の公開 API・メンバ配置・保存形式は変更しない。
#define REPLAY_EFFECT_STACK_COMPONENT_TYPE UIEffectStackComponent
#define REPLAY_EFFECT_STACK_HAS_CAPTURE_BACKDROP 1
#include "../Rendering/EffectStackComponentCommon.inl"
#undef REPLAY_EFFECT_STACK_HAS_CAPTURE_BACKDROP
#undef REPLAY_EFFECT_STACK_COMPONENT_TYPE
