# Blender の中で走らせる XPS -> GLB / FBX 変換本体。
#
#   blender --background --factory-startup --python xps_to_gltf.py -- <入力.mesh> <出力> [--fbx] [--no-armature]
#
# 直接叩かず convert.bat から使うほうが楽。

import bpy
import os
import sys
import math
import importlib.util
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    "xps_parse", os.path.join(HERE, "xps_parse.py"))
xps_parse = importlib.util.module_from_spec(spec)
spec.loader.exec_module(xps_parse)


# XPS は Y-up、Blender は Z-up。
def conv(p):
    return Vector((p[0], -p[2], p[1]))


def build_armature(bones, name):
    data = bpy.data.armatures.new(name + "_Armature")
    obj = bpy.data.objects.new(name + "_Armature", data)
    bpy.context.collection.objects.link(obj)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode="EDIT")

    heads = [conv(b.position) for b in bones]
    children = {}
    for i, b in enumerate(bones):
        if 0 <= b.parent < len(bones):
            children.setdefault(b.parent, []).append(i)

    edit = []
    for i, b in enumerate(bones):
        e = data.edit_bones.new(b.name)
        e.head = heads[i]
        kids = children.get(i, [])
        if len(kids) == 1:
            tail = heads[kids[0]]
        elif kids:
            tail = sum((heads[k] for k in kids), Vector()) / len(kids)
        elif 0 <= b.parent < len(bones):
            d = heads[i] - heads[b.parent]
            tail = heads[i] + (d.normalized() * max(d.length, 0.01))
        else:
            tail = heads[i] + Vector((0.0, 0.0, 0.05))
        # 長さ 0 のボーンは Blender が捨てるので必ず伸ばす。
        if (tail - heads[i]).length < 1e-4:
            tail = heads[i] + Vector((0.0, 0.0, 0.02))
        e.tail = tail
        edit.append(e)

    for i, b in enumerate(bones):
        if 0 <= b.parent < len(bones):
            edit[i].parent = edit[b.parent]

    bpy.ops.object.mode_set(mode="OBJECT")
    return obj


def build_mesh(name, group, folder, bones, with_weights):
    verts = []
    faces = []
    uvs = []
    weights = {}   # bone index -> [(vertex, weight), ...]
    base = 0
    for m in group:
        for p in m.positions:
            verts.append(conv(p))
        for f in m.faces:
            faces.append((f[0] + base, f[1] + base, f[2] + base))
        for u in m.uvs:
            uvs.append((u[0], 1.0 - u[1]))
        if with_weights:
            for vi in range(len(m.positions)):
                idx = m.bone_indices[vi]
                wgt = m.bone_weights[vi]
                for k in range(4):
                    w = wgt[k]
                    if w <= 0.0:
                        continue
                    bi = idx[k]
                    if 0 <= bi < len(bones):
                        weights.setdefault(bi, []).append((base + vi, w))
        base += len(m.positions)

    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    me.update()

    uv_layer = me.uv_layers.new(name="UVMap")
    for li, loop in enumerate(me.loops):
        uv_layer.data[li].uv = uvs[loop.vertex_index]

    mat = bpy.data.materials.new(name)
    try:
        mat.use_nodes = True
    except Exception:
        pass
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    tex_name = group[0].texture()
    tex_path = os.path.join(folder, tex_name) if tex_name else ""
    if bsdf is not None and tex_path and os.path.exists(tex_path):
        node = mat.node_tree.nodes.new("ShaderNodeTexImage")
        node.image = bpy.data.images.load(tex_path)
        mat.node_tree.links.new(bsdf.inputs["Base Color"], node.outputs["Color"])
        mat.node_tree.links.new(bsdf.inputs["Alpha"], node.outputs["Alpha"])
        mat.blend_method = "CLIP" if hasattr(mat, "blend_method") else mat.blend_method
    me.materials.append(mat)

    obj = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(obj)

    if with_weights:
        for bi, pairs in weights.items():
            vg = obj.vertex_groups.new(name=bones[bi].name)
            for vi, w in pairs:
                vg.add([vi], w, "REPLACE")
    return obj


def main():
    argv = sys.argv[sys.argv.index("--") + 1:]
    src = argv[0]
    out = argv[1]
    use_fbx = "--fbx" in argv
    with_armature = "--no-armature" not in argv

    bones, meshes = xps_parse.parse(src)
    folder = os.path.dirname(src)
    name = os.path.splitext(os.path.basename(out))[0]

    bpy.ops.wm.read_factory_settings(use_empty=True)

    # テクスチャごとにまとめる。これがエンジン側のサブセット単位になる。
    groups = {}
    order = []
    for m in meshes:
        key = m.texture() or "none"
        if key not in groups:
            groups[key] = []
            order.append(key)
        groups[key].append(m)

    armature = build_armature(bones, name) if with_armature else None

    objs = []
    for key in order:
        objs.append(build_mesh(os.path.splitext(key)[0], groups[key],
                               folder, bones, with_armature))

    # 1 オブジェクトへ統合する。エンジンは 1 メッシュの材質数をサブセット数にするため、
    # 分けたままだとサブセットではなく別オブジェクトになってしまう。
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.join()
    mesh_obj = bpy.context.view_layer.objects.active
    mesh_obj.name = name

    if armature is not None:
        mesh_obj.parent = armature
        mod = mesh_obj.modifiers.new("Armature", "ARMATURE")
        mod.object = armature

    print("=== %s" % name)
    print("    bones          : %d" % len(bones))
    print("    material slots : %d" % len(mesh_obj.data.materials))
    for i, m in enumerate(mesh_obj.data.materials):
        print("      slot[%d] %s" % (i, m.name))
    print("    vertices       : %d" % len(mesh_obj.data.vertices))
    print("    triangles      : %d" % len(mesh_obj.data.polygons))
    print("    vertex groups  : %d" % len(mesh_obj.vertex_groups))

    if use_fbx:
        bpy.ops.export_scene.fbx(filepath=out, path_mode="COPY",
                                 embed_textures=True, add_leaf_bones=False)
    else:
        bpy.ops.export_scene.gltf(filepath=out, export_format="GLB")
    print("=== 出力 %s (%d bytes)" % (out, os.path.getsize(out)))


main()
