# RePlayEngine – Four Convenience Features Pack

## Scope

This patch implements the first practical version of four editor/runtime convenience features:

1. **Scene Memo / Editor Annotation**
2. **Play From Here / Play From Checkpoint**
3. **Runtime Raycast + existing Editor Picking integration**
4. **Scene Flow asset + runtime-managed transition conditions**

The source baseline used for this patch is the current `ui` tree with Shader Composer V1, the 1812 scalar-splat fix, and the Editor/Resize fix applied. The provisional Shader Composer touchpad-navigation patch is intentionally **not** included.

---

## 1. Scene Memo / Editor Annotation

### Added

- New built-in component: `Scene Note` (`EditorNoteComponent`)
- Text, category, priority, completed state, viewport visibility, hide-when-completed, and display offset
- Categories: TODO / BUG / ART / PROGRAM / LEVEL / IDEA
- Priorities: Low / Normal / High / Critical
- Scene View overlay rendered as readable 2D editor labels projected from world positions
- Scene Notes panel with open/total count, completion checkbox, selection and camera focus
- Add a free-standing memo from the Scene View context menu
- Add a memo directly to the currently selected GameObject so the memo follows that object
- Notes persist through the normal component/property scene serializer
- `Scene Note` is registered as **EditorOnly** and is skipped when SceneData is instantiated into a runtime Scene

### Editor usage

- Scene View right click -> `Add Scene Memo Here`
- Window -> `シーンメモ`
- Or select a GameObject and use `選択GameObjectにメモ追加` in the Scene Notes panel
- Edit the memo fields through the normal Inspector

### v1 limitation

A note-only host GameObject can still exist as an empty GameObject in the runtime snapshot; the EditorOnly note component itself is not instantiated in runtime.

---

## 2. Play From Here

### Added

Scene View right-click actions:

- `Play From Here`
- `Play From Here + Camera Direction`
- `Play From Camera`
- `Play From Checkpoint -> ...`

The clicked world point is obtained by raycasting the editor collision world. If no collider is hit, the editor y=0 grid is used as fallback, then a short forward-ray fallback is used so the command does not silently fail in an empty scene.

The controlled GameObject is moved only in the **runtime SceneData snapshot**, before runtime `OnAwake` / `OnStart`. The edit Scene is immediately restored and is not dirtied by Play From Here.

Collider bottom clearance is used to raise the controlled object slightly above the selected surface. Checkpoint mode reuses the existing `CheckpointComponent` position offset and rotation.

### v1 limitation

Play From Here uses the current Scene controlled object. Selecting a completely different Player prefab for one run is not included in v1.

---

## 3. Runtime Raycast + Editor Picking

### Native/runtime

`IPhysicsQueryService` and `SceneCollisionWorld` now expose raycast queries based on the existing collision-query path.

`RaycastHit` contains:

- hit point
- hit normal
- distance
- collision source / owning object information
- validity

`RuntimeContext::Raycast(...)` supports:

- origin
- direction
- max distance
- query layer
- mask
- ignored ObjectHandle

A miss is a successful query with `hit.valid == false`; missing physics service is `ServiceUnavailable`.

### C#

`ScriptRuntimeContext` now exposes:

```csharp
RuntimeResult<RaycastHit> Raycast(
    Vector3 origin,
    Vector3 direction,
    float maxDistance = 1000.0f,
    int layer = 0,
    int mask = -1,
    ObjectHandle ignore = default);
```

Example:

```csharp
var result = Runtime.Raycast(origin, direction, 100.0f);
if (result.Succeeded && result.Value.Valid)
{
    ObjectHandle target = result.Value.Object;
    Vector3 point = result.Value.Point;
}
```

### Editor selection

The existing `ViewportPicker` is intentionally retained for editor object selection. It already checks collider AABBs and then falls back to object picking, so collider-less GameObjects remain selectable. Runtime gameplay raycasts stay physics/collision based instead of forcing editor selection semantics into the runtime API.

### v1 limitation

A public gameplay `ScreenPoint -> Camera Ray` helper is not added yet because the current managed runtime input/camera API is not finalized. The lower-level generic raycast API is available now.

---

## 4. Scene Flow

### Asset

New asset extension:

`*.replaysceneflow`

New AssetKind:

`SceneFlow`

The Project Browser can create and open Scene Flow assets. The editor is a form/list editor in v1; it is intentionally not another node graph yet.

A transition contains:

- stable transition ID
- enabled flag
- priority
- From Scene GUID (`empty = any scene`)
- event name
- destination Scene GUID
- zero or more AND conditions

Supported v1 condition types:

- Bool
- Int
- Float

