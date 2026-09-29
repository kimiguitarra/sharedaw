#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "collab/BuiltinEffects.h"
#include "collab/ProjectJson.h"
#include "collab/Render.h"
#include "collab/Uuid.h"
#include "TestUtils.h"

using namespace collab::fx;

namespace
{
    constexpr double sr = 48000.0;
    constexpr double pi = 3.14159265358979323846;

    struct Stereo
    {
        std::vector<float> l, r;
        explicit Stereo (size_t n) : l (n, 0.0f), r (n, 0.0f) {}
        float* ptrs[2];
        float* const* channels()  { ptrs[0] = l.data(); ptrs[1] = r.data(); return ptrs; }
    };

    Stereo sine (double freq, double amp, double seconds)
    {
        Stereo s ((size_t) (seconds * sr));

        for (size_t i = 0; i < s.l.size(); ++i)
            s.l[i] = s.r[i] = (float) (amp * std::sin (2.0 * pi * freq * (double) i / sr));

        return s;
    }

    double rms (const std::vector<float>& x, size_t from, size_t to)
    {
        double sum = 0;
        for (size_t i = from; i < to; ++i) sum += (double) x[i] * x[i];
        return std::sqrt (sum / (double) (to - from));
    }

    std::unique_ptr<Processor> make (Type t, nlohmann::json params)
    {
        auto p = createProcessor (t);
        p->prepare (sr);
        p->setParams (params);
        return p;
    }

    /** freq の成分の振幅（単一周波数の DFT）。 */
    double amplitudeAt (const std::vector<float>& x, size_t from, size_t to, double freq)
    {
        double re = 0, im = 0;

        for (size_t i = from; i < to; ++i)
        {
            re += x[i] * std::cos (2.0 * pi * freq * (double) i / sr);
            im += x[i] * std::sin (2.0 * pi * freq * (double) i / sr);
        }

        return 2.0 * std::sqrt (re * re + im * im) / (double) (to - from);
    }
}

TEST_CASE ("builtin effect ids and defaults")
{
    for (auto t : allTypes())
    {
        CHECK (typeFromId (idOf (t)) == t);
        CHECK_FALSE (paramSpecs (t).empty());
        const auto d = defaultParams (t, false);

        for (auto& s : paramSpecs (t))
            CHECK (paramValue (d, s) == doctest::Approx (s.def));
    }

    CHECK (defaultParams (Type::hallReverb, true)["mix"].get<double>() == doctest::Approx (100.0));
    CHECK (defaultParams (Type::hallReverb, false)["mix"].get<double>() == doctest::Approx (30.0));
    CHECK_FALSE (typeFromId ("nope"));
}

TEST_CASE ("bus compressor reduces level above threshold by the ratio")
{
    // -6 dBFS の正弦波、スレッショルド -20 dB（ピーク検出）、4:1 → 超えた 14 dB が 3.5 dB に（10.5 dB 下がる）
    auto s = sine (1000.0, 0.5, 1.0);
    auto comp = make (Type::busComp, { { "threshold", -20.0 }, { "ratio", 1 }, { "attack", 2 }, { "release", 1 } });
    comp->process (s.channels(), 2, (int) s.l.size());

    const double outDb = 20.0 * std::log10 (rms (s.l, 24000, 48000) * std::sqrt (2.0));
    CHECK (outDb == doctest::Approx (-6.0 - 10.5).epsilon (0.12));
    CHECK (comp->getGainReductionDb() > 8.0f);

    // MIX 0% なら元のまま
    auto dry = sine (1000.0, 0.5, 0.2);
    auto off = make (Type::busComp, { { "threshold", -20.0 }, { "mix", 0.0 } });
    const auto before = dry.l;
    off->process (dry.channels(), 2, (int) dry.l.size());
    CHECK (dry.l[5000] == doctest::Approx (before[5000]));
}

