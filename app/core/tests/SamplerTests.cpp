#include <doctest/doctest.h>

#include <cmath>

#include "collab/Sampler.h"
#include "collab/BuiltinInstruments.h"

using namespace collab;

TEST_CASE ("sampler pads round-trip through params and become SFZ regions for the audio that exists")
{
    auto pads = samplerPads (nlohmann::json::object());
    REQUIRE (pads.size() == 16);
    CHECK (pads[0].note == 36);    // C2
    CHECK (pads[1].note == 38);    // D2: 白鍵だけ
    CHECK (pads[7].note == 48);    // C3
    CHECK (pads[15].note == 62);   // D4
    CHECK (pads[3].audioHash.empty());

    pads[0] = { 36, "aaa", "Kick", -3.0, 0.0, 0.0, true, 0 };
    pads[1] = { 38, "bbb", "Hat closed", 0.0, 0.25, 0.0, true, 1 };
    pads[2] = { 40, "ccc", "Hat open", 0.0, 0.0, -2.5, false, 1 };
    pads[3] = { 41, "missing", "Clap", 0.0, 0.0, 0.0, true, 0 };
    pads[4] = { 43, "ddd", "Loop 98", 0.0, 0.0, 0.0, false, 0, 98.0, 4800, 96000, 3.0, -2.0, 2500.0, 1.5, 6.0 };

    const auto params = withSamplerPads ({ { "volumeDb", -2.0 } }, pads);
    CHECK (params["volumeDb"] == -2.0);
    CHECK (samplerPads (params) == pads);
    CHECK (params["pads"][4]["bpm"] == 98.0);
    CHECK (params["pads"][4]["drive"] == 6.0);

    // 前の版で保存したノート（半音ずつ）は、パッドの番号の白鍵に直す
    auto old = params;
    old["pads"][1]["note"] = 37;
    CHECK (samplerPads (old)[1].note == 38);

    const auto sfz = generateSamplerSfz (params, [] (const SamplerPad& p) { const auto& h = p.audioHash; return h == "missing" ? std::string() : "C:\\audio\\" + h + ".wav"; });
    CHECK (sfz.find ("sample=C:/audio/aaa.wav key=36") != std::string::npos);
    CHECK (sfz.find ("volume=-3") != std::string::npos);
    CHECK (sfz.find ("key=38") != std::string::npos);
    CHECK (sfz.find ("pan=25") != std::string::npos);
    CHECK (sfz.find ("group=101 off_by=101") != std::string::npos);
    CHECK (sfz.find ("transpose") == std::string::npos);   // 高さは加工したファイルで変える（長さを変えない）
    CHECK (sfz.find ("missing") == std::string::npos);     // ない実体のパッドは鳴らさない

    // 同期で送る実体にパッドの音も入る
    Project p = Project::createEmpty ("t");
    Track t;
    t.id = "t1";
    t.type = TrackType::midi;
    t.instrument = Instrument { Instrument::Kind::builtin, builtin::sampler, "1.0.0", params, {}, {} };
    p.tracks.push_back (t);
    const auto refs = referencedAudio (p);
    CHECK (refs.size() == 5);
}

TEST_CASE ("sampler pads are rendered only when edited, and the key follows the settings")
{
    SamplerPad pad;
    pad.audioHash = "aaa";
    CHECK_FALSE (padNeedsRender (pad, 1.0));
    CHECK (padNeedsRender (pad, 1.05));

    auto edited = pad;
    edited.eqHighDb = 3.0;
    CHECK (padNeedsRender (edited, 1.0));
    CHECK (padRenderKey (edited, 1.0) != padRenderKey (pad, 1.0));
    CHECK (padRenderKey (edited, 1.0) == padRenderKey (edited, 1.0));
    CHECK (padRenderKey (edited, 1.0).size() == 16);

    edited = pad;
    edited.startSamples = 100;
    CHECK (padNeedsRender (edited, 1.0));
}

TEST_CASE ("pad EQ changes the tone and saturation keeps the loudness")
{
    constexpr double sr = 48000.0;
    auto sine = [] (double freq)
    {
        std::vector<std::vector<float>> ch (1, std::vector<float> ((size_t) sr));

        for (size_t i = 0; i < ch[0].size(); ++i)
            ch[0][i] = (float) (0.5 * std::sin (2.0 * 3.141592653589793 * freq * (double) i / sr));

        return ch;
    };
    auto rms = [] (const std::vector<float>& x, size_t from)
    {
        double s = 0.0;

        for (size_t i = from; i < x.size(); ++i)
            s += (double) x[i] * x[i];

        return std::sqrt (s / (double) (x.size() - from));
    };

    SamplerPad pad;
    pad.eqHighDb = 6.0;
    auto high = sine (12000.0);
    const double before = rms (high[0], 4800);
    processPadAudio (high, sr, pad);
    CHECK (20.0 * std::log10 (rms (high[0], 4800) / before) == doctest::Approx (6.0).epsilon (0.1));

    auto low = sine (100.0);
    processPadAudio (low, sr, pad);
    CHECK (20.0 * std::log10 (rms (low[0], 4800) / before) == doctest::Approx (0.0).epsilon (0.05));

    SamplerPad drive;
    drive.driveDb = 18.0;
    auto x = sine (220.0);
    const double dryRms = rms (x[0], 0);
    processPadAudio (x, sr, drive);
    CHECK (rms (x[0], 0) == doctest::Approx (dryRms).epsilon (0.01));
}
