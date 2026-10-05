#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "collab/ChannelStripDsp.h"
#include "collab/ProjectJson.h"
#include "collab/Render.h"
#include "collab/Uuid.h"
#include "TestUtils.h"

using namespace collab;

namespace
{
    Project projectWithTrack()
    {
        auto p = parseProject (fixture ("full.project.json"));
        return p;
    }

    /** 一定振幅のサイン波を流して、最後の区間のピークを返す。 */
    double runSine (ChannelStripDsp& dsp, double freq, double amplitude, double seconds, double sr = 48000.0)
    {
        const int n = (int) (seconds * sr);
        std::vector<float> l ((size_t) n), r ((size_t) n);

        for (int i = 0; i < n; ++i)
            l[(size_t) i] = r[(size_t) i] = (float) (amplitude * std::sin (2.0 * 3.14159265358979 * freq * i / sr));

        float* ch[] = { l.data(), r.data() };

        for (int pos = 0; pos < n; pos += 512)
        {
            float* block[] = { ch[0] + pos, ch[1] + pos };
            dsp.process (block, 2, std::min (512, n - pos));
        }

        double peak = 0;
        for (int i = n - (int) (sr * 0.1); i < n; ++i)
            peak = std::max (peak, (double) std::abs (l[(size_t) i]));

        return peak;
    }
}

TEST_CASE ("channel strip is omitted from JSON when default and round-trips otherwise")
{
    auto p = projectWithTrack();
    REQUIRE (! p.tracks.empty());
    CHECK (projectToJson (p)["tracks"][0].contains ("strip") == false);

    p.tracks[0].strip.eq.enabled = true;
    p.tracks[0].strip.eq.midGainDb = 3.5;
    p.tracks[0].strip.comp.enabled = true;
    p.tracks[0].strip.comp.type = CompType::opto;
    p.tracks[0].strip.comp.thresholdDb = -24.0;

    const auto text = serialiseProject (p);
    const auto p2 = parseProject (text);   // スキーマ検証も通る
    CHECK (p2.tracks[0].strip == p.tracks[0].strip);
    CHECK (p2 == p);
}

TEST_CASE ("channel strip does not affect the bounce fingerprint")
{
    auto p = projectWithTrack();
    const auto before = trackSourceFingerprint (p.tracks[0], {});
    p.tracks[0].strip.comp.enabled = true;
    CHECK (trackSourceFingerprint (p.tracks[0], {}) == before);
}

TEST_CASE ("eq filters have the expected response")
{
    const double sr = 48000.0;
    CHECK (Biquad::peak (sr, 1000, 1.0, 6.0).magnitudeDb (sr, 1000) == doctest::Approx (6.0).epsilon (0.01));
    CHECK (Biquad::lowShelf (sr, 100, -6.0).magnitudeDb (sr, 20) == doctest::Approx (-6.0).epsilon (0.05));
    CHECK (std::abs (Biquad::lowShelf (sr, 100, -6.0).magnitudeDb (sr, 10000)) < 0.1);
    CHECK (Biquad::highShelf (sr, 8000, 4.0).magnitudeDb (sr, 20000) == doctest::Approx (4.0).epsilon (0.1));
    CHECK (Biquad::highPass (sr, 100, 0.7071).magnitudeDb (sr, 100) == doctest::Approx (-3.0).epsilon (0.05));
    CHECK (Biquad::highPass (sr, 100, 0.7071).magnitudeDb (sr, 20) < -20.0);
}

TEST_CASE ("compressor static curve")
{
    CHECK (compGainReductionDb (-30, -20, 4, 0) == 0.0);
    CHECK (compGainReductionDb (-10, -20, 4, 0) == doctest::Approx (7.5));
    CHECK (compGainReductionDb (-20, -20, 4, 4) > 0.0);   // ニーの中
}

TEST_CASE ("fet and opto compressors reduce loud signals")
{
    for (auto type : { CompType::fet, CompType::opto })
    {
        CAPTURE ((int) type);
        ChannelStripDsp dsp;
        ChannelStrip s;
        s.comp.enabled = true;
        s.comp.type = type;
        s.comp.thresholdDb = -20.0;
        s.comp.ratio = 4.0;
        dsp.setParams (s);
        dsp.prepare (48000.0);

        const double out = runSine (dsp, 200.0, 0.5, 1.5);   // -6dBFS のピーク
        CHECK (out < 0.3);
        CHECK (out > 0.05);
        CHECK (dsp.getGainReductionDb() > 3.0);

        // 閾値より十分小さい音はそのまま
        ChannelStripDsp quiet;
        quiet.setParams (s);
        quiet.prepare (48000.0);
        CHECK (runSine (quiet, 200.0, 0.01, 1.0) == doctest::Approx (0.01).epsilon (0.02));
    }
}

