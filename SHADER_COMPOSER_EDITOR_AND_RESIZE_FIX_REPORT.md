# Shader Composer generated-HLSL + resize D3D11 fix

## 1. Generated HLSL / Visual Studio errors

Generated files live under `Shader/Materials/Generated` or `Shader/Layers/Generated`.
The runtime compiler had custom include search paths and injected the Material schema before
compilation, so runtime validation passed. Visual Studio opens the generated `.hlsl` directly,
therefore it could not resolve `static_mesh.hlsli` / `frame_common.hlsli`, `VS_OUT`, or generated
Material symbols such as `BaseColor` / `BaseMap`.

Fix:
- generated includes are now relative (`../../static_mesh.hlsli`, etc.)
- generated HLSL carries a standalone b9/t40+ schema fallback
- runtime `ShaderConstantPacker` defines `REPLAY_MATERIAL_SCHEMA_INJECTED`, so actual runtime
  compilation skips the fallback and still uses the canonical generated schema
- generated source disables X3568 for RePlayEngine custom pragmas
- composer validation now also performs a standalone compile without schema injection

## 2. D3D11 COPYRESOURCE_INVALIDSOURCE spam after resize

The log showed source `1920x1080` and destination `1600x900` every frame. SSR history was created
at startup size but `resize_back_buffers()` recreated Deferred/Bloom only. After a window resize,
`SsrPass::CaptureHistory()` attempted `CopyResource` from the new Deferred lit texture into the old
SSR history texture.

Fix:
- `resize_back_buffers()` now recreates SSAO, SSR, TAA, and tiled deferred resources at the new size
- SSR also checks resource dimensions/format/sample layout before `CopyResource`; if they differ it
  invalidates history and skips the copy instead of generating a D3D11 error

## Files

- `RePlayEngine/Rendering/ShaderComposer/ShaderComposerGenerator.cpp`
- `RePlayEngine/Rendering/ShaderComposer/ShaderComposerValidation.cpp`
- `RePlayEngine/Rendering/Shaders/ShaderConstantPacker.cpp`
- `RePlayEngine/Rendering/Passes/SsrPass.cpp`
- `Source/app/Runtime/framework.cpp`