TEST_CASE ("saturator adds even harmonics and stays bounded")
{
    auto s = sine (500.0, 0.8, 0.5);
    auto sat = make (Type::saturator, { { "drive", 18.0 }, { "warmth", 100.0 } });
    sat->process (s.channels(), 2, (int) s.l.size());

    const double fundamental = amplitudeAt (s.l, 4800, 24000, 500.0);
    const double second = amplitudeAt (s.l, 4800, 24000, 1000.0);
    CHECK (fundamental > 0.05);
    CHECK (second / fundamental > 0.01);   // 偶数次（温かさ）

    double peak = 0, mean = 0;
    for (size_t i = 4800; i < s.l.size(); ++i) { peak = std::max (peak, (double) std::abs (s.l[i])); mean += s.l[i]; }
    CHECK (peak < 1.5);
    CHECK (std::abs (mean / (double) (s.l.size() - 4800)) < 0.01);   // 直流は取れている
}

TEST_CASE ("reverbs ring out for about the decay time and stay stable")
{
    for (auto t : { Type::roomReverb, Type::hallReverb, Type::plateReverb })
    {
        const double decay = t == Type::roomReverb ? 0.6 : 2.0;
        auto rev = make (t, { { "decay", decay }, { "predelay", 0.0 }, { "mix", 100.0 }, { "damping", 20000.0 }, { "lowCut", 20.0 } });
        Stereo s ((size_t) (sr * (decay * 2.0 + 1.0)));
        s.l[10] = s.r[10] = 1.0f;
        rev->process (s.channels(), 2, (int) s.l.size());

        bool finite = true;
        for (auto v : s.l) finite = finite && std::isfinite (v);
        CHECK (finite);

        // 響きの始め（50〜150 ms）と、残響時間の所の大きさ: おおよそ 60 dB（±20 dB）下がっている
        const double early = rms (s.l, (size_t) (0.05 * sr), (size_t) (0.15 * sr));
        const size_t at = (size_t) (decay * sr);
        const double late = rms (s.l, at, at + (size_t) (0.1 * sr));
        const double drop = 20.0 * std::log10 (early / std::max (late, 1.0e-12));
        CHECK (early > 1.0e-4);
        CHECK (drop > 40.0);
        CHECK (drop < 85.0);

        // 左右で違う（広がりがある）
        CHECK (rms (s.l, 4800, 24000) > 0);
        double diff = 0;
        for (size_t i = 4800; i < 24000; ++i) diff += std::abs (s.l[i] - s.r[i]);
        CHECK (diff > 1.0e-3);
    }
}

TEST_CASE ("builtin effects round-trip through JSON and do not need a bounce")
{
    auto p = collab::parseProject (fixture ("full.project.json"));
    REQUIRE (! p.tracks.empty());

    collab::Effect e;
    e.id = collab::generateUuid();
    e.builtin = idOf (Type::plateReverb);
    e.params = defaultParams (Type::plateReverb, false);
    e.params["decay"] = 3.5;
    p.tracks[0].effects.push_back (e);

    const auto back = collab::parseProject (collab::serialiseProject (p));   // スキーマの検証も通る
    REQUIRE (back.tracks[0].effects.size() == 1);
    CHECK (back.tracks[0].effects[0].builtin == "plateReverb");
    CHECK (back.tracks[0].effects[0].params.at ("decay").get<double>() == doctest::Approx (3.5));
    CHECK (back.tracks[0].effects[0] == p.tracks[0].effects[0]);
    CHECK_FALSE (collab::usesExternalPlugin (back.tracks[0]));
}

TEST_CASE ("factory presets only use known parameters within range")
{
    for (auto t : allTypes())
    {
        CHECK_FALSE (factoryPresets (t).empty());

        for (auto& preset : factoryPresets (t))
            for (auto& [key, value] : preset.params.items())
            {
                const auto& specs = paramSpecs (t);
                auto it = std::find_if (specs.begin(), specs.end(), [&] (auto& s) { return s.key == key; });
                REQUIRE (it != specs.end());
                CHECK (value.get<double>() >= it->min);
                CHECK (value.get<double>() <= it->max);
            }
    }

    CHECK (tailSeconds (Type::hallReverb, { { "decay", 4.0 }, { "predelay", 100.0 } }) == doctest::Approx (6.1));
}
