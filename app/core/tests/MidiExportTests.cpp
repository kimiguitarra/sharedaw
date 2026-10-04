#include <doctest/doctest.h>

#include "collab/MidiExport.h"
#include "collab/ProjectJson.h"
#include "TestUtils.h"
#include "collab/ChordPlayback.h"
#include "collab/chord/Degree.h"

using namespace collab;

namespace
{
    struct ParsedEvent { Tick tick; std::vector<std::uint8_t> bytes; };
    struct ParsedFile { int format = -1, tracks = 0, ppq = 0; std::vector<std::vector<ParsedEvent>> events; };

    // テスト用の小さな SMF の読み取り（ランニングステータスは使っていないので扱わない）
    ParsedFile parse (const std::vector<std::uint8_t>& d)
    {
        ParsedFile f;
        size_t pos = 0;
        auto u32 = [&] { std::uint32_t v = 0; for (int i = 0; i < 4; ++i) v = (v << 8) | d.at (pos++); return v; };
        auto u16 = [&] { std::uint32_t v = (std::uint32_t) (d.at (pos) << 8 | d.at (pos + 1)); pos += 2; return (int) v; };
        auto var = [&] { std::uint32_t v = 0; std::uint8_t b; do { b = d.at (pos++); v = (v << 7) | (b & 0x7f); } while (b & 0x80); return v; };

        REQUIRE (std::string (d.begin(), d.begin() + 4) == "MThd");
        pos = 4;
        REQUIRE (u32() == 6);
        f.format = u16(); f.tracks = u16(); f.ppq = u16();

        for (int t = 0; t < f.tracks; ++t)
        {
            REQUIRE (std::string (d.begin() + (long) pos, d.begin() + (long) pos + 4) == "MTrk");
            pos += 4;
            const auto length = u32();
            const auto chunkEnd = pos + length;
            Tick tick = 0;
            std::vector<ParsedEvent> evs;

            while (pos < chunkEnd)
            {
                tick += var();
                const auto status = d.at (pos);
                ParsedEvent e { tick, {} };

                if (status == 0xff)
                {
                    e.bytes = { d.at (pos), d.at (pos + 1) };
                    pos += 2;
                    const auto len = var();
                    e.bytes.insert (e.bytes.end(), d.begin() + (long) pos, d.begin() + (long) (pos + len));
                    pos += len;
                }
                else
                {
                    const int n = (status & 0xf0) == 0xc0 ? 2 : 3;
                    e.bytes.assign (d.begin() + (long) pos, d.begin() + (long) pos + n);
                    pos += (size_t) n;
                }

                evs.push_back (e);
            }

            REQUIRE (pos == chunkEnd);
            REQUIRE (evs.back().bytes == std::vector<std::uint8_t> { 0xff, 0x2f });
            f.events.push_back (evs);
        }

        REQUIRE (pos == d.size());
        return f;
    }

    MidiClip clip (Tick start, Tick length, std::vector<Note> notes)
    {
        MidiClip c;
        c.id = "c" + std::to_string (start);
        c.startTick = start;
        c.lengthTick = length;
        c.notes = std::move (notes);
        return c;
    }

    Note note (Tick tick, Tick length, int pitch, int velocity = 100)
    {
        Note n;
        n.tick = tick; n.lengthTick = length; n.pitch = pitch; n.velocity = velocity;
        return n;
    }
}

