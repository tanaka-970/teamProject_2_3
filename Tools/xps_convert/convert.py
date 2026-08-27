# XPS / XNALara の Generic_Item.mesh をまとめて GLB / FBX へ変換する。
#
#   python convert.py <入力.mesh か フォルダ> [-o 出力フォルダ] [--fbx] [--no-armature]
#
# フォルダを渡すと配下の Generic_Item.mesh を再帰的に全部変換する。
# 出力名は .mesh が置いてあるフォルダ名になる（空白は _ に置換）。

import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "xps_to_gltf.py")

BLENDER_CANDIDATES = [
    r"C:\Program Files (x86)\Steam\steamapps\common\Blender\blender.exe",
    r"C:\Program Files\Blender Foundation\Blender 5.2\blender.exe",
    r"C:\Program Files\Blender Foundation\Blender 4.4\blender.exe",
]


def find_blender():
    env = os.environ.get("BLENDER")
    if env and os.path.exists(env):
        return env
    for p in BLENDER_CANDIDATES:
        if os.path.exists(p):
            return p
    from shutil import which
    p = which("blender")
    if p:
        return p
    return None


def collect(target):
    if os.path.isfile(target):
        return [target]
    found = []
    for root, _dirs, files in os.walk(target):
        for f in files:
            if f.lower().endswith((".mesh", ".xps")):
                found.append(os.path.join(root, f))
    return sorted(found)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("-o", "--out", default=None)
    ap.add_argument("--fbx", action="store_true")
    ap.add_argument("--no-armature", action="store_true")
    args = ap.parse_args()

    blender = find_blender()
    if blender is None:
        print("blender.exe が見つかりません。環境変数 BLENDER で指定してください。")
        return 1

    sources = collect(args.input)
    if not sources:
        print(".mesh / .xps が見つかりません: %s" % args.input)
        return 1

    out_dir = args.out or (args.input if os.path.isdir(args.input)
                           else os.path.dirname(args.input))
    os.makedirs(out_dir, exist_ok=True)
    ext = "fbx" if args.fbx else "glb"

    print("blender : %s" % blender)
    print("対象    : %d 件\n" % len(sources))

    failed = 0
    for src in sources:
        name = os.path.basename(os.path.dirname(src)).replace(" ", "_")
        same_dir_mesh = os.path.join(os.path.dirname(src), "Generic_Item.mesh")
        if src.lower().endswith(".xps") and os.path.exists(same_dir_mesh):
            continue   # 同じフォルダに .mesh があるならそちらを優先する
        dst = os.path.join(out_dir, "%s.%s" % (name, ext))
        cmd = [blender, "--background", "--factory-startup",
               "--python", SCRIPT, "--", src, dst]
        if args.fbx:
            cmd.append("--fbx")
        if args.no_armature:
            cmd.append("--no-armature")
        r = subprocess.run(cmd, capture_output=True, text=True,
                           encoding="utf-8", errors="replace")
        keep = [ln for ln in (r.stdout or "").splitlines()
                if ln.startswith("===") or ln.startswith("    ")
                or ln.startswith("      ")]
        if keep:
            print("\n".join(keep))
        else:
            failed += 1
            print("!!! 失敗: %s" % src)
            print((r.stdout or "")[-1500:])
            print((r.stderr or "")[-1500:])
        print("")

    print("完了: %d 件成功 / %d 件失敗  ->  %s"
          % (len(sources) - failed, failed, out_dir))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
