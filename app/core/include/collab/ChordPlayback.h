#pragma once

#include "collab/chord/Degree.h"

#include <vector>

#include "collab/chord/Chord.h"
#include "Project.h"
#include "TempoMap.h"

namespace collab
{

struct GeneratedNote
{
    Tick tick = 0;
    Tick lengthTick = 0;
    int pitch = 60;
    int velocity = 80;

    bool operator== (const GeneratedNote&) const = default;
};

chord::Chord toChord (const ChordSymbol&);
ChordSymbol toChordSymbol (const chord::Chord&);

/** その位置のキー（キートラックが空なら std::nullopt）。 */
std::optional<chord::Key> keyAt (const Project&, const TempoMap&, Tick);

/**
    その位置（曲の頭からの tick）で鳴っているコードの構成音（ピッチクラス 0〜11。書いてあるテンションと分数コードのベースも含む）。
    コードがない・ノーコード（N.C.）の所は std::nullopt。ベースの打ち込みで、コードトーン以外の音に印を付けるのに使う。
*/
std::optional<std::vector<int>> chordTonesAt (const Project&, Tick);

/** コードトラックの終端（最後のコードは曲の末尾まで。曲がそれより短ければ次の小節線まで）。 */
Tick chordTrackEndTick (const Project&, const TempoMap&);

/**
    コードトラックの自動発音（§3.8）: コードイベントごとに 1 回だけ鳴らす（次のコードまで、最長 1 小節）。
    ボイシングは collab::chord::voiceProgression による。ノーコード区間は発音しない。
*/
std::vector<GeneratedNote> renderChordTrack (const Project&, const TempoMap&, int velocity = 80);

} // namespace collab
