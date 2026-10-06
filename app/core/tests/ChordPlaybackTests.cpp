#include <doctest/doctest.h>

#include "collab/ChordPlayback.h"

using namespace collab;

namespace
{
    ChordEvent ev (const std::string& id, Tick tick, const std::string& text)
    {
        ChordEvent e;
        e.id = id;
        e.tick = tick;
        e.text = text;
        auto r = chord::parse (text);
        e.noChord = r.noChord;
        if (r.chord)
            e.chord = toChordSymbol (*r.chord);
        return e;
    }
}

TEST_CASE ("chord track: each chord event sounds once, up to one bar")
{
    auto p = Project::createEmpty ("t");
    const Tick bar = 3840;
    // C を2小節、F を1小節、X（無音）、G を1拍目から
    p.chordTrack.events = { ev ("a", 0, "C"), ev ("b", 2 * bar, "F"), ev ("c", 3 * bar, "X"), ev ("d", 4 * bar + 1920, "G") };
    const TempoMap map (p);
    auto notes = renderChordTrack (p, map);

    auto startsAt = [&] (Tick t)
    {
        return std::count_if (notes.begin(), notes.end(), [&] (auto& n) { return n.tick == t; });
    };

    CHECK (startsAt (0) == 4);          // ベース + 上声部3音
    CHECK (startsAt (bar) == 0);        // 弾き直さない
    CHECK (startsAt (2 * bar) == 4);    // F
    CHECK (startsAt (3 * bar) == 0);    // X は発音しない
    CHECK (startsAt (4 * bar + 1920) == 4);
    CHECK (notes.size() == 12);

    for (auto& n : notes)
    {
        CHECK (n.velocity == 80);
        CHECK (n.lengthTick > 0);
        CHECK (n.lengthTick <= bar);   // 最長 1 小節
    }

    // 最初の C はルート C2 と 60,64,67
    std::vector<int> first;
    for (auto& n : notes)
        if (n.tick == 0)
            first.push_back (n.pitch);
    std::sort (first.begin(), first.end());
    CHECK (first == std::vector<int> { 36, 60, 64, 67 });
}

TEST_CASE ("chord track: the last chord does not ring until the end of the song")
{
    auto p = Project::createEmpty ("t");
    p.chordTrack.events = { ev ("a", 0, "Am7") };
    Track t;
    t.id = "t";
    t.midiClips.push_back ({ "c", 0, 3840 * 3, {} });
    p.tracks.push_back (t);
    const TempoMap map (p);

    CHECK (chordTrackEndTick (p, map) == 3840 * 3);
    auto notes = renderChordTrack (p, map);
    CHECK (notes.size() == 5);

    for (auto& n : notes)
        CHECK (n.tick + n.lengthTick <= 3840);
}

TEST_CASE ("chord track: empty (undefined) chord events are silent")
{
    auto p = Project::createEmpty ("t");
    ChordEvent empty;
    empty.id = "e";
    empty.tick = 0;
    p.chordTrack.events = { empty, ev ("b", 3840, "F") };
    const TempoMap map (p);
    auto notes = renderChordTrack (p, map);
    CHECK (notes.size() == 4);
    CHECK (notes.front().tick == 3840);
}

TEST_CASE ("chord track: bar lines follow meter changes")
{
    auto p = Project::createEmpty ("t");
    p.meterTrack.events = { { "m", 1, 3, 4 } };     // 3/4 = 2880
    p.chordTrack.events = { ev ("a", 0, "C"), ev ("b", 2880 * 2, "X") };
    const TempoMap map (p);
    auto notes = renderChordTrack (p, map);
    // 3/4 なので 1 小節 = 2880 tick で切れる
    for (auto& n : notes)
        CHECK (n.lengthTick == 2880);
}

TEST_CASE ("chord tones at a position: root, chord intervals, tensions and slash bass; none before the first chord or on N.C.")
{
    auto p = Project::createEmpty ("t");
    p.chordTrack.events = { ev ("a", 3840, "Am7/G"), ev ("b", 3840 * 2, "G7(9)"), ev ("c", 3840 * 3, "X") };

    CHECK_FALSE (chordTonesAt (p, 0));
    CHECK (chordTonesAt (p, 3840) == std::vector<int> { 0, 4, 7, 9 });          // A C E G
    CHECK (chordTonesAt (p, 3840 * 2 + 100) == std::vector<int> { 2, 5, 7, 9, 11 });   // G B D F + A
    CHECK_FALSE (chordTonesAt (p, 3840 * 3));
}