TEST_CASE ("MIDI export writes a type 1 file with tempo, meter, tracks and clipped notes")
{
    auto p = Project::createEmpty ("Song");
    p.tempoTrack.events = { { "t1", 0, 110.0 }, { "t2", 3840 * 2, 90.0 } };
    p.meterTrack.events = { { "m1", 1, 3, 4 } };
    p.keyTrack.events = { { "k1", 1, 9, true } };   // A minor（調号なし）
    p.markerTrack.events = { { "mk", 2880, "Verse" } };

    Track drums;
    drums.id = "d"; drums.name = "Drums"; drums.type = TrackType::midi;
    drums.instrument = Instrument { Instrument::Kind::builtin, "builtin.drums", "1.2.0" };
    drums.midiClips = { clip (2880, 2880, { note (0, 240, 36), note (2880, 240, 38) /* クリップの外 */, note (2760, 480, 42) /* 端で切る */ }) };

    Track bass;
    bass.id = "b"; bass.name = "Bass"; bass.type = TrackType::midi;
    bass.instrument = Instrument { Instrument::Kind::builtin, "builtin.bass", "3.2.0", { { "preset", "upright" } } };
    bass.midiClips = { clip (0, 2880, { note (960, 960, 40, 90) }) };

    Track audio;
    audio.id = "a"; audio.name = "Vocal"; audio.type = TrackType::audio;

    p.tracks = { drums, audio, bass };
    const TempoMap map (p);
    const auto f = parse (writeMidiFile (p, map, true));

    CHECK (f.format == 1);
    CHECK (f.ppq == 960);
    REQUIRE (f.tracks == 3);   // 指揮トラック + MIDI 2 本（オーディオは書かない、コードなし）

    auto has = [] (const std::vector<ParsedEvent>& evs, Tick tick, std::vector<std::uint8_t> bytes)
    {
        return std::any_of (evs.begin(), evs.end(), [&] (auto& e) { return e.tick == tick && e.bytes == bytes; });
    };

    // テンポ: 110 BPM = 545455 µs（0x0852AF）、90 BPM = 666667 µs（0x0A2C2B）
    CHECK (has (f.events[0], 0, { 0xff, 0x51, 0x08, 0x52, 0xaf }));
    CHECK (has (f.events[0], 7680, { 0xff, 0x51, 0x0a, 0x2c, 0x2b }));
    CHECK (has (f.events[0], 0, { 0xff, 0x58, 3, 2, 24, 8 }));
    CHECK (has (f.events[0], 0, { 0xff, 0x59, 0, 1 }));
    CHECK (has (f.events[0], 2880, { 0xff, 0x06, 'V', 'e', 'r', 's', 'e' }));

    // ドラムはチャンネル 10、クリップの位置を足した絶対位置。外のノートは無し、はみ出しは端で切る
    const auto& d = f.events[1];
    CHECK (has (d, 0, { 0xff, 0x03, 'D', 'r', 'u', 'm', 's' }));
    CHECK (has (d, 2880, { 0x99, 36, 100 }));
    CHECK (has (d, 3120, { 0x89, 36, 0x40 }));
    CHECK (has (d, 2880 + 2760, { 0x99, 42, 100 }));
    CHECK (has (d, 2880 + 2880, { 0x89, 42, 0x40 }));
    CHECK (std::none_of (d.begin(), d.end(), [] (auto& e) { return e.bytes.size() == 3 && e.bytes[1] == 38; }));

    // ベースはチャンネル 1、ウッドベースは GM 33 番（0 始まりで 32）
    const auto& b = f.events[2];
    CHECK (has (b, 0, { 0xc0, 32 }));
    CHECK (has (b, 960, { 0x90, 40, 90 }));
    CHECK (has (b, 1920, { 0x80, 40, 0x40 }));
}

TEST_CASE ("MIDI export adds the chord track as its own track")
{
    auto p = Project::createEmpty ("Song");
    ChordEvent c;
    c.id = "c"; c.tick = 0; c.text = "Cmaj7";
    c.chord = toChordSymbol (*chord::parse ("Cmaj7").chord);
    p.chordTrack.events = { c };
    const TempoMap map (p);

    const auto expected = renderChordTrack (p, map);
    REQUIRE_FALSE (expected.empty());

    const auto with = parse (writeMidiFile (p, map, true));
    REQUIRE (with.tracks == 2);
    const auto& evs = with.events[1];
    CHECK (evs.front().bytes == std::vector<std::uint8_t> { 0xff, 0x03, 'C', 'h', 'o', 'r', 'd', 's' });
    CHECK (std::count_if (evs.begin(), evs.end(), [] (auto& e) { return e.bytes.size() == 3 && (e.bytes[0] & 0xf0) == 0x90; })
           == (long) expected.size());

    CHECK (parse (writeMidiFile (p, map, false)).tracks == 1);
}

TEST_CASE ("MIDI key signatures are spelled the way the app names the key")
{
    CHECK (chord::keyName ({ 6, false }) == "Gb");
    CHECK (chord::keySignature ({ 6, false }) == -6);   // F# / Gb 長調 → フラット 6 つ（画面の Gb と同じ）
    CHECK (chord::keySignature ({ 3, true }) == -6);    // Ebm
    CHECK (chord::keySignature ({ 11, false }) == 5);   // B
    CHECK (chord::keySignature ({ 9, true }) == 0);     // Am

    auto p = Project::createEmpty ("Song");
    p.keyTrack.events = { { "k", 1, 6, false } };
    const TempoMap map (p);
    const auto f = parse (writeMidiFile (p, map, false));
    CHECK (std::any_of (f.events[0].begin(), f.events[0].end(), [] (auto& e)
    {
        return e.bytes == std::vector<std::uint8_t> { 0xff, 0x59, (std::uint8_t) (std::int8_t) -6, 0 };
    }));
}

TEST_CASE ("pitch bends are written and reset to centre at the clip end")
{
    auto p = parseProject (fixture ("full.project.json"));
    auto& clip = p.tracks[0].midiClips.at (0);
    clip.pitchBends = { { 0, 8191 } };

    const TempoMap map (p);
    const auto bytes = writeMidiFile (p, map, false);

    // ピッチベンド（0xE0〜0xEF）: 一番上（LSB 0x7f, MSB 0x7f）と、中央（0x00, 0x40）
    int up = 0, centre = 0;

    for (size_t i = 0; i + 2 < bytes.size(); ++i)
        if ((bytes[i] & 0xf0) == 0xe0)
        {
            up += bytes[i + 1] == 0x7f && bytes[i + 2] == 0x7f ? 1 : 0;
            centre += bytes[i + 1] == 0x00 && bytes[i + 2] == 0x40 ? 1 : 0;
        }

    CHECK (up == 1);
    CHECK (centre >= 1);
}
