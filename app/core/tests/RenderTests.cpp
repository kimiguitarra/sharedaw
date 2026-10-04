#include <doctest/doctest.h>

#include "collab/ProjectDiff.h"
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

TEST_CASE ("bouncing makes an audio track below the source; plugin tracks keep playing and the bounce is hidden on the owner's PC")
{
    setOwnedPluginTrackCheck ([] (const Track&) { return true; });
    auto p = parseProject (fixture ("full.project.json"));
    const auto sourceId = p.tracks[1].id;
    p.tracks[1].volumeDb = -4;
    p.tracks[1].pan = 0.25;

    Render r { std::string (64, 'a'), "2026-01-01T00:00:00Z", std::string (64, '1'), 2.0 };
    const auto id = applyBounce (p, sourceId, r, 48000 * 10, "00000000-0000-4000-8000-0000000000b1", "00000000-0000-4000-8000-0000000000c1");

    REQUIRE (p.indexOfTrack (id) == p.indexOfTrack (sourceId) + 1);
    auto* bounced = p.findTrack (id);
    CHECK (bounced->type == TrackType::audio);
    CHECK (bounced->name == p.findTrack (sourceId)->name + "（バウンス）");
    CHECK (bounced->volumeDb == -4);
    CHECK (bounced->pan == 0.25);
    REQUIRE (bounced->audioClips.size() == 1);
    CHECK (bounced->audioClips[0].audioHash == r.audioHash);
    CHECK (bounced->audioClips[0].startTick == 0);
    CHECK (bounced->audioClips[0].lengthSamples == 480000);
    CHECK_FALSE (p.findTrack (sourceId)->mute);   // 外部プラグインのトラックは、持ち主はそのまま鳴らす
    CHECK (p.findTrack (sourceId)->render == r);
    CHECK (findBounceTrack (p, *p.findTrack (sourceId)) == bounced);
    CHECK (findBounceSource (p, *bounced) == p.findTrack (sourceId));
    CHECK (isHiddenBounceTrack (p, *bounced));
    CHECK_FALSE (isHiddenBounceTrack (p, p.tracks[0]));

    // 隠したトラックの音量などは元のトラックに合わせる（持ち主は元のトラックでミックスする）
    p.findTrack (sourceId)->volumeDb = -10;
    CHECK (mirrorBounceMixers (p));
    CHECK (p.findTrack (id)->volumeDb == -10);
    CHECK_FALSE (mirrorBounceMixers (p));

    // バウンスし直し: 同じトラックのクリップを差し替える
    const auto count = p.tracks.size();
    Render r2 { std::string (64, 'b'), "2026-01-02T00:00:00Z", std::string (64, '2'), 2.0 };
    CHECK (applyBounce (p, sourceId, r2, 48000, "00000000-0000-4000-8000-0000000000b2", "00000000-0000-4000-8000-0000000000c2") == id);
    CHECK (p.tracks.size() == count);
    CHECK (p.findTrack (id)->volumeDb == -10);
    CHECK (p.findTrack (id)->audioClips.size() == 1);
    CHECK (p.findTrack (id)->audioClips[0].audioHash == r2.audioHash);

    // 持ち主でない PC（プラグインの状態がない）では隠さない
    setOwnedPluginTrackCheck ({});
    CHECK_FALSE (isHiddenBounceTrack (p, *p.findTrack (id)));

    // 内蔵音源のトラックをバウンスすると、元のトラックはミュートする（二重に鳴らないように）
    const auto builtinId = p.tracks[0].id;
    const auto b2 = applyBounce (p, builtinId, Render { std::string (64, 'c'), "2026-01-02T00:00:00Z", std::string (64, '3'), 2.0 },
                                 48000, "00000000-0000-4000-8000-0000000000b3", "00000000-0000-4000-8000-0000000000c3");
    CHECK (p.findTrack (builtinId)->mute);
    CHECK_FALSE (isHiddenBounceTrack (p, *p.findTrack (b2)));

    CHECK (parseProject (serialiseProject (p)) == p);
}

TEST_CASE ("external plugin tracks stay on this PC: the bounced audio track is uploaded instead")
{
    setOwnedPluginTrackCheck ([] (const Track&) { return true; });
    auto base = parseProject (fixture ("full.project.json"));
    base.tracks.erase (base.tracks.begin() + 1);   // 外部プラグインのトラックはまだサーバーにない

    auto local = parseProject (fixture ("full.project.json"));
    const auto externalId = local.tracks[1].id;
    Render r { std::string (64, 'a'), "2026-01-01T00:00:00Z", std::string (64, '1'), 2.0 };
    const auto bounceId = applyBounce (local, externalId, r, 48000, "00000000-0000-4000-8000-0000000000b1", "00000000-0000-4000-8000-0000000000c1");

    CHECK (isLocalOnlyTrack (base, local, externalId));
    CHECK_FALSE (isLocalOnlyTrack (base, local, bounceId));
    CHECK_FALSE (isLocalOnlyTrack (base, local, local.tracks[0].id));

    for (auto& st : syncStates (base, local, nullptr))
    {
        if (st.id == externalId)  { CHECK (st.localOnly); CHECK_FALSE (st.mine); }
        if (st.id == bounceId)    { CHECK_FALSE (st.localOnly); CHECK (st.mine); }
    }

    // 選んでも外部プラグインのトラックはアップしない
    const auto up = uploadSnapshot (base, local, { externalId, bounceId });
    CHECK (up.findTrack (externalId) == nullptr);
    CHECK (up.findTrack (bounceId) != nullptr);

    // ダウンロードしても、この PC だけのトラックは残る
    auto head = up;
    head.tracks[0].volumeDb = -12;
    auto merged = resolvePull (up, local, head, {});
    REQUIRE (merged.findTrack (externalId) != nullptr);
    CHECK (*merged.findTrack (externalId) == *local.findTrack (externalId));
    CHECK (merged.tracks[0].volumeDb == -12);

    // 以前の版でアップ済み（ベースにある）外部プラグインのトラック: 持ち主の PC ではこの PC だけにして、アップでサーバーから消す
    CHECK (isLocalOnlyTrack (local, local, externalId));
    CHECK (uploadSnapshot (local, local, { bounceId }).findTrack (externalId) == nullptr);

    // 持ち主でない PC にある古いトラックはふつうに扱う（持ち主が消したら、ダウンロードで消える）
    setOwnedPluginTrackCheck ({});
    CHECK_FALSE (isLocalOnlyTrack (local, local, externalId));
    const auto removedOnServer = uploadSnapshot (local, local, { bounceId });
    CHECK (resolvePull (local, local, removedOnServer, {}).findTrack (externalId) != nullptr);   // サーバーで消えていない
    auto headWithout = local;
    std::erase_if (headWithout.tracks, [&] (const Track& t) { return t.id == externalId; });
    CHECK (resolvePull (local, local, headWithout, {}).findTrack (externalId) == nullptr);
    setOwnedPluginTrackCheck ({});
}

TEST_CASE ("registering a new song leaves external plugin tracks on this PC")
{
    const auto local = parseProject (fixture ("full.project.json"));
    const auto registered = withoutLocalOnlyTracks (local);
    CHECK (registered.tracks.size() == local.tracks.size() - 1);
    CHECK (registered.findTrack (local.tracks[1].id) == nullptr);
    CHECK (isLocalOnlyTrack (registered, local, local.tracks[1].id));
}