TEST_CASE ("disabled strip passes audio through")
{
    ChannelStripDsp dsp;
    ChannelStrip s;
    s.eq.midGainDb = 12.0;   // 無効なので効かない
    dsp.setParams (s);
    dsp.prepare (48000.0);
    CHECK (runSine (dsp, 1000.0, 0.25, 0.3) == doctest::Approx (0.25).epsilon (0.01));
}

TEST_CASE ("bus tracks, output routing and sends round-trip through JSON")
{
    auto p = projectWithTrack();
    Track bus;
    bus.id = generateUuid();
    bus.type = TrackType::bus;
    bus.name = "Drum Bus";
    p.tracks.push_back (bus);
    p.tracks[0].output = bus.id;
    p.tracks[0].sends.push_back ({ bus.id, -6.0, true });
    p.tracks[0].strip.eq.enabled = true;
    p.tracks[0].strip.eq.lowMidGainDb = -2.0;
    p.tracks[0].strip.eq.highCutHz = 12000.0;

    const auto p2 = parseProject (serialiseProject (p));   // スキーマ検証も通る
    CHECK (p2 == p);
    CHECK (p2.tracks.back().type == TrackType::bus);

    // 出力先・センドはバウンスの内容に影響しない
    auto t = p.tracks[0];
    const auto before = trackSourceFingerprint (t, {});
    t.output.clear();
    t.sends.clear();
    CHECK (trackSourceFingerprint (t, {}) == before);
}

TEST_CASE ("eq response includes all bands")
{
    ChannelEq eq;
    eq.enabled = true;
    eq.lowMidGainDb = -6.0;
    eq.lowMidFreqHz = 300.0;
    eq.highCutHz = 5000.0;
    CHECK (eqResponseDb (eq, 48000.0, 300.0) == doctest::Approx (-6.0).epsilon (0.05));
    CHECK (eqResponseDb (eq, 48000.0, 15000.0) < -12.0);
    eq.enabled = false;
    CHECK (eqResponseDb (eq, 48000.0, 300.0) == 0.0);
}

TEST_CASE ("compressor low-frequency through keeps bass from triggering compression")
{
    for (auto type : { CompType::fet, CompType::opto })
    {
        CAPTURE ((int) type);
        ChannelStrip s;
        s.comp.enabled = true;
        s.comp.type = type;
        s.comp.thresholdDb = -20.0;
        s.comp.ratio = 4.0;

        ChannelStripDsp plain;
        plain.setParams (s);
        plain.prepare (48000.0);
        const double squashed = runSine (plain, 50.0, 0.5, 1.5);

        s.comp.sidechainHpHz = 300.0;   // 50 Hz は検出にほとんど届かない
        ChannelStripDsp through;
        through.setParams (s);
        through.prepare (48000.0);
        const double passed = runSine (through, 50.0, 0.5, 1.5);

        CHECK (squashed < 0.3);
        CHECK (passed > 0.4);
    }
}

TEST_CASE ("channel strip order and low-frequency through round-trip through JSON")
{
    auto p = projectWithTrack();
    REQUIRE (! p.tracks.empty());
    p.tracks[0].strip.compFirst = true;
    p.tracks[0].strip.comp.sidechainHpHz = 120.0;

    const auto back = parseProject (serialiseProject (p));
    CHECK (back.tracks[0].strip.compFirst);
    CHECK (back.tracks[0].strip.comp.sidechainHpHz == doctest::Approx (120.0));

    // 既定値のときは書き出さない（古いアプリでも読める）
    p.tracks[0].strip.compFirst = false;
    p.tracks[0].strip.comp.sidechainHpHz = 0.0;
    p.tracks[0].strip.comp.enabled = true;
    const auto text = serialiseProject (p);
    CHECK (text.find ("sidechainHpHz") == std::string::npos);
    CHECK (text.find ("\"order\"") == std::string::npos);
}

TEST_CASE ("compressor before eq changes the result when eq boosts into the threshold")
{
    ChannelStrip s;
    s.eq.enabled = true;
    s.eq.midGainDb = 12.0;
    s.eq.midFreqHz = 1000.0;
    s.comp.enabled = true;
    s.comp.thresholdDb = -12.0;
    s.comp.ratio = 8.0;

    ChannelStripDsp eqFirst;
    eqFirst.setParams (s);
    eqFirst.prepare (48000.0);
    const double a = runSine (eqFirst, 1000.0, 0.1, 1.0);   // EQ で持ち上げてから圧縮 → 抑えられる

    s.compFirst = true;
    ChannelStripDsp compFirst;
    compFirst.setParams (s);
    compFirst.prepare (48000.0);
    const double b = runSine (compFirst, 1000.0, 0.1, 1.0);  // 小さいまま圧縮されず、あとで 12 dB 上がる

    CHECK (b > a * 1.3);   // 約 -8 dB と約 -11.5 dB
}

