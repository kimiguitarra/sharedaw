#include <doctest/doctest.h>

#include "collab/Grid.h"

using namespace collab;

TEST_CASE ("grid step sizes")
{
    CHECK (Grid { 1, false }.stepTicks() == 3840);
    CHECK (Grid { 4, false }.stepTicks() == 960);
    CHECK (Grid { 16, false }.stepTicks() == 240);
    CHECK (Grid { 32, false }.stepTicks() == 120);
    CHECK (Grid { 8, true }.stepTicks() == 320);
    CHECK (Grid { 16, true }.stepTicks() == 160);
    CHECK (Grid::presets().size() == 11);
    CHECK (Grid::presets()[2].stepTicks() == 1280);          // 1/3 = 2分3連
    CHECK (Grid { 2, true }.label().rfind ("1/3", 0) == 0);
    CHECK (Grid { 4, true }.label().rfind ("1/6", 0) == 0);
}

TEST_CASE ("snap is relative to bar start")
{
    auto p = Project::createEmpty ("t");
    p.meterTrack.events = { { "a", 1, 3, 4 } };      // 3/4: 1小節 2880
    const TempoMap map (p);

    Grid whole { 1, false };
    CHECK (whole.snap (2000, map) == 2880);
    CHECK (whole.snap (1000, map) == 0);

    Grid quarter { 4, false };
    CHECK (quarter.snap (2880 + 500, map) == 2880 + 960);
    CHECK (quarter.snapFloor (2880 + 959, map) == 2880);

    Grid off { 4, false, false };
    CHECK (off.snap (1234, map) == 1234);
}

TEST_CASE ("quantise notes")
{
    auto p = Project::createEmpty ("t");
    const TempoMap map (p);
    MidiClip c { "c", 3840, 3840, { { "n1", 250, 100, 60, 100 }, { "n2", 470, 100, 62, 100 }, { "n3", 10, 100, 64, 100 } } };

    quantiseNotes (c, { "n1", "n2" }, Grid { 16, false }, map);
    CHECK (c.notes[0].tick == 240);
    CHECK (c.notes[1].tick == 480);
    CHECK (c.notes[2].tick == 10);     // 選択外

    quantiseNotes (c, {}, Grid { 16, false }, map, 0.5);
    CHECK (c.notes[2].tick == 5);
}
