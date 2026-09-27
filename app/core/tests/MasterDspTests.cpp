#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include "collab/ChordPlayback.h"
#include "collab/MasterDsp.h"
#include "collab/TempoMap.h"
#include "collab/ProjectDiff.h"
#include "collab/ProjectJson.h"
#include "TestUtils.h"

using namespace collab;

namespace
{
    constexpr double sr = 48000.0;

    std::vector<float> sine (double freq, double amplitude, double seconds)
    {
        std::vector<float> v ((size_t) (seconds * sr));

        for (size_t i = 0; i < v.size(); ++i)
            v[i] = (float) (amplitude * std::sin (2.0 * 3.14159265358979 * freq * (double) i / sr));

        return v;
    }

    double integrated (std::vector<float> l, std::vector<float> r)
    {
        LoudnessBlocks k;
        k.prepare (sr);
        LoudnessStats stats;
        std::vector<double> blocks;
        const float* ch[2] = { l.data(), r.data() };
        k.process (ch, 2, (int) l.size(), blocks);

        for (double b : blocks)
            stats.addBlock (b);

        return stats.integratedLufs();
    }

    double peakAfterLimiter (const MasterLimiter& p, double amplitude)
    {
        VintageLimiterDsp dsp;
        dsp.setParams (p);
        dsp.prepare (sr);
        auto l = sine (440.0, amplitude, 2.0), r = l;
        float* ch[2] = { l.data(), r.data() };

        for (int pos = 0; pos < (int) l.size(); pos += 512)
        {
            float* block[2] = { ch[0] + pos, ch[1] + pos };
            dsp.process (block, 2, std::min (512, (int) l.size() - pos));
        }

        double peak = 0.0;

        for (size_t i = l.size() / 2; i < l.size(); ++i)
            peak = std::max (peak, (double) std::abs (l[i]));

        return 20.0 * std::log10 (peak);
    }
}

TEST_CASE ("Loudness: 1 kHz sine at 0 dBFS on both channels is about 0 LUFS")
{
    // BS.1770: 1 kHz, 0 dBFS のサインを片チャンネルに流すと -3.01 LUFS（両方なら約 0）
    auto s = sine (1000.0, 1.0, 5.0);
    CHECK (std::abs (integrated (s, s)) < 0.1);
    CHECK (std::abs (integrated (s, std::vector<float> (s.size(), 0.0f)) - -3.01) < 0.1);

    auto quiet = sine (1000.0, std::pow (10.0, -20.0 / 20.0), 5.0);
    CHECK (std::abs (integrated (quiet, quiet) - -20.0) < 0.1);
}

TEST_CASE ("Loudness: silence is gated out")
{
    auto s = sine (1000.0, 0.1, 3.0);
    s.resize (s.size() * 2, 0.0f);   // 後半は無音
    const double withSilence = integrated (s, s);
    auto loud = sine (1000.0, 0.1, 3.0);
    CHECK (std::abs (withSilence - integrated (loud, loud)) < 0.3);
}

TEST_CASE ("Vintage limiter never exceeds the ceiling")
{
    for (auto mode : { LimiterMode::analog, LimiterMode::tube, LimiterMode::modern })
        for (double character : { 0.0, 5.0, 10.0 })
        {
            MasterLimiter p;
            p.enabled = true;
            p.mode = mode;
            p.character = character;
            p.ceilingDb = -1.0;
            p.thresholdDb = -12.0;
            CHECK (peakAfterLimiter (p, 1.0) <= -1.0 + 0.01);
        }
}

TEST_CASE ("Vintage limiter leaves quiet material alone")
{
    MasterLimiter p;
    p.enabled = true;
    p.mode = LimiterMode::modern;
    p.thresholdDb = 0.0;
    p.ceilingDb = -0.3;
    CHECK (std::abs (peakAfterLimiter (p, 0.25) - 20.0 * std::log10 (0.25)) < 0.2);
}

TEST_CASE ("Master limiter settings round-trip through JSON and diff as their own scope")
{
    auto p = parseProject (fixture ("full.project.json"));
    CHECK (p.master.id == masterBusIdFor (p.projectId));
    CHECK (nlohmann::json::parse (serialiseProject (p)).count ("master") == 0);   // 既定値は省略

    auto q = p;
    q.master.limiter.enabled = true;
    q.master.limiter.thresholdDb = -6.5;
    q.master.limiter.mode = LimiterMode::tube;
    const auto text = serialiseProject (q);
    CHECK (validateProjectJson (nlohmann::json::parse (text)).empty());
    CHECK (parseProject (text).master == q.master);

    auto d = diffProjects (p, q);
    REQUIRE (d.changes.size() == 1);
    CHECK (d.changes[0].scopeKind == ScopeKind::master);
    CHECK (d.touches (p.master.id));
}

TEST_CASE ("Key track round-trips through JSON, diffs as its own scope and answers keyAt")
{
    auto p = parseProject (fixture ("full.project.json"));
    CHECK (p.keyTrack.id == keyTrackIdFor (p.projectId));
    CHECK (nlohmann::json::parse (serialiseProject (p)).count ("keyTrack") == 0);   // 空は省略
    CHECK_FALSE (keyAt (p, TempoMap (p), 0));

    auto q = p;
    q.keyTrack.events.push_back ({ "11111111-1111-4111-8111-11111111aaaa", 1, 7, false });   // G
    q.keyTrack.events.push_back ({ "11111111-1111-4111-8111-11111111bbbb", 9, 4, true });    // Em（9 小節目から）
    const auto text = serialiseProject (q);
    CHECK (validateProjectJson (nlohmann::json::parse (text)).empty());
    auto r = parseProject (text);
    CHECK (r.keyTrack == q.keyTrack);

    const TempoMap map (r);
    CHECK (keyAt (r, map, 0) == chord::Key { 7, false });
    CHECK (keyAt (r, map, map.barToTick (9)) == chord::Key { 4, true });
    CHECK (keyAt (r, map, map.barToTick (9) - 1) == chord::Key { 7, false });

    auto d = diffProjects (p, q);
    REQUIRE (d.changes.size() == 2);
    CHECK (d.changes[0].scopeKind == ScopeKind::key);
    CHECK (d.changes[0].summary.find ("G") != std::string::npos);
}
