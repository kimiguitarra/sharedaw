#pragma once

#include <vector>

#include "TempoMap.h"

namespace collab
{

/** カウントインのクリック（§3.5）。秒は曲の先頭からの位置（負になることもある）。 */
struct Click
{
    double seconds = 0.0;
    bool accent = false;   // 小節の頭
};

/**
    startSeconds から録音するときのカウントイン（bars 小節分）のクリック。
    テンポ・拍子は録音開始位置のものを使い、拍は拍子の分母の音符とする。
*/
std::vector<Click> countInClicks (const TempoMap&, double startSeconds, int bars);

/** カウントインの長さ（秒）。 */
double countInSeconds (const TempoMap&, double startSeconds, int bars);

/**
    録音したテイクからオーディオクリップを作る。
    startSeconds はテイクの曲上の位置、offsetSeconds / lengthSeconds はファイル内の使用範囲、
    fileLengthSamples は 48kHz に変換後のファイルの長さ。
    punchInSeconds（録音開始位置）より前（カウントイン中に録れた部分）は使わない。
*/
AudioClip makeRecordedClip (const TempoMap&, std::string id, std::string audioHash, std::string displayName,
                            double startSeconds, double offsetSeconds, double lengthSeconds, SampleCount fileLengthSamples,
                            double punchInSeconds = 0.0);

} // namespace collab
