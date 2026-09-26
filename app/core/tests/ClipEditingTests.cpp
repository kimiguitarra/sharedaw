#include <doctest/doctest.h>

#include "collab/ClipEditing.h"

using namespace collab;

namespace
{
    // 120BPM: 1拍（960 tick）= 0.5 秒 = 24000 サンプル
    AudioClip clip()   { return { "a", 3840, std::string (64, 'a'), "take", 48000, 96000, -3.0, 2400, 4800 }; }
}

TEST_CASE ("audio clip end and sample conversion")
{
    const TempoMap map;
    CHECK (audioClipEndTick (clip(), map) == 3840 + 4 * 960);   // 2秒 = 4拍
    CHECK (samplesBetween (0, 960, map) == 24000);
}

TEST_CASE ("split audio clip keeps the source continuous")
{
    const TempoMap map;
    auto r = splitAudioClip (clip(), 3840 + 960, map, "b");
    REQUIRE (r);
    auto [l, rt] = *r;
    CHECK (l.id == "a");
    CHECK (l.lengthSamples == 24000);
    CHECK (l.fadeInSamples == 2400);
    CHECK (l.fadeOutSamples == 0);
    CHECK (rt.id == "b");
    CHECK (rt.startTick == 3840 + 960);
    CHECK (rt.sourceOffsetSamples == 48000 + 24000);
    CHECK (rt.lengthSamples == 72000);
    CHECK (rt.fadeInSamples == 0);
    CHECK (rt.fadeOutSamples == 4800);
    CHECK (rt.gainDb == -3.0);

    CHECK_FALSE (splitAudioClip (clip(), 3840, map, "b"));
    CHECK_FALSE (splitAudioClip (clip(), 3840 + 4 * 960, map, "b"));
}

TEST_CASE ("split respects tempo changes (audio does not follow tempo)")
{
    auto p = Project::createEmpty ("t");
    p.tempoTrack.events = { { "t1", 0, 120.0 }, { "t2", 3840 + 960, 60.0 } };   // 分割位置の後はテンポが半分
    const TempoMap map (p);
    auto c = clip();
    // 開始から 1拍（0.5秒）後で分割
    auto r = splitAudioClip (c, 3840 + 960, map, "b");
    REQUIRE (r);
    CHECK (r->first.lengthSamples == 24000);
    // 残り 1.5 秒は 60BPM で 1.5 拍
    CHECK (audioClipEndTick (r->second, map) == 3840 + 960 + 1440);
}

TEST_CASE ("trim start keeps the end and clamps to the source")
{
    const TempoMap map;
    auto t = trimAudioClipStart (clip(), 3840 + 960, map);
    CHECK (t.startTick == 3840 + 960);
    CHECK (t.sourceOffsetSamples == 72000);
    CHECK (t.lengthSamples == 72000);
    CHECK (audioClipEndTick (t, map) == audioClipEndTick (clip(), map));

    // 実体の頭（オフセット 1 秒 = 2拍）より前には伸ばせない
    auto e = trimAudioClipStart (clip(), 0, map);
    CHECK (e.sourceOffsetSamples == 0);
    CHECK (e.startTick == 3840 - 1920);
    CHECK (e.lengthSamples == 96000 + 48000);
}

TEST_CASE ("trim end clamps to the source length")
{
    const TempoMap map;
    auto t = trimAudioClipEnd (clip(), 3840 + 960, 200000, map);
    CHECK (t.lengthSamples == 24000);
    auto longer = trimAudioClipEnd (clip(), 3840 * 10, 200000, map);
    CHECK (longer.lengthSamples == 200000 - 48000);
}

TEST_CASE ("split MIDI clip")
{
    MidiClip c { "m", 0, 3840, { { "n1", 0, 960, 60, 100 }, { "n2", 1440, 960, 62, 100 }, { "n3", 2880, 480, 64, 100 } } };
    int counter = 0;
    auto r = splitMidiClip (c, 1920, "m2", [&] { return "new" + std::to_string (++counter); });
    REQUIRE (r);
    CHECK (r->first.lengthTick == 1920);
    REQUIRE (r->first.notes.size() == 2);
    CHECK (r->first.notes[1].lengthTick == 480);   // 分割位置で切れる
    CHECK (r->second.startTick == 1920);
    REQUIRE (r->second.notes.size() == 1);
    CHECK (r->second.notes[0].tick == 960);
    CHECK (r->second.notes[0].id == "new1");
    CHECK_FALSE (splitMidiClip (c, 0, "x", [] { return std::string(); }));
}
