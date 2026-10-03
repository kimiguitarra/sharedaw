#include <sstream>

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
    for (auto [id, version] : { std::pair (builtin::drums, "0.1.0"), std::pair (builtin::bass, "0.1.0"), std::pair (builtin::piano, "0.1.0"),
                                std::pair (builtin::drums, "1.0.0"), std::pair (builtin::bass, "1.0.0"), std::pair (builtin::piano, "1.0.0"),
                                std::pair (builtin::drums, "1.1.0"), std::pair (builtin::drums, "1.2.0"), std::pair (builtin::drums, "1.3.0"), std::pair (builtin::bass, "3.2.0"), std::pair (builtin::bass, "3.3.0"),
                                std::pair (builtin::piano, "1.1.0") })
    {
        CAPTURE (id);
        CAPTURE (version);
        auto m = loadManifest (id, version);
        CHECK (m.id == id);
        CHECK (m.version == version);
        const auto dir = std::string (COLLAB_ASSETS_DIR) + "/instruments/" + id + "/" + m.sampleVersion() + "/";

        if (m.type == "melodic" && ! m.mainSfz.empty())
            CHECK (fileExists (dir + m.mainSfz));

        for (auto& p : m.presets)
            CHECK (fileExists (dir + p.sfz));

        for (auto& s : m.samples)
        {
            CAPTURE (s);
            CHECK (fileExists (dir + "samples/" + s + ".sfz"));

            // サンプルのファイル（sample= はマニフェストのフォルダからの相対パス）
            std::istringstream in (readTextFile (dir + "samples/" + s + ".sfz"));
            std::string token;

            while (in >> token)
                if (token.rfind ("sample=", 0) == 0 && token[7] != '*')
                {
                    CAPTURE (token);
                    CHECK (fileExists (dir + token.substr (7)));
                }
        }

        for (auto& [kit, pieces] : m.kits)
            for (auto& [piece, sample] : pieces)
            {
                CAPTURE (kit);
                CHECK (m.findPiece (piece) != nullptr);
                CHECK (std::find (m.samples.begin(), m.samples.end(), sample) != m.samples.end());
            }

        for (auto& p : m.pieces)
            for (auto& [kit, map] : m.kits)
            {
                CAPTURE (p.key);
                CHECK (map.count (p.key) == 1);   // どのキットでも全パーツが鳴る
            }
    }
}

TEST_CASE ("melodic presets select the sfz")
{
    auto m = loadManifest (builtin::bass, "1.0.0");
    CHECK (generateSfz (m, nlohmann::json::object()).find ("#include \"fingered.sfz\"") != std::string::npos);
    CHECK (generateSfz (m, nlohmann::json::parse (R"({ "preset": "synth" })")).find ("#include \"synth.sfz\"") != std::string::npos);
    CHECK (resolveInstrumentParams (m, nlohmann::json::parse (R"({ "preset": "nope" })")).preset == "fingered");
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

TEST_CASE ("renamed kits of an older version resolve to the new names")
{
    auto m = loadManifest (builtin::drums, "1.2.0");
    CHECK (m.kitOrder == std::vector<std::string> { "PopAcoustic", "JazzAcoustic", "Electronic" });
    CHECK (resolveInstrumentParams (m, nlohmann::json::parse (R"({ "kit": "acoustic" })")).kit == "PopAcoustic");
    CHECK (resolveInstrumentParams (m, nlohmann::json::parse (R"({ "kit": "electronic" })")).kit == "Electronic");
    CHECK (resolveInstrumentParams (m, nlohmann::json::parse (R"({ "kit": "nope" })")).kit == "PopAcoustic");
    CHECK (resolveInstrumentParams (m, nlohmann::json::object()).kit == "PopAcoustic");
    CHECK (resolveInstrumentParams (m, nlohmann::json::parse (R"({ "kit": "JazzAcoustic" })")).pieces.at ("ride2").sample == "virtuosity/flatRide");
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

TEST_CASE ("drums 1.3.0 also play Superior Drummer 3 notes")
{
    auto m = loadManifest (builtin::drums, "1.3.0");
    std::string name;

    // クローズ・エッジ（22）とタイト（62）はクローズのハイハット、オープンの段階（24・25・26・60）はオープン
    REQUIRE (m.findPieceForNote (22, &name) != nullptr);
    CHECK (m.findPieceForNote (22)->key == "hhClosed");
    CHECK (name == "ハイハット（クローズ・エッジ）");
    CHECK (m.findPieceForNote (62)->key == "hhClosed");
    CHECK (m.findPieceForNote (21)->key == "hhPedal");

    for (int note : { 24, 25, 26, 60 })
        CHECK (m.findPieceForNote (note)->key == "hhOpen");

    // GM と同じノートはそのまま（39 はクラップのまま）
    CHECK (m.findPieceForNote (42, &name)->key == "hhClosed");
    CHECK (name == "ハイハット（クローズ）");
    CHECK (m.findPieceForNote (39)->key == "clap");
    CHECK (m.findPieceForNote (0) == nullptr);

    // 別名のノートも同じチョークのグループで鳴る（クローズで、オープンの余韻が止まる）
    const auto sfz = generateSfz (m, {});
    CHECK (sfz.find ("<master> key=22 ") != std::string::npos);
    const auto open = sfz.find ("<master> key=24 ");
    REQUIRE (open != std::string::npos);
    CHECK (sfz.substr (open, sfz.find ('\n', open) - open).find ("off_by=2") != std::string::npos);
}

TEST_CASE ("bass 3.3.0 raises each preset to the level of the other instruments")
{
    auto m = loadManifest (builtin::bass, "3.3.0");
    CHECK (m.sampleVersion() == "3.2.0");

    for (auto [preset, volume] : { std::pair ("jazz-finger", "volume=12"), std::pair ("upright", "volume=8.5") })
    {
        CAPTURE (preset);
        const auto sfz = generateSfz (m, { { "preset", preset } });
        CHECK (sfz.find ("<master> " + std::string (volume)) != std::string::npos);
    }

    // 補正のない版は今までどおり（同じ曲を開いた人どうしで音が変わらない）
    CHECK (generateSfz (loadManifest (builtin::bass, "3.2.0"), {}).find ("volume=") == std::string::npos);
}
