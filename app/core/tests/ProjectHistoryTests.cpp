#include <doctest/doctest.h>

#include "collab/ProjectHistory.h"
#include "TestUtils.h"

using namespace collab;

TEST_CASE ("undo deltas keep only changed tracks and restore the project exactly")
{
    auto before = Project::createEmpty ("t");

    for (int i = 0; i < 4; ++i)
    {
        Track t;
        t.id = "track-" + std::to_string (i);
        t.name = "T" + std::to_string (i);
        MidiClip c;
        c.id = "clip-" + std::to_string (i);
        c.lengthTick = 3840;
        c.notes.push_back ({ "n" + std::to_string (i), 0, 480, 60 + i, 100 });
        t.midiClips.push_back (c);
        before.tracks.push_back (t);
    }

    // 1 つのトラックのノートとテンポを変え、1 つ消し、1 つ足し、並びを入れ替える
    auto after = before;
    after.tracks[1].midiClips[0].notes[0].velocity = 10;
    after.tempoTrack.events.push_back ({ "tempo", 3840, 90.0 });
    after.tracks.erase (after.tracks.begin() + 2);
    Track added;
    added.id = "added";
    after.tracks.push_back (added);
    std::swap (after.tracks[0], after.tracks[1]);

    const auto d = makeDelta (before, after);
    CHECK (d.tracks.size() == 2);   // 変えたトラックと消したトラックだけ
    CHECK (d.shell.tracks.empty());
    CHECK (applyDelta (d, after) == before);

    // 写さずに移す版も同じ（Project のフィールドが増えたら、写す版にも足すこと）
    auto moved = makeDelta (Project (before), after);
    CHECK (applyDelta (moved, after) == before);
    CHECK (moved.shell == d.shell);
    CHECK (moved.tracks == d.tracks);

    // 続けて別のトラックを変えた（まとめた操作）: 足した差分でも元に戻る
    auto later = after;
    later.tracks[1].name = "renamed";   // track-0
    later.tracks.erase (later.tracks.begin() + 2);   // track-3
    auto merged = d;
    extendDelta (merged, after, later);
    CHECK (merged.tracks.size() == 4);
    CHECK (applyDelta (merged, later) == before);

    // 何も変わっていなければトラックは持たない
    CHECK (makeDelta (before, before).tracks.empty());
}
