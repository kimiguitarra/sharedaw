#pragma once

#include <string>

namespace collab
{

/** 小文字ハイフン区切りの UUID v4 を生成する。 */
std::string generateUuid();

/** UUID の書式（小文字 8-4-4-4-12）として正しいか。 */
bool isValidUuid (const std::string&);

} // namespace collab
