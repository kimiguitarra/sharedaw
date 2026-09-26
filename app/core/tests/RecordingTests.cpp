#include <doctest/doctest.h>

#include "collab/Recording.h"

using namespace collab;

namespace
{
    Project projectWith (double bpm, int num, int den)
    {
        Project p;
        p.tempoTrack.events = { { "t", 0, bpm } };
        p.meterTrack.events = { { "m", 1, num, den } };
        return p;
    }
}

TEST_CASE ("count-in clicks: 2 bars of 4/4 at 120 BPM before bar 3")
{
    const TempoMap map (projectWith (120.0, 4, 4));
    const double start = map.tickToSeconds ((double) map.barToTick (3));   // 4 秒
    auto clicks = countInClicks (map, start, 2);

    REQUIRE (clicks.size() == 8);
    CHECK (clicks[0].seconds == doctest::Approx (0.0));
    CHECK (clicks[0].accent);
    CHECK_FALSE (clicks[1].accent);
    CHECK (clicks[4].accent);
    CHECK (clicks[7].seconds == doctest::Approx (3.5));
    CHECK (countInSeconds (map, start, 2) == doctest::Approx (4.0));
}

TEST_CASE ("count-in before the song start goes into negative time")
{
    const TempoMap map (projectWith (100.0, 6, 8));   // 8分音符 = 0.3 秒
    auto clicks = countInClicks (map, 0.0, 1);

    REQUIRE (clicks.size() == 6);
    CHECK (clicks[0].seconds == doctest::Approx (-1.8));
    CHECK (clicks[5].seconds == doctest::Approx (-0.3));
    CHECK (countInClicks (map, 0.0, 0).empty());
}

TEST_CASE ("recorded clip is placed at the take position and clipped to the file")
{
    const TempoMap map (projectWith (120.0, 4, 4));
    auto c = makeRecordedClip (map, "id", "hash", "take", 2.0, 0.0, 1.5, 48000 * 3);
    CHECK (c.startTick == 4 * 960);
    CHECK (c.sourceOffsetSamples == 0);
    CHECK (c.lengthSamples == 72000);

    auto early = makeRecordedClip (map, "id", "hash", "take", -0.5, 0.0, 1.0, 48000);
    CHECK (early.startTick == 0);
    CHECK (early.sourceOffsetSamples == 24000);
    CHECK (early.lengthSamples == 24000);

    auto punched = makeRecordedClip (map, "id", "hash", "take", 0.5, 0.0, 5.0, 48000 * 5, 4.0);
    CHECK (punched.startTick == 8 * 960);
    CHECK (punched.sourceOffsetSamples == 168000);
    CHECK (punched.lengthSamples == 72000);

    auto longer = makeRecordedClip (map, "id", "hash", "take", 0.0, 0.0, 5.0, 48000);
    CHECK (longer.lengthSamples == 48000);
}
