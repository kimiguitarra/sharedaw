# テキストファイルを C++ のバイト配列として埋め込む（-DINPUT=... -DOUTPUT=...）。
file(READ "${INPUT}" hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
string(REGEX REPLACE "(0x[0-9a-f][0-9a-f],){32}" "\\0\n" bytes "${bytes}")
file(WRITE "${OUTPUT}" "#pragma once\n// generated from project.schema.json\n#include <string_view>\nnamespace collab::detail {\ninline constexpr unsigned char projectSchemaBytes[] = {\n${bytes}0x00 };\ninline std::string_view projectSchemaText() { return { reinterpret_cast<const char*> (projectSchemaBytes), sizeof (projectSchemaBytes) - 1 }; }\n}\n")
