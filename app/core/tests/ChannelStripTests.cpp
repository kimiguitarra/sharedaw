#include <doctest/doctest.h>

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
