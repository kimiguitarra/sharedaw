#include <doctest/doctest.h>

#include "collab/BuiltinInstruments.h"
#include "TestUtils.h"

using namespace collab;

namespace
{
    BuiltinInstrumentManifest loadManifest (const std::string& id, const std::string& version)
    {
        auto text = readTextFile (std::string (COLLAB_ASSETS_DIR) + "/instruments/" + id + "/" + version + "/manifest.json");
        return BuiltinInstrumentManifest::fromJson (nlohmann::json::parse (text));
    }

    bool fileExists (const std::string& path)
    {
        return std::ifstream (path).good();
    }
}

TEST_CASE ("bundled manifests are valid and reference existing files")
{
    for (auto id : { builtin::drums, builtin::bass, builtin::piano })
    {
        CAPTURE (id);
        auto m = loadManifest (id, "0.1.0");
        CHECK (m.id == id);
        const auto dir = std::string (COLLAB_ASSETS_DIR) + "/instruments/" + id + "/0.1.0/";

        if (m.type == "melodic")
            CHECK (fileExists (dir + m.mainSfz));

        for (auto& s : m.samples)
        {
            CAPTURE (s);
            CHECK (fileExists (dir + "samples/" + s + ".sfz"));
        }

        for (auto& [kit, pieces] : m.kits)
            for (auto& [piece, sample] : pieces)
            {
                CAPTURE (kit);
                CHECK (m.findPiece (piece) != nullptr);
                CHECK (std::find (m.samples.begin(), m.samples.end(), sample) != m.samples.end());
            }
    }
}

TEST_CASE ("drum params resolve with defaults and overrides")
{
    auto m = loadManifest (builtin::drums, "0.1.0");
    auto params = nlohmann::json::parse (R"({ "kit": "synth", "pieces": { "snare": { "sample": "synth/snare_02", "volumeDb": -2.0, "pan": 0.5, "tune": 1 },
                                                                           "kick":  { "sample": "does/not_exist" } } })");
    auto r = resolveInstrumentParams (m, params);
    CHECK (r.kit == "synth");
    CHECK (r.pieces.at ("snare").sample == "synth/snare_02");
    CHECK (r.pieces.at ("snare").volumeDb == -2.0);
    CHECK (r.pieces.at ("kick").sample == "synth/kick_01");      // 不明なサンプルは既定に戻す
    CHECK (r.pieces.at ("rim").sample == "synth/rim_01");

    auto sfz = generateSfz (m, params);
    CHECK (sfz.find ("<master> key=38 volume=-2 pan=50 tune=100") != std::string::npos);
    CHECK (sfz.find ("#include \"samples/synth/snare_02.sfz\"") != std::string::npos);
    CHECK (sfz.find ("off_by=2") != std::string::npos);
}

TEST_CASE ("unknown kit falls back to the first kit")
{
    auto m = loadManifest (builtin::drums, "0.1.0");
    auto r = resolveInstrumentParams (m, nlohmann::json::parse (R"({ "kit": "nope" })"));
    CHECK (m.kits.count (r.kit) == 1);
}

TEST_CASE ("melodic instrument sfz with tone")
{
    auto m = loadManifest (builtin::piano, "0.1.0");
    auto sfz = generateSfz (m, nlohmann::json::parse (R"({ "tone": 3 })"));
    CHECK (sfz.find ("eq1_gain=3") != std::string::npos);
    CHECK (sfz.find ("#include \"piano.sfz\"") != std::string::npos);

    auto plain = generateSfz (m, nlohmann::json::object());
    CHECK (plain.find ("eq1_") == std::string::npos);
}
