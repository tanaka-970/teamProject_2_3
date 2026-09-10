"""PropertyRegistry の内容から C# の Component バインディングを起こす。

手順:
    x64\\Release\\3dgp.exe --dump-component-properties
    python Tools\\generate_component_bindings.py

なぜ要るか:
    Components*.cs を手で書き足していると、C++ 側に増えた Component が
    取り残される。2026-09-04 の時点で 73 型のうち 28 型が C# から触れなかった。
    生成元を PropertyRegistry 一本にすれば、この食い違いは起きない。

既に手で書いてある型には触らない。ComponentsGenerated.cs へ不足分だけ出す。
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DUMP = os.path.join(ROOT, "Saved", "ComponentProperties.txt")
MANAGED = os.path.join(ROOT, "Managed", "RePlayEngine.Managed")
OUTPUT = os.path.join(MANAGED, "ComponentsGenerated.cs")
CATALOG = os.path.join(MANAGED, "ComponentCatalog.cs")

# PropertyType -> (C# の型, Accessor の接尾辞)
# Accessor に窓口が無い型はここへ載せない。載っていないものは出力から落とす。
TYPE_MAP = {
    "bool": ("bool", "Bool"),
    "int": ("int", "Int"),
    "float": ("float", "Float"),
    "double": ("double", "Double"),
    "string": ("string", "String"),
    "vec2": ("Vector2", "Vector2"),
    "vec3": ("Vector3", "Vector3"),
    "vec4": ("Vector4", "Vector4"),
    "color": ("Color", "Color"),
    "objref": ("ObjectReference", "ObjectReference"),
    "compref": ("ComponentReference", "ComponentReference"),
    "colliderref": ("ComponentReference", "ComponentReference"),
    # 内部が int のもの。Inspector では専用の描き方をするが、値は整数。
    "enum": ("int", "Int"),
    "layer": ("int", "Int"),
    "layermask": ("int", "Int"),
    # 内部が string のもの。アセットの GUID が入る。
    "asset": ("string", "String"),
    "assetref": ("string", "String"),
}

# C# の予約語。プロパティ名がぶつかったら @ を付ける。
KEYWORDS = {
    "abstract", "as", "base", "bool", "break", "byte", "case", "catch", "char",
    "checked", "class", "const", "continue", "decimal", "default", "delegate",
    "do", "double", "else", "enum", "event", "explicit", "extern", "false",
    "finally", "fixed", "float", "for", "foreach", "goto", "if", "implicit",
    "in", "int", "interface", "internal", "is", "lock", "long", "namespace",
    "new", "null", "object", "operator", "out", "override", "params", "private",
    "protected", "public", "readonly", "ref", "return", "sbyte", "sealed",
    "short", "sizeof", "stackalloc", "static", "string", "struct", "switch",
    "this", "throw", "true", "try", "typeof", "uint", "ulong", "unchecked",
    "unsafe", "ushort", "using", "virtual", "void", "volatile", "while",
}


def split_tokens(line):
    """std::quoted で書かれた行を、引用符を解いて分ける。"""
    tokens = []
    i = 0
    while i < len(line):
        if line[i].isspace():
            i += 1
            continue
        if line[i] == '"':
            i += 1
            buffer = []
            while i < len(line) and line[i] != '"':
                if line[i] == "\\" and i + 1 < len(line):
                    i += 1
                buffer.append(line[i])
                i += 1
            i += 1
            tokens.append("".join(buffer))
        else:
            start = i
            while i < len(line) and not line[i].isspace():
                i += 1
            tokens.append(line[start:i])
    return tokens


def read_dump(path):
    """dump を読んで [(型名, 表示名, カテゴリ, [(プロパティ名, 型, read_only)])] にする。"""
    components = []
    current = None
    with open(path, encoding="utf-8") as stream:
        for raw in stream:
            line = raw.rstrip("\n").rstrip("\r")
            if line.startswith("COMPONENT "):
                t = split_tokens(line[len("COMPONENT "):])
                current = (t[0], t[1], t[2], [])
            elif line.startswith("PROPERTY ") and current is not None:
                t = split_tokens(line[len("PROPERTY "):])
                current[3].append((t[0], t[1], t[2] == "1"))
            elif line == "END_COMPONENT" and current is not None:
                components.append(current)
                current = None
    return components


def existing_types(directory):
    """既に手で書いてある NativeTypeName を集める。"""
    found = set()
    for name in os.listdir(directory):
        if not name.endswith(".cs"):
            continue
        # 前回の出力を「手書き済み」と数えると、2 回目から何も出なくなる。
        if os.path.abspath(os.path.join(directory, name)) == os.path.abspath(OUTPUT):
            continue
        with open(os.path.join(directory, name), encoding="utf-8-sig") as stream:
            found |= set(re.findall(r'NativeTypeName\s*=>\s*"([^"]+)"', stream.read()))
    return found


def pascal_case(snake):
    # プロパティ名には "__script.asset" のように識別子へ使えない文字が入る。
    # 英数字以外はすべて区切りとして落とす。
    parts = [p for p in re.split(r"[^0-9A-Za-z]+", snake) if p]
    name = "".join(p[:1].upper() + p[1:] for p in parts)
    if not name:
        return ""
    if name[0].isdigit():
        name = "_" + name
    return name


def emit(components):
    lines = []
    for type_name, display, category, properties in components:
        usable = [p for p in properties if p[1] in TYPE_MAP]
        if not usable:
            continue

        lines.append("")
        lines.append("// %s（%s）" % (display, category))
        lines.append("public readonly struct %s : IComponentBinding<%s>"
                     % (type_name, type_name))
        lines.append("{")
        lines.append('    public static string NativeTypeName => "%s";' % type_name)
        lines.append("    public static %s FromHandle(ComponentHandle handle) => new(handle);"
                     % type_name)
        lines.append("")
        lines.append("    private %s(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);"
                     % type_name)
        lines.append("    public ComponentAccessor Accessor { get; }")
        lines.append("    public ComponentHandle Handle => Accessor.Handle;")
        lines.append("    public bool IsValid => Accessor.IsValid;")

        used = set()
        for prop_name, prop_type, read_only in usable:
            cs_type, suffix = TYPE_MAP[prop_type]
            member = pascal_case(prop_name)
            if not member:
                continue
            if member in KEYWORDS:
                member = "@" + member
            # 同じ名前へ落ちるプロパティが 2 つあっても、C# 側は 1 つしか持てない。
            if member in used or member == type_name:
                continue
            used.add(member)

            lines.append("")
            lines.append("    public %s %s" % (cs_type, member))
            lines.append("    {")
            lines.append('        get => Accessor.Get%s("%s");' % (suffix, prop_name))
            if not read_only:
                lines.append('        set => Accessor.Set%s("%s", value);' % (suffix, prop_name))
            lines.append("    }")
        lines.append("}")
    return lines


def emit_catalog(components):
    """C# から「どの Component に何があるか」を引ける一覧を書く。"""
    lines = [
        "using System;",
        "using System.Collections.Generic;",
        "",
        "namespace ReplayEngine;",
        "",
        "// PropertyRegistry から起こした Component の一覧。手で書き換えない。",
        "//",
        "// エンジン側では Validation & Diagnostics の Component API タブに同じものが出る。",
        "// C# からはこう引ける:",
        "//   foreach (var entry in ComponentCatalog.All) ...",
        "//   var motion = ComponentCatalog.Find(\"MotionPlayerComponent\");",
        "//",
        "// 作り直しかた:",
        "//   x64\\Release\\3dgp.exe --dump-component-properties",
        "//   python Tools\\generate_component_bindings.py",
        "",
        "public readonly struct ComponentPropertyEntry",
        "{",
        "    public ComponentPropertyEntry(string name, string type, bool readOnly, bool availableInCSharp)",
        "    {",
        "        Name = name;",
        "        Type = type;",
        "        ReadOnly = readOnly;",
        "        AvailableInCSharp = availableInCSharp;",
        "    }",
        "",
        "    // C++ 側の登録名。ComponentAccessor へ渡すのはこの名前。",
        "    public string Name { get; }",
        "",
        "    // PropertyRegistry の型名（float / vec3 / enum など）。",
        "    public string Type { get; }",
        "    public bool ReadOnly { get; }",
        "",
        "    // ComponentAccessor に窓口があるか。array だけ false。",
        "    public bool AvailableInCSharp { get; }",
        "}",
        "",
        "public readonly struct ComponentCatalogEntry",
        "{",
        "    public ComponentCatalogEntry(string typeName, string displayName, string category,",
        "        IReadOnlyList<ComponentPropertyEntry> properties)",
        "    {",
        "        TypeName = typeName;",
        "        DisplayName = displayName;",
        "        Category = category;",
        "        Properties = properties;",
        "    }",
        "",
        "    public string TypeName { get; }",
        "    public string DisplayName { get; }",
        "    public string Category { get; }",
        "    public IReadOnlyList<ComponentPropertyEntry> Properties { get; }",
        "}",
        "",
        "public static class ComponentCatalog",
        "{",
        "    public static IReadOnlyList<ComponentCatalogEntry> All => Entries;",
        "",
        "    public static ComponentCatalogEntry? Find(string typeName)",
        "    {",
        "        foreach (ComponentCatalogEntry entry in Entries)",
        "            if (entry.TypeName == typeName) return entry;",
        "        return null;",
        "    }",
        "",
        "    private static readonly ComponentCatalogEntry[] Entries =",
        "    {",
    ]
    for type_name, display, category, properties in components:
        lines.append('        new("%s", "%s", "%s", new ComponentPropertyEntry[]'
                     % (type_name, display, category))
        lines.append("        {")
        for prop_name, prop_type, read_only in properties:
            lines.append('            new("%s", "%s", %s, %s),'
                         % (prop_name, prop_type,
                            "true" if read_only else "false",
                            "true" if prop_type in TYPE_MAP else "false"))
        lines.append("        }),")
    lines.append("    };")
    lines.append("}")

    with open(CATALOG, "wb") as stream:
        stream.write(b"\xef\xbb\xbf" + "\r\n".join(lines + [""]).encode("utf-8"))


