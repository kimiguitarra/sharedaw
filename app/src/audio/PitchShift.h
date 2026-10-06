#pragma once

#include "Common.h"

/**
    オーディオクリップのピッチ（§3.6）: 元のファイルを、長さを変えずに半音単位で高さを変えたファイルにして
    曲のフォルダの cache/pitch に置く（同期はしない。なければその場で作り直せる）。
    その場で変える（Tracktion の実時間の処理）と音の位置が前後にずれるので、前もって作り、位置をそろえておく。
*/
namespace PitchShift
{
    /** 高さを変えたファイルの置き場所（まだないこともある）。 */
    juce::File cachedFile (const juce::File& projectDir, const std::string& hash, double semitones);

    /** source の高さを semitones 変えて dest に書く（長さ・位置は同じ）。時間がかかるので、画面のスレッドでは呼ばない。 */
    bool render (const juce::File& source, const juce::File& dest, double semitones);
}
