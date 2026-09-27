#include <doctest/doctest.h>

#include "collab/Grid.h"

using namespace collab;

TEST_CASE ("grid step sizes")
{
    CHECK (Grid { 1, 1 }.stepTicks() == 3840);
    CHECK (Grid { 4, 1 }.stepTicks() == 960);
    CHECK (Grid { 16, 1 }.stepTicks() == 240);
    CHECK (Grid { 32, 1 }.stepTicks() == 120);
    CHECK (Grid { 8, 3 }.stepTicks() == 320);
    CHECK (Grid { 16, 3 }.stepTicks() == 160);
    CHECK (Grid { 16, 5 }.stepTicks() == 192);               // 4分を 5 等分
    CHECK (Grid { 16, 7 }.stepExact() == doctest::Approx (960.0 / 7.0));
    CHECK (Grid { 128, 1 }.stepTicks() == 30);
    CHECK (Grid::presets().size() == 19);                    // Cubase と同じ一覧
    CHECK (Grid::presets()[8].label() == "1/2 3連符");
    CHECK (Grid { 4, 3 }.label() == "1/4 3連符");
    CHECK (Grid { 16, 1 }.label() == "1/16");
}

TEST_CASE ("snap is relative to bar start")
{
    auto p = Project::createEmpty ("t");
    p.meterTrack.events = { { "a", 1, 3, 4 } };      // 3/4: 1小節 2880
    const TempoMap map (p);

    Grid whole { 1, 1 };
    CHECK (whole.snap (2000, map) == 2880);
    CHECK (whole.snap (1000, map) == 0);

    Grid quarter { 4, 1 };
    CHECK (quarter.snap (2880 + 500, map) == 2880 + 960);
    CHECK (quarter.snapFloor (2880 + 959, map) == 2880);

    Grid off { 4, 1, false };
    CHECK (off.snap (1234, map) == 1234);
}

TEST_CASE ("quantise notes")
{
    auto p = Project::createEmpty ("t");
    const TempoMap map (p);
    MidiClip c { "c", 3840, 3840, { { "n1", 250, 100, 60, 100 }, { "n2", 470, 100, 62, 100 }, { "n3", 10, 100, 64, 100 } } };

    quantiseNotes (c, { "n1", "n2" }, Grid { 16, 1 }, map);
    CHECK (c.notes[0].tick == 240);
    CHECK (c.notes[1].tick == 480);
    CHECK (c.notes[2].tick == 10);     // 選択外

    quantiseNotes (c, {}, Grid { 16, 1 }, map, 0.5);
    CHECK (c.notes[2].tick == 5);
}

TEST_CASE ("septuplet grid stays on exact positions")
{
    auto p = Project::createEmpty ("t");
    const TempoMap map (p);
    Grid sept { 16, 7 };   // 4分に 7 つ

    CHECK (sept.snap (960, map) == 960);                      // 拍の頭は必ずグリッド上
    CHECK (sept.snap (137, map) == 137);
    CHECK (sept.snap (7 * 960 + 5, map) == 7 * 960);
    CHECK (sept.snapFloor (274, map) == 274);
    CHECK (sept.snapFloor (273, map) == 137);
}
