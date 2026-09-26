#include <doctest/doctest.h>

#include "collab/ProjectJson.h"
#include "collab/Render.h"
#include "TestUtils.h"

using namespace collab;

namespace
{
    Track lead()
    {
        auto p = parseProject (fixture ("full.project.json"));
        auto t = p.tracks[1];   // Synth Lead（外部プラグイン）
        t.midiClips.push_back ({ "c", 0, 3840, { { "n", 0, 480, 60, 100 } } });
        return t;
    }

    std::string state = "state-v1";
    auto stateHash = [] (const std::string&) { return state; };
}

TEST_CASE ("tracks with external plugins need a render")
{
    auto p = parseProject (fixture ("full.project.json"));
    CHECK_FALSE (usesExternalPlugin (p.tracks[0]));   // 内蔵音源
    CHECK (usesExternalPlugin (p.tracks[1]));
    CHECK (renderStatus (p.tracks[0], "x") == RenderStatus::notNeeded);

    auto t = lead();
    t.render.reset();
    CHECK (renderStatus (t, "x") == RenderStatus::missing);
}

TEST_CASE ("fingerprint changes with content and plugin state, not with mixer settings")
{
    state = "state-v1";
    auto t = lead();
    const auto fp = trackSourceFingerprint (t, stateHash);
    CHECK (fp.size() == 64);

    t.render->sourceFingerprint = fp;
    CHECK (renderStatus (t, trackSourceFingerprint (t, stateHash)) == RenderStatus::upToDate);

    auto mixer = t;
    mixer.name = "renamed";
    mixer.volumeDb = -6;
    mixer.pan = 0.5;
    mixer.mute = true;
    mixer.color = "#000000";
    mixer.render->audioHash = std::string (64, 'e');
    CHECK (trackSourceFingerprint (mixer, stateHash) == fp);

    auto notes = t;
    notes.midiClips[0].notes[0].velocity = 90;
    CHECK (trackSourceFingerprint (notes, stateHash) != fp);
    CHECK (renderStatus (notes, trackSourceFingerprint (notes, stateHash)) == RenderStatus::stale);

    auto fx = t;
    fx.effects[0].bypass = true;
    CHECK (trackSourceFingerprint (fx, stateHash) != fp);

    state = "state-v2";   // プラグインの設定を変えた
    CHECK (trackSourceFingerprint (t, stateHash) != fp);
    state = "state-v1";
}
