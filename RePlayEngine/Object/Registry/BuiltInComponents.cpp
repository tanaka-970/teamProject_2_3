// Built-in Component 登録のうち、公開入口と登録順だけを持つ。
//
//   BuiltInComponents.cpp              … 公開入口と登録順（このファイル）
//   BuiltInComponentsCore.cpp          … Missing / Transform / Pivot / State / Scene
//   BuiltInComponentsRendering.cpp     … Mesh / Light / Post Process / Particle
//   BuiltInComponentsGameplay.cpp      … Character / Player / Stage gameplay
//   BuiltInComponentsScripting.cpp     … Script Component
//   BuiltInComponentsPhysics.cpp       … Rigidbody / Collider / Landscape
//   BuiltInComponentsCameraAudio.cpp   … Camera / Follow Target / Audio
//   BuiltInComponentsUI.cpp            … UI Component
//   BuiltInComponentsMotionEditor.cpp  … Motion / Editor Note
//   BuiltInComponentsInternal.h        … 分割した登録実装だけが共有する宣言
//
// RegisterBuiltInComponents の呼び出し順は Add Component 一覧と Scene 読み込みの
// 前提なので、分割前の順序をそのまま維持する。

#include "BuiltInComponents.h"
#include "BuiltInComponentsInternal.h"

namespace ReplayEngine::Core
{
    using namespace Detail;

    void RegisterBuiltInComponents()
    {
        // 並び順がそのまま Add Component 一覧の並びになる。
        //
        // 新しい Component を足すときは、ここへ 1 行足すだけでよい。
        // それだけで Add Component 一覧・Inspector・Scene 保存・読み込み・
        // 複製・Undo/Redo・Prefab のすべてへ反映される。
        // Missing Component の預かり先を最初に登録する。
        // Scene 読み込み中に型が見つからなかった場合、この型が必ず使える必要がある。
        RegisterMissingComponent();

        RegisterTransform();
        RegisterPivot();
        RegisterMeshRenderer();
        RegisterPrimitiveMeshRenderer();
        RegisterPostProcessVolume();
        RegisterParticleEmitter();
        RegisterLineRenderers();
        RegisterLights();
        RegisterUI();
        RegisterMotion();
        RegisterState();
        RegisterPropertyLink();
        RegisterScenePersistence();
        RegisterSkinnedMeshRenderer();
        RegisterAnimator();
        RegisterRigidbody();
        RegisterSphereCollider();
        RegisterBoxCollider();
        RegisterCapsuleCollider();
        RegisterMeshCollider();
        RegisterLandscape();
        RegisterCharacterMotor();
        RegisterPlayerInput();
        RegisterPlayerController();
        RegisterAudioListener();
        RegisterAudioSource();
        RegisterCamera();
        RegisterFollowTarget();
        RegisterCameraTarget();
        RegisterRotator();
        RegisterHealth();
        RegisterStageGameplay();
        RegisterEditorNote();
        RegisterScript();
    }
}
