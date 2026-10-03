#pragma once

// MIDI の伸び縮み（音価を一定の割合で変える）と、曲全体の解釈の変更
// （例: 3/4・BPM 180 の曲を 6/8・BPM 90 として扱う = 位置と長さを 1/2、テンポも 1/2）。

#include <optional>
#include <set>
#include <string>

#include "Project.h"
#include "TempoMap.h"

namespace collab
{

/** tick を factor 倍にして丸める。 */
Tick stretchTick (Tick, double factor);

/** クリップの中身（ノートの位置と長さ）とクリップの長さを factor 倍にする。クリップの開始位置は変えない。 */
MidiClip stretchMidiClip (const MidiClip&, double factor);

/**
    クリップの中の選んだノートを、いちばん早いノートの位置を起点に factor 倍にする（位置と長さ）。
    クリップに収まらなくなったら、クリップを小節単位ではなくノートの終わりまで伸ばす。
*/
MidiClip stretchNotes (const MidiClip&, const std::set<std::string>& noteIds, double factor);

struct ProjectStretch
{
    double factor = 0.5;                       // 位置と長さの倍率（0.5 = 半分）
    bool scaleTempo = true;                    // テンポも同じ倍率にする（聞こえ方を変えない）
    std::optional<TimeSignature> newMeter;     // 拍子を変える（全部の拍子をこれにする）
};

/**
    曲全体の tick を factor 倍にする: MIDI クリップ（位置・長さ・ノート）、オーディオクリップの位置、コード、マーカー、テンポの位置。
    拍子・キーは小節で持っているので、元の位置（tick）を factor 倍した所の小節に置き直す。
    scaleTempo なら BPM も factor 倍（秒で見た位置は変わらない）。
*/
void stretchProject (Project&, const ProjectStretch&);

} // namespace collab
