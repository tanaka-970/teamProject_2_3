#include "ModelEffectStackComponent.h"

// 共通実装は Rendering/EffectStackComponentCommon.inl に集約。
// 型の公開 API・メンバ配置・保存形式は変更しない。
#define REPLAY_EFFECT_STACK_COMPONENT_TYPE ModelEffectStackComponent
#include "MeshRendererComponent.h"
#include "SkinnedMeshRendererComponent.h"
#include "PrimitiveMeshRendererComponent.h"
#include "../../Object/GameObject/GameObject.h"

#define REPLAY_EFFECT_STACK_HAS_CAPTURE_BACKDROP 0
#define REPLAY_EFFECT_STACK_HAS_TARGET_SLOT 1
#include "EffectStackComponentCommon.inl"
#undef REPLAY_EFFECT_STACK_HAS_TARGET_SLOT
#undef REPLAY_EFFECT_STACK_HAS_CAPTURE_BACKDROP
#undef REPLAY_EFFECT_STACK_COMPONENT_TYPE
