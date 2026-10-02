#!/usr/bin/env python3
"""app/src の日本語（非ASCII）文字列リテラルに _ju が付いているか確認する。

juce::String (const char*) は ASCII として扱うため、日本語は "..."_ju で書く必要がある
（docs/architecture.md「文字列」）。std::string として使う箇所は行末に `// utf8-std` と書いて除外する。
"""
import pathlib, re, sys

literal = re.compile(r'(?<![A-Za-z0-9_])"((?:[^"\\\n]|\\.)*)"(_ju)?')
problems = []


def strip_comment (line):
    """行コメント（// 以降）を外す。文字列の中の // （URL など）はコメントとみなさない。"""
    in_string = False
    i = 0
    while i < len (line):
        c = line[i]
        if in_string:
            if c == "\\":
                i += 1
            elif c == '"':
                in_string = False
        elif c == '"':
            in_string = True
        elif c == "'":
            # 文字リテラル（'"' など）は飛ばす
            end = line.find ("'", i + 2 if line.startswith ("\\", i + 1) else i + 1)
            i = end if end > i else i
        elif line.startswith ("//", i):
            return line[:i]
        i += 1
    return line


for path in sorted(pathlib.Path(__file__).resolve().parent.parent.joinpath("app/src").rglob("*")):
    if path.suffix not in (".cpp", ".h"):
        continue
    for no, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        code = strip_comment (line) if not line.lstrip().startswith("#include") else ""
        if "utf8-std" in line or line.lstrip().startswith(("*", "/*")):
            continue
        for m in literal.finditer(code):
            if any(ord(c) > 127 for c in m.group(1)) and not m.group(2):
                problems.append(f"{path}:{no}: {line.strip()}")

if problems:
    print("日本語のリテラルには _ju を付けてください（std::string なら行末に // utf8-std）:")
    print("\n".join(problems))
    sys.exit(1)

print("ok")
