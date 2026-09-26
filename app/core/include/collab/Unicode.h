#pragma once

#include <string>

namespace collab
{

/** UTF-8 文字列を Unicode NFC に正規化する（§8.1）。不正な UTF-8 はそのまま返す。 */
std::string toNfc (const std::string& utf8);

/** ファイル名に使えない文字（\ / : * ? " < > | と制御文字）を '_' に置き換え、NFC に正規化する。 */
std::string sanitiseFileName (const std::string& utf8);

} // namespace collab
