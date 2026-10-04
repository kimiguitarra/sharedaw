#include <doctest/doctest.h>

#include "collab/ProjectJson.h"
#include "collab/Uuid.h"
#include "TestUtils.h"

using namespace collab;

TEST_CASE ("fixtures load and round-trip")
{
    for (auto name : { "minimal.project.json", "full.project.json", "demo-project/project.json" })
    {
        CAPTURE (name);
        const auto p = parseProject (fixture (name));
        const auto text = serialiseProject (p);
        const auto p2 = parseProject (text);
        CHECK (p == p2);
        CHECK (serialiseProject (p2) == text);    // 決定的な出力
    }
}

TEST_CASE ("full fixture content")
{
    const auto p = parseProject (fixture ("full.project.json"));
    REQUIRE (p.tracks.size() == 3);
    CHECK (p.name == "フル機能のテスト");
    CHECK (p.tempoTrack.events.size() == 2);
    CHECK (p.meterTrack.events[1].denominator == 8);

    auto& drums = p.tracks[0];
    REQUIRE (drums.instrument);
    CHECK (drums.instrument->kind == Instrument::Kind::builtin);
    CHECK (drums.instrument->params["pieces"]["snare"]["sample"] == "synth/snare_02");
    REQUIRE (drums.midiClips.size() == 1);
    // ノートは開始 tick 順に並び替えられる
    CHECK (drums.midiClips[0].notes[0].pitch == 36);
    CHECK (drums.midiClips[0].notes[1].pitch == 38);

    auto& lead = p.tracks[1];
    CHECK (lead.instrument->kind == Instrument::Kind::external);
    CHECK (lead.instrument->plugin.format == "VST3");
    REQUIRE (lead.render);
    CHECK (lead.render->tailSeconds == 2.0);
    CHECK (lead.effects.size() == 1);

    auto& gt = p.tracks[2];
    CHECK (gt.type == TrackType::audio);
    CHECK (gt.mute);
    CHECK (gt.audioClips[0].lengthSamples == 480000);

    CHECK (p.chordTrack.events[1].chord->bass == std::optional<std::string> ("G"));
    CHECK (p.chordTrack.events[2].noChord);
}

TEST_CASE ("serialised key order starts with schemaVersion")
{
    auto text = serialiseProject (Project::createEmpty ("x"));
    CHECK (text.rfind ("{\n  \"schemaVersion\": 1,", 0) == 0);
    CHECK (text.back() == '\n');
}

TEST_CASE ("invalid fixtures are rejected")
{
    CHECK_THROWS_AS (parseProject (fixture ("invalid/bad-pitch.project.json")), ProjectFormatError);
    CHECK_THROWS_AS (parseProject (fixture ("invalid/absolute-stateref.project.json")), ProjectFormatError);
    CHECK_THROWS_AS (parseProject ("{ not json"), ProjectFormatError);
    CHECK_THROWS_AS (parseProject ("{}"), ProjectFormatError);
}

TEST_CASE ("names are stored as NFC")
{
    auto p = Project::createEmpty ("\xe3\x81\x8b\xe3\x82\x99");   // NFD の「が」
    Track t;
    t.id = generateUuid();
    t.name = "\xe3\x81\xaf\xe3\x82\x9a";                          // NFD の「ぱ」
    t.instrument = Instrument { Instrument::Kind::builtin, "builtin.piano", "0.1.0", nlohmann::json::object(), {}, {} };
    p.tracks.push_back (t);

    const auto p2 = parseProject (serialiseProject (p));
    CHECK (p2.name == "\xe3\x81\x8c");
    CHECK (p2.tracks[0].name == "\xe3\x81\xb1");
}

TEST_CASE ("builtin params keys are canonicalised")
{
    auto p = Project::createEmpty ("x");
    Track t;
    t.id = generateUuid();
    t.instrument = Instrument { Instrument::Kind::builtin, "builtin.drums", "0.1.0",
                                nlohmann::json::parse (R"({"pieces":{"snare":{"tune":1,"pan":0}},"kit":"synth"})"), {}, {} };
    p.tracks.push_back (t);

    const auto text = serialiseProject (p);
    CHECK (text.find ("\"kit\"") < text.find ("\"pieces\""));
    CHECK (text.find ("\"pan\"") < text.find ("\"tune\""));
}

TEST_CASE ("schema is embedded and usable")
{
    CHECK (projectSchema().contains ("definitions"));
    CHECK (validateProjectJson (nlohmann::json::parse (fixture ("full.project.json"))).empty());
    CHECK_FALSE (validateProjectJson (nlohmann::json::parse (fixture ("invalid/bad-pitch.project.json"))).empty());
}

TEST_CASE ("master effects round-trip and are found by effectsFor")
{
    auto p = parseProject (fixture ("full.project.json"));
    p.master.id = masterBusIdFor (p.projectId);

    Effect e;
    e.id = generateUuid();
    e.builtin = "busComp";
    p.master.effects.push_back (e);

    REQUIRE (p.effectsFor (p.master.id) == &p.master.effects);
    CHECK (p.effectsFor (p.tracks[0].id) == &p.tracks[0].effects);
    CHECK (p.effectsFor ("nope") == nullptr);
    CHECK_FALSE (p.master.isDefault());

    const auto back = parseProject (serialiseProject (p));
    CHECK (back.master.effects == p.master.effects);
    CHECK (back == p);
}

TEST_CASE ("pitch bends round-trip as [tick, value] pairs")
{
    auto p = parseProject (fixture ("full.project.json"));
    auto& t = p.tracks[0];
    REQUIRE (! t.midiClips.empty());
    t.midiClips[0].pitchBends = { { 0, -8192 }, { 480, 0 }, { 960, 8191 } };

    const auto text = serialiseProject (p);
    CHECK (text.find ("\"pitchBends\"") != std::string::npos);
    CHECK (parseProject (text) == p);

    t.midiClips[0].pitchBends = { { 0, 9000 } };   // 範囲外はスキーマで弾く
    CHECK_THROWS (parseProject (serialiseProject (p)));
}
