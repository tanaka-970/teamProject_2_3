# Shader Composer Validation 1812 Fix

## Failure

`--validate-shader-composer` returned `1812`.

Validation code 1812 is:

- generated Surface Shader
- Static variant
- actual D3DCompile / ps_5_0 compilation

The graph used by this check multiplies a scalar Fresnel result by a float4 EmissionColor.

## Root cause

Shader Composer's scalar -> vector conversion emitted HLSL such as:

```hlsl
float4(replay_n7)
```

The DX11 FXC/D3DCompile path is not guaranteed to accept a one-argument numeric vector
constructor as a scalar splat.

This did not affect the default graph because its initial Multiply is float4 * float4.
The validation graph intentionally exercises scalar * vector and exposed the problem.

## Fix

Scalar -> vector conversion now emits explicit components:

```hlsl
float2(x, x)
float3(x, x, x)
float4(x, x, x, x)
```

This keeps the intended scalar-splat semantics and is compatible with Shader Model 5 / FXC.

No Material, ShaderAsset, ShaderCatalog, Layer, Pass, save format, or renderer behavior is changed.
