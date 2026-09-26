#pragma once

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

/** コードトラックの終端（最後のコードは曲の末尾まで。曲がそれより短ければ次の小節線まで）。 */
Tick chordTrackEndTick (const Project&, const TempoMap&);

/**
    コードトラックの自動発音（§3.8）: 全音符ベタ、小節線をまたぐ場合は各小節の頭で弾き直す。
    ボイシングは collab::chord::voiceProgression による。ノーコード区間は発音しない。
*/
std::vector<GeneratedNote> renderChordTrack (const Project&, const TempoMap&, int velocity = 80);

} // namespace collab
