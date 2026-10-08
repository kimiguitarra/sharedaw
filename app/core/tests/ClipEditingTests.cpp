#include <doctest/doctest.h>

#include <algorithm>

#include "collab/ClipEditing.h"
#include "collab/Stretch.h"

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

TEST_CASE ("a clip with a source tempo follows the song tempo (Cubase musical mode)")
{
    // 素材は 60BPM で 4 秒（= 4 拍）。曲は 120BPM なので 2 倍の速さで、2 秒（= 4 拍）になる
    auto c = clip();
    c.sourceBpm = 60.0;
    c.sourceOffsetSamples = 0;
    c.lengthSamples = 4 * 48000;
    c.fadeInSamples = c.fadeOutSamples = 0;
    const TempoMap map;
    CHECK (audioClipSpeed (c, map) == doctest::Approx (2.0));
    CHECK (audioClipSeconds (c, map) == doctest::Approx (2.0));
    CHECK (audioClipEndTick (c, map) == 3840 + 4 * 960);

    // テンポを変えても拍の数は同じ
    auto p = Project::createEmpty ("t");
    p.tempoTrack.events = { { "t1", 0, 90.0 } };
    const TempoMap slower (p);
    CHECK (audioClipEndTick (c, slower) == 3840 + 4 * 960);

    // 1 拍目で分割すると、元ファイル上は 1 秒（素材の 1 拍）
    auto r = splitAudioClip (c, 3840 + 960, map, "b");
    REQUIRE (r);
    CHECK (r->first.lengthSamples == 48000);
    CHECK (r->second.sourceOffsetSamples == 48000);
    CHECK (r->second.sourceBpm == 60.0);
    CHECK (audioClipEndTick (r->second, map) == 3840 + 4 * 960);
    CHECK (glueAudioClips (r->first, r->second, map));

    auto t = trimAudioClipStart (c, 3840 + 960, map);
    CHECK (t.sourceOffsetSamples == 48000);
    CHECK (t.startTick == 3840 + 960);
    CHECK (audioClipEndTick (t, map) == 3840 + 4 * 960);

    auto e = trimAudioClipEnd (c, 3840 + 2 * 960, 4 * 48000, map);
    CHECK (e.lengthSamples == 2 * 48000);

    // 素材のテンポがなければ今まで通り
    c.sourceBpm = 0.0;
    CHECK (audioClipSpeed (c, map) == 1.0);
    CHECK (audioClipEndTick (c, map) == 3840 + 8 * 960);
}

