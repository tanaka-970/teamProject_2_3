# Primitive Mesh Renderer Hotfix

- Added `PrimitiveMeshRendererComponent` (Inspector name: `Primitive Mesh Renderer`).
- Built-in Plane / Cube / Sphere / Capsule / Cylinder / Quad no longer use `MeshRendererComponent` when created from Hierarchy.
- Primitive type is a Component property; no external model file/GUID is required.
- Rendering still reuses the existing `IRenderSubmitter -> RenderItem -> builtin:*` renderer path.
- `Mesh Renderer` remains the renderer for imported/static model assets.
