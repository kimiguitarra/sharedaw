#include <doctest/doctest.h>

#include "collab/Sampler.h"
#include "collab/BuiltinInstruments.h"

using namespace collab;

TEST_CASE ("sampler pads round-trip through params and become SFZ regions for the audio that exists")
{
    auto pads = samplerPads (nlohmann::json::object());
    REQUIRE (pads.size() == 16);
    CHECK (pads[0].note == 36);
    CHECK (pads[15].note == 51);
    CHECK (pads[3].audioHash.empty());

    pads[0] = { 36, "aaa", "Kick", -3.0, 0.0, 0.0, true, 0 };
    pads[1] = { 42, "bbb", "Hat closed", 0.0, 0.25, 0.0, true, 1 };
    pads[2] = { 46, "ccc", "Hat open", 0.0, 0.0, -2.5, false, 1 };
    pads[3] = { 39, "missing", "Clap", 0.0, 0.0, 0.0, true, 0 };

    const auto params = withSamplerPads ({ { "volumeDb", -2.0 } }, pads);
    CHECK (params["volumeDb"] == -2.0);
    CHECK (samplerPads (params) == pads);

    const auto sfz = generateSamplerSfz (params, [] (const std::string& h) { return h == "missing" ? std::string() : "C:\\audio\\" + h + ".wav"; });
    CHECK (sfz.find ("sample=C:/audio/aaa.wav key=36") != std::string::npos);
    CHECK (sfz.find ("volume=-3") != std::string::npos);
    CHECK (sfz.find ("key=42") != std::string::npos);
    CHECK (sfz.find ("pan=25") != std::string::npos);
    CHECK (sfz.find ("group=101 off_by=101") != std::string::npos);
    CHECK (sfz.find ("transpose=-2 tune=-50") != std::string::npos);
    CHECK (sfz.find ("missing") == std::string::npos);   // ない実体のパッドは鳴らさない

    // 同期で送る実体にパッドの音も入る
    Project p = Project::createEmpty ("t");
    Track t;
    t.id = "t1";
    t.type = TrackType::midi;
    t.instrument = Instrument { Instrument::Kind::builtin, builtin::sampler, "1.0.0", params, {}, {} };
    p.tracks.push_back (t);
    const auto refs = referencedAudio (p);
    CHECK (refs.size() == 4);
}