TEST_CASE ("guess the source tempo from the name or the length")
{
    CHECK (guessSourceBpm ("drum_loop_100bpm", 0.0, 120.0) == 100.0);
    CHECK (guessSourceBpm ("Drums 92 BPM Cmin", 0.0, 120.0) == 92.0);
    CHECK (guessSourceBpm ("BPM128_house", 0.0, 120.0) == 128.0);
    CHECK (guessSourceBpm ("KSHMR_Kick_Loop_126_Fmin", 0.0, 120.0) == 126.0);
    CHECK (guessSourceBpm ("vocal_01_take3", 0.0, 120.0) == 120.0);           // 番号はテンポにしない
    CHECK (guessSourceBpm ("loop", 4.8, 105.0) == doctest::Approx (100.0));   // 8 拍で 4.8 秒 = 100BPM
    CHECK (guessSourceBpm ("loop", 2.0, 128.0) == doctest::Approx (120.0));   // 4 拍で 2 秒
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

TEST_CASE ("trimming a MIDI clip's start keeps notes in place and restores them when extended")
{
    MidiClip c { "m", 3840, 3840, { { "n1", 0, 240, 60, 100 }, { "n2", 1920, 240, 62, 100 } } };

    auto shorter = trimMidiClipStart (c, 3840 + 960, 240);
    CHECK (shorter.startTick == 4800);
    CHECK (shorter.endTick() == c.endTick());
    CHECK (shorter.notes[0].tick == -960);    // 隠れたノート
    CHECK (shorter.notes[1].tick == 960);     // 絶対位置は同じ

    auto back = trimMidiClipStart (shorter, 3840, 240);
    CHECK (back == c);

    CHECK (trimMidiClipStart (c, 99999, 240).lengthTick == 240);   // 最低の長さは残す
}

TEST_CASE ("glue MIDI and audio clips")
{
    MidiClip a { "a", 0, 3840, { { "n1", 0, 240, 60, 100 } } };
    MidiClip b { "b", 3840, 1920, { { "n2", 0, 240, 64, 100 } } };
    auto g = glueMidiClips (a, b);
    CHECK (g.id == "a");
    CHECK (g.lengthTick == 5760);
    REQUIRE (g.notes.size() == 2);
    CHECK (g.notes[1].tick == 3840);

    const auto map = TempoMap (Project::createEmpty ("t"));
    auto parts = splitAudioClip (clip(), 3840 + 960, map, "b");
    REQUIRE (parts);
    auto joined = glueAudioClips (parts->first, parts->second, map);
    REQUIRE (joined);
    CHECK (joined->lengthSamples == clip().lengthSamples);
    CHECK (joined->sourceOffsetSamples == clip().sourceOffsetSamples);

    auto other = parts->second;
    other.audioHash = std::string (64, 'b');
    CHECK (! glueAudioClips (parts->first, other, map));
}

TEST_CASE ("newer overlapping audio clip hides the older one with crossfades (Pro Tools style)")
{
    const TempoMap map;   // 120BPM: 1 拍 = 0.5 秒
    auto older = clip();  // 3840 tick（2 秒）から 2 秒、読み始め 1 秒、フェード 0.05 / 0.1 秒
    auto newer = clip();
    newer.id = "b";
    newer.startTick = 3840 + 960;   // 2.5 秒から
    newer.lengthSamples = 24000;    // 0.5 秒
    newer.fadeInSamples = newer.fadeOutSamples = 0;

    const double xf = 0.01;
    const auto segs = audibleSegments ({ older, newer }, map, xf);
    REQUIRE (segs.size() == 3);

    // 古いクリップは前後の 2 つに分かれ、切れ目で xf だけ上のクリップの下へ延びてクロスフェードする
    CHECK (segs[0].clipIndex == 0);
    CHECK (segs[0].startSeconds == doctest::Approx (2.0));
    CHECK (segs[0].lengthSeconds == doctest::Approx (0.5 + xf));
    CHECK (segs[0].offsetSeconds == doctest::Approx (1.0));
    CHECK (segs[0].fadeInSeconds == doctest::Approx (0.05));   // 自分のフェード
    CHECK_FALSE (segs[0].crossfadeIn);
    CHECK (segs[0].fadeOutSeconds == doctest::Approx (xf));
    CHECK (segs[0].crossfadeOut);

    CHECK (segs[1].clipIndex == 0);
    CHECK (segs[1].startSeconds == doctest::Approx (3.0 - xf));
    CHECK (segs[1].lengthSeconds == doctest::Approx (1.0 + xf));
    CHECK (segs[1].offsetSeconds == doctest::Approx (2.0 - xf));
    CHECK (segs[1].fadeInSeconds == doctest::Approx (xf));
    CHECK (segs[1].crossfadeIn);
    CHECK (segs[1].fadeOutSeconds == doctest::Approx (0.1));

    // 新しいクリップは全部鳴り、下のクリップの上なので入りと終わりがクロスフェード
    CHECK (segs[2].clipIndex == 1);
    CHECK (segs[2].startSeconds == doctest::Approx (2.5));
    CHECK (segs[2].lengthSeconds == doctest::Approx (0.5));
    CHECK (segs[2].fadeInSeconds == doctest::Approx (xf));
    CHECK (segs[2].crossfadeIn);
    CHECK (segs[2].fadeOutSeconds == doctest::Approx (xf));
    CHECK (segs[2].crossfadeOut);

    // すっかり隠れたクリップは鳴らない
    const auto hidden = audibleSegments ({ newer, older }, map, xf);
    CHECK (std::none_of (hidden.begin(), hidden.end(), [] (auto& s) { return s.clipIndex == 0; }));
}

TEST_CASE ("pitch bends: two points at the same tick jump, and a curve bends the ramp")
{
    // 半音下からしゃくり上げる: 0 → 同じ時刻に -1 半音（-4096）→ 480 tick で 0 へ
    std::vector<PitchBend> bends { { 0, 0 }, { 960, 0 }, { 960, -4096 }, { 1440, 0 } };
    CHECK (pitchBendAt (bends, 959) == 0);
    CHECK (pitchBendAt (bends, 960) == -4096);
    CHECK (pitchBendAt (bends, 1200) == -2048);
    CHECK (pitchBendAt (bends, 1440) == 0);

    // カーブ: 正はゆっくり始まる（真ん中ではまだ半分より下）、負は速く上がる
    bends[2].curve = 0.5;
    CHECK (pitchBendAt (bends, 1200) < -2048);
    bends[2].curve = -0.5;
    CHECK (pitchBendAt (bends, 1200) > -2048);
    CHECK (pitchBendAt (bends, 1440) == 0);

    // 真ん中で 75 % まで上がっているカーブを求める
    const double c = pitchBendCurveFor (0.75);
    CHECK (pitchBendCurve (0.5, c) == doctest::Approx (0.75));
    CHECK (pitchBendCurveFor (0.5) == doctest::Approx (0.0));

    // 鳴らすときもカーブどおり
    const auto dense = densePitchBends (bends, 20);
    CHECK (std::any_of (dense.begin(), dense.end(), [] (auto& b) { return b.tick == 1200 && b.value > -2048; }));
}

TEST_CASE ("pitch bends: value at a position, drawing a range, and split / trim / glue / stretch keep them in place")
{
    // 点の間は直線（Cubase のランプ）。最初の点より前は中央、最後の点より後はその値
    std::vector<PitchBend> bends { { 100, 4000 }, { 200, 0 } };
    CHECK (pitchBendAt (bends, 0) == 0);
    CHECK (pitchBendAt (bends, 100) == 4000);
    CHECK (pitchBendAt (bends, 150) == 2000);
    CHECK (pitchBendAt (bends, 250) == 0);

    // 鳴らすときは直線を細かいイベントで埋める
    const auto dense = densePitchBends ({ { 0, 0 }, { 100, 8000 } }, 20);
    CHECK (dense.size() == 6);
    CHECK (dense[2] == PitchBend { 40, 3200 });

    // 描く: 50〜120 を上書き。後ろは元の値に戻る
    bends = { { 100, 4000 }, { 200, 4000 }, { 300, 0 } };
    replacePitchBends (bends, 50, 120, { { 50, 1000 }, { 80, 2000 }, { 120, 3000 } });
    CHECK (pitchBendAt (bends, 50) == 1000);
    CHECK (pitchBendAt (bends, 80) == 2000);
    CHECK (pitchBendAt (bends, 121) == 4000);
    CHECK (pitchBendAt (bends, 250) == 2000);
    CHECK (pitchBendAt (bends, 300) == 0);

    // 消す（中央で描く）と、イベントはまとまる
    replacePitchBends (bends, 0, 300, { { 0, 0 } });
    CHECK (bends.empty());

    // 直線の途中で分割・トリムしても、形は変わらない
    MidiClip c { "c", 1000, 960 * 4, {}, { { 960, 8191 }, { 1920, -8192 }, { 2880, 0 } } };

    auto split = splitMidiClip (c, 1000 + 1500, "d", [] { return std::string ("n"); });
    REQUIRE (split);

    for (Tick t : { 960, 1200, 1499 })
        CHECK (std::abs (pitchBendAt (split->first.pitchBends, t) - pitchBendAt (c.pitchBends, t)) <= 1);

    for (Tick t : { 1500, 1700, 1920, 2400, 3000 })
        CHECK (std::abs (pitchBendAt (split->second.pitchBends, t - 1500) - pitchBendAt (c.pitchBends, t)) <= 1);

    auto glued = glueMidiClips (split->first, split->second);
    for (Tick t : { 0, 500, 1000, 1600, 2000, 3000 })
        CHECK (std::abs (pitchBendAt (glued.pitchBends, t) - pitchBendAt (c.pitchBends, t)) <= 20);

    auto trimmed = trimMidiClipStart (c, 1000 + 1200, 10);
    CHECK (std::abs (pitchBendAt (trimmed.pitchBends, 0) - pitchBendAt (c.pitchBends, 1200)) <= 1);
    CHECK (pitchBendAt (trimmed.pitchBends, 1920 - 1200) == -8192);

    auto stretched = stretchMidiClip (c, 0.5);
    CHECK (pitchBendAt (stretched.pitchBends, 480) == 8191);
    CHECK (pitchBendAt (stretched.pitchBends, 960) == -8192);
}

TEST_CASE ("recorded MIDI continues an existing clip")
{
    auto note = [] (std::string id, Tick t) { return Note { id, t, 480, 60, 100 }; };
    std::vector<MidiClip> clips { { "a", 3840, 3840, { note ("1", 0) }, {} } };

    // 重なる所に録ると、既存のクリップに足して、録音した範囲まで伸ばす
    MidiClip rec { "r", 3840 * 2, 3840, { note ("2", 960) }, { { 960, 4000 }, { 1440, 0 } } };
    CHECK (addRecordedMidi (clips, rec, true) == "a");
    REQUIRE (clips.size() == 1);
    CHECK (clips[0].startTick == 3840);
    CHECK (clips[0].lengthTick == 3840 * 2);
    REQUIRE (clips[0].notes.size() == 2);
    CHECK (clips[0].notes[1].tick == 3840 + 960);
    CHECK (pitchBendAt (clips[0].pitchBends, 3840 + 960) == 4000);
    CHECK (pitchBendAt (clips[0].pitchBends, 3840 + 1500) == 0);

    // 前に伸ばすときは元のノートの位置を保つ
    MidiClip before { "b", 0, 3840, { note ("3", 0) }, {} };
    CHECK (addRecordedMidi (clips, before, true) == "a");
    CHECK (clips[0].startTick == 0);
    CHECK (std::any_of (clips[0].notes.begin(), clips[0].notes.end(), [] (auto& n) { return n.id == "1" && n.tick == 3840; }));

    // 離れた所、または別のクリップにするモードでは新しいクリップ
    MidiClip far { "c", 3840 * 8, 3840, { note ("4", 0) }, {} };
    CHECK (addRecordedMidi (clips, far, true) == "c");
    MidiClip separate { "d", 0, 3840, { note ("5", 0) }, {} };
    CHECK (addRecordedMidi (clips, separate, false) == "d");
    CHECK (clips.size() == 3);
}

TEST_CASE ("audio clips that touch are joined with a short crossfade across the seam")
{
    const TempoMap map;   // 120BPM: 1 拍 = 0.5 秒
    AudioClip a { "a", 0, std::string (64, 'a'), "take", 48000, 48000, 0.0, 0, 0 };     // 0〜1 秒、読み始め 1 秒
    AudioClip b { "b", 1920, std::string (64, 'a'), "take", 96000, 48000, 0.0, 0, 0 };  // 1〜2 秒、読み始め 2 秒
    const double xf = 0.01;
    const auto segs = audibleSegments ({ a, b }, map, xf);
    REQUIRE (segs.size() == 2);

    CHECK (segs[0].startSeconds == doctest::Approx (0.0));
    CHECK (segs[0].lengthSeconds == doctest::Approx (1.0 + xf / 2));
    CHECK (segs[0].fadeOutSeconds == doctest::Approx (xf));
    CHECK (segs[0].crossfadeOut);
    CHECK_FALSE (segs[0].crossfadeIn);

    CHECK (segs[1].startSeconds == doctest::Approx (1.0 - xf / 2));
    CHECK (segs[1].offsetSeconds == doctest::Approx (2.0 - xf / 2));
    CHECK (segs[1].fadeInSeconds == doctest::Approx (xf));
    CHECK (segs[1].crossfadeIn);
    CHECK (segs[1].fadeOutSeconds == doctest::Approx (0.0));

    // 元ファイルに余りがない（録ったテイクをそのまま）: 後ろのクリップを前にずらしてでも xf だけ重ねる
    {
        AudioClip ta { "a", 0, std::string (64, 'a'), "take1", 0, 48000, 0.0, 0, 0 };
        AudioClip tb { "b", 1920, std::string (64, 'b'), "take2", 0, 48000, 0.0, 0, 0 };
        AudioClip tc { "c", 3840, std::string (64, 'c'), "take3", 0, 48000, 0.0, 0, 0 };
        const auto oneSecond = [] (const AudioClip&) { return 1.0; };
        const auto takes = audibleSegments ({ ta, tb, tc }, map, xf, oneSecond);
        REQUIRE (takes.size() == 3);

        for (size_t i = 0; i + 1 < takes.size(); ++i)
        {
            const double endA = takes[i].startSeconds + takes[i].lengthSeconds;
            CHECK (endA - takes[i + 1].startSeconds == doctest::Approx (xf));   // ちょうど xf 重なる
            CHECK (takes[i].fadeOutSeconds == doctest::Approx (xf));
            CHECK (takes[i + 1].fadeInSeconds == doctest::Approx (xf));
            CHECK (takes[i + 1].offsetSeconds == doctest::Approx (0.0));         // ファイルの頭より前は読まない
            CHECK (takes[i].offsetSeconds + takes[i].lengthSeconds <= 1.0 + 1e-9);   // 終わりより後も読まない
        }

        CHECK (takes[1].startSeconds == doctest::Approx (1.0 - xf));
        CHECK (takes[2].startSeconds == doctest::Approx (2.0 - 2 * xf));

        // 片側だけ余りがあれば、そちらから全部取る（ずらさない）
        AudioClip trimmed = tb;
        trimmed.sourceOffsetSamples = 4800;
        trimmed.lengthSamples = 43200;
        const auto oneSide = audibleSegments ({ ta, trimmed }, map, xf, oneSecond);
        CHECK (oneSide[1].startSeconds == doctest::Approx (1.0 - xf));
        CHECK (oneSide[1].offsetSeconds == doctest::Approx (0.1 - xf));
        CHECK (oneSide[0].lengthSeconds == doctest::Approx (1.0));
    }

    // 離れていればクロスフェードしない
    b.startTick = 1920 + 96;
    const auto apart = audibleSegments ({ a, b }, map, xf);
    CHECK_FALSE (apart[0].crossfadeOut);
    CHECK_FALSE (apart[1].crossfadeIn);
}