Supported comparison operations:

- `==`
- `!=`
- `<`
- `<=`
- `>`
- `>=`

If multiple transitions match the same event, the highest priority wins; equal priority preserves asset order.

### Project Settings

ProjectSettings serialization is upgraded to version 3 and now stores an **Active Scene Flow GUID**. v1 and v2 ProjectSettings remain readable.

The Scene Flow editor can mark the opened asset active, and Project Settings also has an Active Scene Flow selector.

### Runtime

`SceneFlowService` loads the active asset and owns Bool/Int/Float flow variables. A matched transition uses the existing Scene Flow `LoadScene` path, so existing current-scene tracking, history, reload, and return-to-previous behavior remain unified.

### C#

```csharp
Runtime.SetSceneFlowInt("Keys", 3);
Runtime.TriggerSceneFlow("StartGame");
```

Also available:

```csharp
Runtime.SetSceneFlowBool(...);
Runtime.SetSceneFlowFloat(...);
```

This lets gameplay code publish **what happened** while the destination and conditions stay editable in the engine.

---

## Compatibility / persistence decisions

- `AssetKind::SceneFlow` was appended after existing asset kinds to avoid changing old persisted integer values.
- ProjectSettings v1/v2 migration remains supported; v3 adds Scene Flow.
- New `IPhysicsQueryService` raycast functions and `ISceneFlow` flow-event functions are default non-pure virtual methods so existing mocks/adapters are not forced to implement them immediately.
- Scene Notes reuse the existing ComponentRegistry / PropertyRegistry / SceneData serialization path instead of adding a separate scene-note file format.
- Scene Flow transitions use Asset GUIDs rather than scene names.

---

## Local verification performed in the patch environment

Passed:

- C++17 `-Wall -Wextra -Wpedantic -fsyntax-only` for the new Scene Flow asset/service, ProjectSettings serializer, AssetCache, RuntimeContext, component registration, SceneData EditorOnly handling, Scene Flow validation, Behaviour validation, and collision-world raycast sources (with DirectX stubs where needed)
- Scene Flow + ProjectSettings standalone save/load/migration round-trip: `ROUNDTRIP_OK`
- Native/C# NativeApi table order check: 23 fields, exact order match
- `.vcxproj` and `.vcxproj.filters` XML parse

A real MSVC + Windows SDK + Direct3D + ImGui build cannot be run in this Linux patch environment, so the normal Windows Debug x64 build remains the authoritative integration check.

---

## Recommended Windows checks

Build the normal **Debug x64** target first. Then run the existing validation commands with `start /wait` and record `%ERRORLEVEL%`:

```bat
start /wait "" .\x64\Debug\3dgp.exe --validate-serialization
set SERIALIZATION=%ERRORLEVEL%

start /wait "" .\x64\Debug\3dgp.exe --validate-runtime-api
set RUNTIME_API=%ERRORLEVEL%

start /wait "" .\x64\Debug\3dgp.exe --validate-collision
set COLLISION=%ERRORLEVEL%

start /wait "" .\x64\Debug\3dgp.exe --validate-scene-flow
set SCENE_FLOW=%ERRORLEVEL%

start /wait "" .\x64\Debug\3dgp.exe --validate-editor-integration
set EDITOR=%ERRORLEVEL%

start /wait "" .\x64\Debug\3dgp.exe --validate-csharp-scripting
set CSHARP=%ERRORLEVEL%

start /wait "" .\x64\Debug\3dgp.exe --validate-shader-composer
set SHADER_COMPOSER=%ERRORLEVEL%

echo SERIALIZATION=%SERIALIZATION%
echo RUNTIME_API=%RUNTIME_API%
echo COLLISION=%COLLISION%
echo SCENE_FLOW=%SCENE_FLOW%
echo EDITOR=%EDITOR%
echo CSHARP=%CSHARP%
echo SHADER_COMPOSER=%SHADER_COMPOSER%
```

Expected: all `0`.

### Manual editor smoke test

1. Open a Scene.
2. Right click in Scene View -> `Add Scene Memo Here`; edit text in Inspector; save/reopen Scene.
3. Open Window -> `シーンメモ`; click a memo and confirm camera focus.
4. Right click in Scene View -> `Play From Here`; confirm the controlled object begins there immediately.
5. Stop Play; confirm the edit Scene position is unchanged.
6. Project Browser -> Create -> Scene Flow; add a transition and condition; Save and Set Active.
7. Trigger the configured event from C# and confirm the destination changes only when conditions match.
8. Exercise C# `Runtime.Raycast` against a known collider and confirm Object/Point/Normal/Distance.

