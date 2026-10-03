#include <doctest/doctest.h>

#include "collab/Stretch.h"

using namespace collab;

namespace
{
    Project waltz()
    {
        // 3/4・BPM 180、2 小節目にキー、3 小節目にコード・マーカー、4 小節目からテンポ 160
        Project p;
        p.tempoTrack.events = { { "t1", 0, 180.0 }, { "t2", kPpq * 9, 160.0 } };
        p.meterTrack.events = { { "m1", 1, 3, 4 } };
        p.keyTrack.events = { { "k1", 1, 0, false }, { "k2", 3, 7, false } };
        p.chordTrack.events = { { "c1", kPpq * 6, false, std::nullopt, "C" } };
        p.markerTrack.events = { { "mk", kPpq * 6, "B" } };

        Track t;
        t.id = "midi";
        t.type = TrackType::midi;
        MidiClip c;
        c.id = "clip";
        c.startTick = kPpq * 3;
        c.lengthTick = kPpq * 6;
        c.notes = { { "n1", 0, kPpq, 60, 100 }, { "n2", kPpq * 3 / 2, kPpq / 2, 64, 90 } };
        t.midiClips.push_back (c);
        p.tracks.push_back (t);

        Track a;
        a.id = "audio";
        a.type = TrackType::audio;
        AudioClip ac;
        ac.id = "ac";
        ac.startTick = kPpq * 6;
        ac.lengthSamples = 48000;
        a.audioClips.push_back (ac);
        p.tracks.push_back (a);
        return p;
    }
}

TEST_CASE ("stretching a MIDI clip scales notes and length, not the start")
{
    const auto c = stretchMidiClip (waltz().tracks[0].midiClips[0], 0.5);
    CHECK (c.startTick == kPpq * 3);
    CHECK (c.lengthTick == kPpq * 3);
    CHECK (c.notes[1].tick == kPpq * 3 / 4);
    CHECK (c.notes[1].lengthTick == kPpq / 4);

    const auto twice = stretchMidiClip (c, 2.0);
    CHECK (twice == waltz().tracks[0].midiClips[0]);
}

TEST_CASE ("stretching selected notes keeps the first selected note in place")
{
    auto clip = waltz().tracks[0].midiClips[0];
    const auto c = stretchNotes (clip, { "n2" }, 2.0);
    CHECK (c.notes[0] == clip.notes[0]);
    CHECK (c.notes[1].tick == kPpq * 3 / 2);
    CHECK (c.notes[1].lengthTick == kPpq);
}

TEST_CASE ("3/4 at 180 BPM becomes 6/8 at 90 BPM and sounds the same")
{
    const auto before = waltz();
    auto p = before;
    stretchProject (p, { 0.5, true, TimeSignature { 6, 8 } });

    const TempoMap oldMap (before), newMap (p);

    // 秒で見た位置は変わらない（音もオーディオも同じ所で鳴る）
    CHECK (newMap.tickToSeconds ((double) p.tracks[0].midiClips[0].startTick) == doctest::Approx (oldMap.tickToSeconds ((double) before.tracks[0].midiClips[0].startTick)));
    CHECK (newMap.tickToSeconds ((double) p.tracks[1].audioClips[0].startTick) == doctest::Approx (oldMap.tickToSeconds ((double) before.tracks[1].audioClips[0].startTick)));
    CHECK (newMap.tickToSeconds ((double) p.chordTrack.events[0].tick) == doctest::Approx (oldMap.tickToSeconds ((double) before.chordTrack.events[0].tick)));
    CHECK (p.tempoTrack.events[0].bpm == doctest::Approx (90.0));
    CHECK (p.tempoTrack.events[1].bpm == doctest::Approx (80.0));

    // 3/4 の 2 小節が 6/8 の 1 小節になる（3 小節目の頭 → 2 小節目の頭）
    CHECK (p.meterTrack.events.size() == 1);
    CHECK (p.meterTrack.events[0].numerator == 6);
    CHECK (p.meterTrack.events[0].denominator == 8);
    CHECK (newMap.tickToBar (p.chordTrack.events[0].tick) == 2);
    CHECK (p.markerTrack.events[0].tick == kPpq * 3);
    CHECK (p.keyTrack.events[1].bar == 2);
}

TEST_CASE ("stretching without the tempo changes how long the song is")
{
    auto p = waltz();
    stretchProject (p, { 2.0, false, std::nullopt });
    CHECK (p.tempoTrack.events[0].bpm == doctest::Approx (180.0));
    CHECK (p.tracks[0].midiClips[0].startTick == kPpq * 6);
    CHECK (p.tracks[0].midiClips[0].lengthTick == kPpq * 12);
    CHECK (p.meterTrack.events[0].numerator == 3);
    CHECK (p.keyTrack.events[1].bar == 5);
}
