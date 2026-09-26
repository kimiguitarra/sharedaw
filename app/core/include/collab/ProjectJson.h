#pragma once

#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "Project.h"

namespace collab
{

struct ProjectFormatError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/** Project → JSON。配列は決定的な順序、キーは仕様書の順。文字列は NFC に正規化する。 */
nlohmann::ordered_json projectToJson (const Project&);

/** JSON → Project。JSON Schema で検証してから読み込む。不正なら ProjectFormatError。 */
Project projectFromJson (const nlohmann::json&);

/** 保存用の文字列（2スペースインデント、末尾改行、UTF-8）。 */
std::string serialiseProject (const Project&);

/** 文字列から読み込む。構文エラー・スキーマ違反は ProjectFormatError。 */
Project parseProject (const std::string& text);

/** JSON Schema による検証。問題がなければ空文字列、あればエラーメッセージ。 */
std::string validateProjectJson (const nlohmann::json&);

/** 埋め込まれている JSON Schema（/shared/schema/project.schema.json）。 */
const nlohmann::json& projectSchema();

/** params など任意のオブジェクトのキーを再帰的にソートする（差分の安定化）。 */
nlohmann::ordered_json canonicalise (const nlohmann::json&);

} // namespace collab
