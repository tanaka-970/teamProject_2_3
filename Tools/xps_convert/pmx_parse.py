# PMX (MMD) を直接解析する。Blender の mmd_tools アドオンは使わない。
# xps_parse と同じ形（bones, meshes）を返すので xps_to_gltf からそのまま使える。

import struct
import sys
import os


class Reader:
    def __init__(self, data):
        self.d = data
        self.o = 0

    def u8(self):
        v = self.d[self.o]
        self.o += 1
        return v

    def i32(self):
        v = struct.unpack_from("<i", self.d, self.o)[0]
        self.o += 4
        return v

    def f32(self):
        v = struct.unpack_from("<f", self.d, self.o)[0]
        self.o += 4
        return v

    def vec(self, n):
        v = struct.unpack_from("<%df" % n, self.d, self.o)
        self.o += 4 * n
        return v

    def skip(self, n):
        self.o += n

    def index(self, size, signed=True):
        v = int.from_bytes(self.d[self.o:self.o + size], "little", signed=signed)
        self.o += size
        return v


class Bone:
    def __init__(self, name, parent, position):
        self.name = name
        self.parent = parent
        self.position = position


class Mesh:
    def __init__(self):
        self.name = ""
        self.tex = ""
        self.positions = []
        self.normals = []
        self.uvs = []
        self.faces = []
        self.bone_indices = []
        self.bone_weights = []

    def texture(self):
        return self.tex


def parse(path):
    data = open(path, "rb").read()
    r = Reader(data)

    if data[0:4] != b"PMX ":
        raise ValueError("PMX ではありません: %s" % path)
    r.skip(4)
    version = r.f32()
    globals_count = r.u8()
    g = [r.u8() for _ in range(globals_count)]
    encoding = "utf-16-le" if g[0] == 0 else "utf-8"
    add_uv = g[1]
    v_idx, t_idx, m_idx, b_idx, mo_idx, ri_idx = g[2], g[3], g[4], g[5], g[6], g[7]

    def text():
        n = r.i32()
        s = r.d[r.o:r.o + n].decode(encoding, "replace")
        r.o += n
        return s

    model_name = text()
    text()
    text()
    text()

    # ---- 頂点 ----------------------------------------------------------
    vertex_count = r.i32()
    positions = []
    normals = []
    uvs = []
    bone_idx = []
    bone_wgt = []
    for _ in range(vertex_count):
        p = r.vec(3)
        n = r.vec(3)
        uv = r.vec(2)
        r.skip(add_uv * 16)
        t = r.u8()
        bi = [0, 0, 0, 0]
        bw = [0.0, 0.0, 0.0, 0.0]
        if t == 0:                       # BDEF1
            bi[0] = r.index(b_idx)
            bw[0] = 1.0
        elif t == 1:                     # BDEF2
            bi[0] = r.index(b_idx)
            bi[1] = r.index(b_idx)
            w = r.f32()
            bw[0], bw[1] = w, 1.0 - w
        elif t == 2:                     # BDEF4
            for k in range(4):
                bi[k] = r.index(b_idx)
            for k in range(4):
                bw[k] = r.f32()
        elif t == 3:                     # SDEF
            bi[0] = r.index(b_idx)
            bi[1] = r.index(b_idx)
            w = r.f32()
            bw[0], bw[1] = w, 1.0 - w
            r.skip(36)                   # C / R0 / R1 は使わない
        elif t == 4:                     # QDEF
            for k in range(4):
                bi[k] = r.index(b_idx)
            for k in range(4):
                bw[k] = r.f32()
        else:
            raise ValueError("未知のウェイト種別 %d" % t)
        r.skip(4)                        # edge scale
        # MMD は Z が奥向き。xps_to_gltf の conv() を通して Blender の向きになるよう
        # ここで Z を反転し、XPS と同じ並びへ揃える。
        positions.append((p[0], p[1], -p[2]))
        normals.append((n[0], n[1], -n[2]))
        uvs.append(uv)
        bone_idx.append(bi)
        bone_wgt.append(bw)

    # ---- 面 ------------------------------------------------------------
    index_count = r.i32()
    indices = []
    for _ in range(index_count):
        indices.append(r.index(v_idx, signed=False))

    # ---- テクスチャ ------------------------------------------------------
    texture_count = r.i32()
    textures = [text().replace("\\", "/") for _ in range(texture_count)]

    # ---- 材質 ------------------------------------------------------------
    material_count = r.i32()
    meshes = []
    face_cursor = 0
    for _ in range(material_count):
        mat_name = text()
        text()
        r.skip(16 + 12 + 4 + 12)          # diffuse / specular / power / ambient
        r.skip(1)                          # draw flags
        r.skip(16 + 4)                     # edge color / size
        tex_index = r.index(t_idx)
        r.index(t_idx)                     # sphere
        r.skip(1)                          # sphere mode
        shared_toon = r.u8()
        r.skip(1 if shared_toon == 1 else t_idx)
        text()                             # memo
        surface_count = r.i32()

        m = Mesh()
        m.name = mat_name
        m.tex = textures[tex_index] if 0 <= tex_index < len(textures) else ""
        remap = {}
        for i in range(face_cursor, face_cursor + surface_count, 3):
            tri = []
            for k in range(3):
                gi = indices[i + k]
                li = remap.get(gi)
                if li is None:
                    li = len(m.positions)
                    remap[gi] = li
                    m.positions.append(positions[gi])
                    m.normals.append(normals[gi])
                    m.uvs.append(uvs[gi])
                    m.bone_indices.append(bone_idx[gi])
                    m.bone_weights.append(bone_wgt[gi])
                tri.append(li)
            m.faces.append(tuple(tri))
        face_cursor += surface_count
        meshes.append(m)

    # ---- ボーン ----------------------------------------------------------
    bone_count = r.i32()
    bones = []
    for _ in range(bone_count):
        bone_name = text()
        text()
        pos = r.vec(3)
        parent = r.index(b_idx)
        r.skip(4)                          # layer
        flags = struct.unpack_from("<H", r.d, r.o)[0]
        r.o += 2
        if flags & 0x0001:
            r.index(b_idx)                 # tail bone
        else:
            r.skip(12)                     # tail offset
        if flags & (0x0100 | 0x0200):
            r.index(b_idx)
            r.skip(4)
        if flags & 0x0400:
            r.skip(12)                     # fixed axis
        if flags & 0x0800:
            r.skip(24)                     # local axes
        if flags & 0x2000:
            r.skip(4)                      # external parent
        if flags & 0x0020:                 # IK
            r.index(b_idx)
            r.skip(4 + 4)
            link_count = r.i32()
            for _ in range(link_count):
                r.index(b_idx)
                if r.u8():
                    r.skip(24)
        bones.append(Bone(bone_name, parent,
                          (pos[0], pos[1], -pos[2])))

    return bones, meshes


def main():
    if len(sys.argv) < 2:
        print("usage: python pmx_parse.py <入力.pmx>")
        return 1
    bones, meshes = parse(sys.argv[1])
    total_v = sum(len(m.positions) for m in meshes)
    total_f = sum(len(m.faces) for m in meshes)
    print("ボーン %d / 材質 %d / 頂点 %d / 三角形 %d"
          % (len(bones), len(meshes), total_v, total_f))
    keys = []
    for m in meshes:
        k = m.texture() or "none"
        if k not in keys:
            keys.append(k)
    print("テクスチャ単位のスロット数 %d" % len(keys))
    for m in meshes:
        print("    %-24s %-34s %6d 三角"
              % (m.name[:24], os.path.basename(m.texture())[:34], len(m.faces)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