def main():
    if not os.path.exists(DUMP):
        print("dump がありません: %s" % DUMP)
        print("先に  x64\\Release\\3dgp.exe --dump-component-properties  を実行してください。")
        return 1

    components = read_dump(DUMP)
    already = existing_types(MANAGED)
    missing = [c for c in components if c[0] not in already]

    header = [
        "using System;",
        "",
        "namespace ReplayEngine;",
        "",
        "// PropertyRegistry から起こした Component の型付き入口。手で書き換えない。",
        "//",
        "// 作り直しかた:",
        "//   x64\\Release\\3dgp.exe --dump-component-properties",
        "//   python Tools\\generate_component_bindings.py",
        "//",
        "// 手で書いてある型（Components.cs / ComponentsRendering.cs /",
        "// ComponentsPhysics.cs / ComponentsGameplay.cs / ComponentsUI.cs）は出さない。",
    ]
    # 一覧は不足の有無に関わらず作り直す。全型が載っている表なので。
    emit_catalog(components)
    print("一覧 %d 型 -> %s" % (len(components), os.path.relpath(CATALOG, ROOT)))

    body = emit(missing)
    if not body:
        print("型付き入口は不足なし（C# 側に全部ある）")
        return 0

    text = "\r\n".join(header + body + [""])
    with open(OUTPUT, "wb") as stream:
        stream.write(b"\xef\xbb\xbf" + text.encode("utf-8"))

    generated = len([l for l in body if l.startswith("public readonly struct")])
    print("既存 %d 型 / dump %d 型 / 生成 %d 型 -> %s"
          % (len(already), len(components), generated, os.path.relpath(OUTPUT, ROOT)))
    skipped = [c[0] for c in missing if not [p for p in c[3] if p[1] in TYPE_MAP]]
    if skipped:
        print("プロパティが無い、または Accessor に窓口が無い型は出していません:")
        print("  " + ", ".join(skipped))
    return 0


if __name__ == "__main__":
    sys.exit(main())
