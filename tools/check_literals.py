#!/usr/bin/env python3
"""app/src の日本語（非ASCII）文字列リテラルに _ju が付いているか確認する。

juce::String (const char*) は ASCII として扱うため、日本語は "..."_ju で書く必要がある
（docs/architecture.md「文字列」）。std::string として使う箇所は行末に `// utf8-std` と書いて除外する。
"""
import pathlib, re, sys

literal = re.compile(r'(?<![A-Za-z0-9_])"((?:[^"\\\n]|\\.)*)"(_ju)?')
problems = []

for path in sorted(pathlib.Path(__file__).resolve().parent.parent.joinpath("app/src").rglob("*")):
    if path.suffix not in (".cpp", ".h"):
        continue
    for no, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        code = line.split("//")[0] if not line.lstrip().startswith("#include") else ""
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
