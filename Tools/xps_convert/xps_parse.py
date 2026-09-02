# XPS / XNALara の Generic_Item.mesh を読む。Blender に依存しない。
#
# 形式（2026-08-27 に実ファイルから解析。490333/490333 バイトで終端一致を確認）
#
#   uint32  boneCount
#   bone*   name(7bit長前置き) / int16 parent / float3 position   ... 位置は絶対座標
#   uint32  meshCount
#   mesh*   name / uint32 uvLayerCount / uint32 textureCount
#           texture*  name / uint32 uvLayerIndex
#           uint32 vertexCount
#           vertex*   float3 position / float3 normal / byte4 color
#                     float2 uv * uvLayerCount
#                     float4 tangent * uvLayerCount
#                     int16 boneIndex * 4 / float weight * 4
#           uint32 faceCount
#           face*     uint32 * 3
#
# uvLayerCount=1 のとき 1 頂点 76 バイト。

import struct


class Reader:
    def __init__(self, data):
        self.d = data
        self.o = 0

    def u32(self):
        v = struct.unpack_from("<I", self.d, self.o)[0]
        self.o += 4
        return v

    def i16(self):
        v = struct.unpack_from("<h", self.d, self.o)[0]
        self.o += 2
        return v

    def f(self, n):
        v = struct.unpack_from("<%df" % n, self.d, self.o)
        self.o += 4 * n
        return v

    def i16n(self, n):
        v = struct.unpack_from("<%dh" % n, self.d, self.o)
        self.o += 2 * n
        return v

    def skip(self, n):
        self.o += n

    def string(self):
        # .NET BinaryWriter と同じ 7bit 可変長の長さ前置き。
        n = 0
        shift = 0
        while True:
            c = self.d[self.o]
            self.o += 1
            n |= (c & 0x7F) << shift
            if not (c & 0x80):
                break
            shift += 7
        v = self.d[self.o:self.o + n]
        self.o += n
        return v.decode("utf-8", "replace")


class Bone:
    __slots__ = ("name", "parent", "position")

    def __init__(self, name, parent, position):
        self.name = name
        self.parent = parent
        self.position = position


class Mesh:
    __slots__ = ("name", "textures", "positions", "normals", "uvs",
                 "bone_indices", "bone_weights", "faces")

    def __init__(self):
        self.name = ""
        self.textures = []
        self.positions = []
        self.normals = []
        self.uvs = []
        self.bone_indices = []
        self.bone_weights = []
        self.faces = []

    def texture(self):
        return self.textures[0] if self.textures else ""


def _parse_from(data, start, has_tangent):
    r = Reader(data)
    r.o = start

    bones = []
    for _ in range(r.u32()):
        bones.append(Bone(r.string(), r.i16(), r.f(3)))

    meshes = []
    for _ in range(r.u32()):
        m = Mesh()
        m.name = r.string()
        uv_layers = r.u32()
        for _ in range(r.u32()):
            m.textures.append(r.string())
            r.u32()
        for _ in range(r.u32()):
            m.positions.append(r.f(3))
            m.normals.append(r.f(3))
            r.skip(4)                       # 頂点カラー
            uv = [r.f(2) for _ in range(uv_layers)]
            m.uvs.append(uv[0] if uv else (0.0, 0.0))
            if has_tangent:
                r.skip(16 * uv_layers)      # 接線。版によって無い
            m.bone_indices.append(r.i16n(4))
            m.bone_weights.append(r.f(4))
        for _ in range(r.u32()):
            m.faces.append(struct.unpack_from("<3I", r.d, r.o))
            r.skip(12)
        meshes.append(m)

    return bones, meshes, len(r.d) - r.o


def _looks_like_bone_table(data, o):
    # boneCount のあと「長さ1バイト + 印字可能な名前 + 親index + 有限な float3」が続くか。
    try:
        n = struct.unpack_from("<I", data, o)[0]
        if not (0 < n < 4096):
            return False
        ln = data[o + 4]
        if not (0 < ln < 64):
            return False
        name = data[o + 5:o + 5 + ln]
        if not all(32 <= c < 127 for c in name):
            return False
        parent = struct.unpack_from("<h", data, o + 5 + ln)[0]
        if not (-1 <= parent < n):
            return False
        pos = struct.unpack_from("<3f", data, o + 7 + ln)
        return all(abs(v) < 1.0e6 for v in pos)
    except Exception:
        return False


# .mesh は先頭からボーン数。.xps は magic 323232 のヘッダと可変長の設定ブロックが
# 前に付く。長さは版で変わるので、ボーン表らしき位置を探してから解析する。
# 接線の有無も版で変わるため、両方試して「終端ぴったり」になったものを採用する。
def parse(path):
    data = open(path, "rb").read()

    starts = []
    if len(data) >= 4 and struct.unpack_from("<I", data, 0)[0] == 323232:
        limit = min(len(data), 65536)
        for o in range(8, limit):
            if _looks_like_bone_table(data, o):
                starts.append(o)
                if len(starts) >= 8:
                    break
    else:
        starts.append(0)

    last = None
    best = None
    for start in starts:
        for has_tangent in (True, False):
            try:
                bones, meshes, rest = _parse_from(data, start, has_tangent)
            except Exception as e:
                last = e
                continue
            if rest < 0:
                continue
            if rest == 0:
                return bones, meshes
            # .xps は末尾に付随ブロックが付くことがある。余りが最小のものを採る。
            if best is None or rest < best[2]:
                best = (bones, meshes, rest)
    if best is not None:
        if best[2] > 65536:
            raise ValueError("末尾の未解釈バイトが多すぎます: %d" % best[2])
        return best[0], best[1]
    raise ValueError("XPS として解釈できません: %s (最後の失敗: %s)" % (path, last))


if __name__ == "__main__":
    import sys
    import os
    bones, meshes = parse(sys.argv[1])
    print("bones  : %d" % len(bones))
    print("meshes : %d" % len(meshes))
    groups = {}
    for m in meshes:
        g = groups.setdefault(m.texture() or "(none)", [0, 0, 0])
        g[0] += 1
        g[1] += len(m.positions)
        g[2] += len(m.faces)
    print("\nテクスチャ別（エンジン側のサブセットになる単位）")
    for t, (n, v, f) in groups.items():
        print("  %-26s mesh=%-3d verts=%-7d tris=%d" % (t, n, v, f))
