#include <doctest/doctest.h>

#include "collab/TempoMap.h"

using namespace collab;

namespace
{
    Project makeProject()
    {
        auto p = Project::createEmpty ("t");
        p.tempoTrack.events = { { "a", 0, 120.0 }, { "b", 4 * 4 * 960, 60.0 } };      // 5小節目から 60BPM
        p.meterTrack.events = { { "c", 1, 4, 4 }, { "d", 3, 3, 4 }, { "e", 5, 6, 8 } };
        return p;
    }
}

TEST_CASE ("tick <-> seconds with stepped tempo")
{
    const TempoMap map (makeProject());

    CHECK (map.tickToSeconds (0) == doctest::Approx (0.0));
    CHECK (map.tickToSeconds (960) == doctest::Approx (0.5));
    CHECK (map.tickToSeconds (15360) == doctest::Approx (8.0));
    CHECK (map.tickToSeconds (15360 + 960) == doctest::Approx (9.0));

    for (double t : { 0.0, 123.0, 959.5, 15360.0, 20000.0, 100000.0 })
        CHECK (map.secondsToTick (map.tickToSeconds (t)) == doctest::Approx (t));

    CHECK (map.tickToSamples (960) == 24000);
    CHECK (map.bpmAtTick (15359) == doctest::Approx (120.0));
    CHECK (map.bpmAtTick (15360) == doctest::Approx (60.0));
}

TEST_CASE ("bars with meter changes")
{
    const TempoMap map (makeProject());

    CHECK (map.barToTick (1) == 0);
    CHECK (map.barToTick (2) == 3840);
    CHECK (map.barToTick (3) == 7680);
    CHECK (map.barToTick (4) == 7680 + 2880);          // 3/4
    CHECK (map.barToTick (5) == 7680 + 2 * 2880);
    CHECK (map.barToTick (6) == 7680 + 2 * 2880 + 2880);   // 6/8 = 6 * 480

    CHECK (map.tickToBar (0) == 1);
    CHECK (map.tickToBar (3839) == 1);
    CHECK (map.tickToBar (3840) == 2);
    CHECK (map.tickToBar (7680 + 2880) == 4);

    auto bb = map.tickToBarBeat (7680 + 2 * 2880 + 480 + 10);
    CHECK (bb.bar == 5);
    CHECK (bb.beat == 2);            // 8分音符が 1 拍
    CHECK (bb.tickInBeat == 10);

    CHECK (map.timeSignatureAtBar (4) == TimeSignature { 3, 4 });
    CHECK (map.timeSignatureAtBar (100) == TimeSignature { 6, 8 });
}

TEST_CASE ("missing first events fall back to 120 BPM 4/4")
{
    TempoTrack tt;
    MeterTrack mt;
    const TempoMap map (tt, mt);
    CHECK (map.tickToSeconds (960) == doctest::Approx (0.5));
    CHECK (map.barToTick (2) == 3840);
}

TEST_CASE ("content end covers midi clips, audio clips and chords")
{
    auto p = Project::createEmpty ("t");
    Track midi;
    midi.midiClips.push_back ({ "c", 3840, 3840, {} });
    Track audio;
    audio.type = TrackType::audio;
    audio.audioClips.push_back ({ "a", 0, std::string (64, 'a'), "x", 0, 48000 * 10, 0, 0, 0 });   // 10秒 = 20拍
    p.tracks = { midi, audio };

    CHECK (p.contentEndTick() == 20 * 960);
}