TEST_CASE ("track input and output channels round-trip through JSON")
{
    auto p = projectWithTrack();
    REQUIRE (! p.tracks.empty());

    // 既定（ステレオ）のときは書き出さない
    CHECK (serialiseProject (p).find ("Channels") == std::string::npos);

    p.tracks[0].outputChannels = 1;
    auto back = parseProject (serialiseProject (p));
    CHECK (back.tracks[0].outputChannels == 1);
    CHECK (back.tracks[0].inputChannels == 2);

    collab::Track audio;
    audio.id = collab::generateUuid();
    audio.type = collab::TrackType::audio;
    audio.name = "Vocal";
    audio.inputChannels = 1;
    p.tracks.push_back (audio);
    back = parseProject (serialiseProject (p));
    CHECK (back.tracks.back().inputChannels == 1);
    CHECK (back.tracks.back().outputChannels == 2);
    CHECK (back.tracks.back().crossfadeMs == doctest::Approx (10.0));

    p.tracks.back().crossfadeMs = 50.0;
    p.tracks.back().crossfadeShape = "sCurve";
    back = parseProject (serialiseProject (p));
    CHECK (back.tracks.back().crossfadeMs == doctest::Approx (50.0));
    CHECK (back.tracks.back().crossfadeShape == "sCurve");
}

TEST_CASE ("compressor does not colour a steady tone (no added harmonics)")
{
    const double sr = 48000.0, pi = 3.14159265358979323846;

    auto toneAmplitude = [sr, pi] (const std::vector<float>& x, double f)
    {
        double re = 0.0, im = 0.0;

        for (size_t i = 0; i < x.size(); ++i)
        {
            re += x[i] * std::cos (2.0 * pi * f * (double) i / sr);
            im -= x[i] * std::sin (2.0 * pi * f * (double) i / sr);
        }

        return 2.0 * std::sqrt (re * re + im * im) / (double) x.size();
    };

    for (auto type : { CompType::fet, CompType::opto })
    {
        ChannelStripDsp dsp;
        dsp.prepare (sr);
        ChannelStrip p;
        p.comp.enabled = true;
        p.comp.type = type;
        p.comp.thresholdDb = -24.0;
        p.comp.ratio = 4.0;
        dsp.setParams (p);

        const int n = (int) sr * 2;
        std::vector<float> x ((size_t) n);

        for (int i = 0; i < n; ++i)
            x[(size_t) i] = (float) (0.5 * std::sin (2.0 * pi * 60.0 * i / sr));

        for (int pos = 0; pos < n; pos += 512)
        {
            float* ch[1] = { x.data() + pos };
            dsp.process (ch, 1, std::min (512, n - pos));
        }

        CHECK (dsp.getGainReductionDb() > 8.0f);

        const std::vector<float> settled (x.begin() + (long) sr, x.end());
        double harmonics = 0.0;

        for (int k = 2; k <= 10; ++k)
            harmonics += std::pow (toneAmplitude (settled, 60.0 * k), 2.0);

        CHECK (std::sqrt (harmonics) / toneAmplitude (settled, 60.0) < 0.001);
    }
}

TEST_CASE ("inserts, EQ and compressor can be put in any order")
{
    using B = StripBlock;
    const std::array<std::array<B, 3>, 6> all { {
        { B::inserts, B::eq, B::comp }, { B::inserts, B::comp, B::eq }, { B::eq, B::inserts, B::comp },
        { B::comp, B::inserts, B::eq }, { B::eq, B::comp, B::inserts }, { B::comp, B::eq, B::inserts } } };

    for (auto& o : all)
    {
        auto p = projectWithTrack();
        REQUIRE (! p.tracks.empty());
        auto& s = p.tracks[0].strip;
        s.eq.enabled = s.comp.enabled = true;
        s.setOrder (o);
        CHECK (s.order() == o);

        // JSON を通しても同じ（サーバーの検査も通る）
        const auto back = parseProject (serialiseProject (p));
        CHECK (back.tracks[0].strip.order() == o);

        // EQ・コンプは、インサートより前に並んでいれば前の分、後なら後の分に入る
        auto indexOf = [&o] (B b) { return (int) (std::find (o.begin(), o.end(), b) - o.begin()); };
        const auto before = s.beforeInserts(), after = s.afterInserts();
        CHECK (before.eq.enabled == (indexOf (B::eq) < s.insertsAt));
        CHECK (after.eq.enabled == (indexOf (B::eq) > s.insertsAt));
        CHECK (before.comp.enabled == (indexOf (B::comp) < s.insertsAt));
        CHECK (after.comp.enabled == (indexOf (B::comp) > s.insertsAt));
    }

    // 既定（インサート → EQ → コンプ）は書き出さない
    auto p = projectWithTrack();
    p.tracks[0].strip.comp.enabled = true;
    CHECK (serialiseProject (p).find ("insertsAt") == std::string::npos);
}
