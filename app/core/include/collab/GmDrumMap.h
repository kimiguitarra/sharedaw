#pragma once

#include <string>

namespace collab
{

/** GM ドラムマップのパーツ名（表示用）。該当しなければ空文字列。 */
std::string gmDrumName (int midiNote);

/** "C4" 形式の音名（C4 = 60、ヤマハ式ではなく一般的な表記）。 */
std::string midiNoteName (int midiNote);

} // namespace collab
