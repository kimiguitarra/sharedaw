#pragma once

#include "Common.h"

/**
    オーディオクリップのピッチ・テンポ合わせ（§3.6）: 元のファイルを、半音単位で高さを変え、speed 倍の速さにした（長さ 1/speed）ファイルにして
    曲のフォルダの cache/pitch に置く（同期はしない。なければその場で作り直せる）。
    その場で変える（Tracktion の実時間の処理）と音の位置が前後にずれるので、前もって作り、位置をそろえておく。
*/
namespace PitchShift
{
    /** 高さを変えたファイルの置き場所（まだないこともある）。 */
    juce::File cachedFile (const juce::File& projectDir, const std::string& hash, double semitones, double speed = 1.0);

    /**
        source の高さを semitones 変え、speed 倍の速さにして dest に書く（音の位置は 1/speed 倍の所にそろえる）。
        時間がかかるので、画面のスレッドでは呼ばない。
    */
    bool render (const juce::File& source, const juce::File& dest, double semitones, double speed = 1.0);
}
