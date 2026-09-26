#pragma once

#include <string>

namespace collab
{

/** 現在時刻を UTC の ISO 8601（例: 2026-09-26T12:34:56Z）で返す（§6.3, §8.1）。 */
std::string nowUtcIso8601();

} // namespace collab
