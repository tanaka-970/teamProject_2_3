# Word / PowerPoint の資料をまとめて PDF にする。
#
#   python convert.py <ファイルかフォルダ...> [-o 出力フォルダ] [--merge [出力.pdf]] [--skip-existing]
#
# 既定は 1 ファイルにつき 1 個の PDF。--merge を付けると全部を 1 本の PDF に綴じる。
# 変換は Word / PowerPoint 本体を COM で呼ぶので Office が入っている Windows が要る。

import argparse
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PS1 = os.path.join(HERE, "office_to_pdf.ps1")

WORD_EXT = (".doc", ".docx", ".docm", ".rtf")
PPT_EXT = (".ppt", ".pptx", ".pptm")
OFFICE_EXT = WORD_EXT + PPT_EXT
INPUT_EXT = OFFICE_EXT + (".pdf",)


def collect(targets):
    found = []
    for t in targets:
        t = os.path.abspath(t)
        if os.path.isfile(t):
            found.append(t)
            continue
        if not os.path.isdir(t):
            print("見つかりません: %s" % t)
            continue
        hits = []
        for root, _dirs, files in os.walk(t):
            for f in files:
                # ~$ で始まるのは Office が開いている間だけ作る一時ファイル。
                if f.lower().endswith(INPUT_EXT) and not f.startswith("~$"):
                    hits.append(os.path.join(root, f))
        found.extend(sorted(hits))
    seen = set()
    uniq = []
    for p in found:
        key = os.path.normcase(p)
        if key not in seen:
            seen.add(key)
            uniq.append(p)
    return uniq


def decide_output(src, out_dir, used):
    stem = os.path.splitext(os.path.basename(src))[0]
    base = out_dir or os.path.dirname(src)
    dst = os.path.join(base, stem + ".pdf")
    # 別のフォルダから同じ名前が来たら連番を足す。先に出した方を上書きしないため。
    n = 2
    while os.path.normcase(dst) in used:
        dst = os.path.join(base, "%s_%d.pdf" % (stem, n))
        n += 1
    used.add(os.path.normcase(dst))
    return dst


def is_fresh(src, dst):
    return os.path.exists(dst) and os.path.getmtime(dst) >= os.path.getmtime(src)


def run_office(jobs):
    tmp = tempfile.mkdtemp(prefix="doc_to_pdf_")
    list_path = os.path.join(tmp, "list.tsv")
    result_path = os.path.join(tmp, "result.tsv")
    with open(list_path, "w", encoding="utf-8") as f:
        for src, dst in jobs:
            f.write("%s\t%s\n" % (src, dst))
    cmd = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
           "-File", PS1, "-ListPath", list_path, "-ResultPath", result_path]
    r = subprocess.run(cmd, capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    if not os.path.exists(result_path):
        print("PowerShell の起動に失敗しました。")
        print((r.stdout or "")[-1500:])
        print((r.stderr or "")[-1500:])
        return {}
    status = {}
    with open(result_path, "r", encoding="utf-8-sig") as f:
        for line in f:
            line = line.rstrip("\r\n")
            if not line:
                continue
            cols = line.split("\t")
            status[os.path.normcase(cols[1])] = (cols[0], cols[2] if len(cols) > 2 else "")
    shutil.rmtree(tmp, ignore_errors=True)
    return status


def merge(pdfs, out_path):
    try:
        from pypdf import PdfWriter
    except ImportError:
        print("pypdf が要ります: python -m pip install pypdf")
        return False
    writer = PdfWriter()
    for p in pdfs:
        # しおりに元ファイル名を残す。まとめた後でも目当ての資料へ飛べるように。
        writer.append(p, outline_item=os.path.splitext(os.path.basename(p))[0])
    os.makedirs(os.path.dirname(os.path.abspath(out_path)) or ".", exist_ok=True)
    with open(out_path, "wb") as f:
        writer.write(f)
    writer.close()
    return True


def main():
    ap = argparse.ArgumentParser(
        description="Word / PowerPoint の資料をまとめて PDF にする")
    ap.add_argument("inputs", nargs="+", help="ファイルかフォルダ（複数可）")
    ap.add_argument("-o", "--out", default=None, help="出力フォルダ（既定は元ファイルの隣）")
    ap.add_argument("--merge", nargs="?", const="", default=None,
                    help="1 本の PDF にまとめる。出力先を省くと まとめ.pdf")
    ap.add_argument("--skip-existing", action="store_true",
                    help="元より新しい PDF が既にあるものは変換しない")
    args = ap.parse_args()

    sources = collect(args.inputs)
    if not sources:
        print("Word / PowerPoint / PDF が見つかりません。")
        return 1

    out_dir = os.path.abspath(args.out) if args.out else None
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)

    merge_path = None
    if args.merge is not None:
        if args.merge:
            merge_path = os.path.abspath(args.merge)
        else:
            first = os.path.abspath(args.inputs[0])
            base = out_dir or (first if os.path.isdir(first) else os.path.dirname(first))
            merge_path = os.path.join(base, "まとめ.pdf")
        # まとめ先が入力に混ざっていると自分を読みながら書くことになるので外す。
        sources = [s for s in sources if os.path.normcase(s) != os.path.normcase(merge_path)]

    # 先に出力先を全部決める。変換で作る PDF と入力の PDF が重なるのを見分けるため。
    used = set()
    outputs = {}
    for src in sources:
        if not src.lower().endswith(".pdf"):
            outputs[src] = decide_output(src, out_dir, used)

    jobs = []
    order = []
    notes = []
    for src in sources:
        if src.lower().endswith(".pdf"):
            if merge_path is None:
                notes.append("既に PDF なので飛ばす: %s" % src)
                continue
            # 前回の変換結果は今回も作り直すので、入力としては数えない。
            if os.path.normcase(src) not in used:
                order.append(src)
            continue
        dst = outputs[src]
        order.append(dst)
        if args.skip_existing and is_fresh(src, dst):
            notes.append("最新なので飛ばす: %s" % os.path.basename(dst))
            continue
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        jobs.append((src, dst))

    if not order:
        print("対象がありません。")
        return 1

    print("対象 : %d 件（変換 %d 件）" % (len(order), len(jobs)))
    for note in notes:
        print("  --  %s" % note)
    print("")

    failed = []
    if jobs:
        status = run_office(jobs)
        for src, dst in jobs:
            state, message = status.get(os.path.normcase(src), ("NG", "結果が返りませんでした"))
            if state == "OK" and os.path.exists(dst):
                print("  OK  %s" % dst)
            else:
                failed.append(dst)
                print("  NG  %s\n      %s" % (src, message))

    order = [p for p in order if p not in failed and os.path.exists(p)]

    if merge_path is not None:
        if not order:
            print("\nまとめる PDF がありません。")
            return 1
        if not merge(order, merge_path):
            return 1
        print("\nまとめ: %d 件 -> %s" % (len(order), merge_path))

    print("\n完了: %d 件成功 / %d 件失敗" % (len(order), len(failed)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
