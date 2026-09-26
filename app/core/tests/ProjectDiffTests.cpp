#include <doctest/doctest.h>

#include "collab/ProjectDiff.h"
#include "collab/ProjectJson.h"
#include "TestUtils.h"

using namespace collab;

namespace
{
    Project full()  { return parseProject (fixture ("full.project.json")); }

    const std::string drums = "22222222-2222-4222-8222-222222222201";
    const std::string lead  = "22222222-2222-4222-8222-222222222205";
    const std::string gt    = "22222222-2222-4222-8222-222222222207";

    bool hasSummary (const ProjectDiff& d, const std::string& text)
    {
        for (auto& c : d.changes)
            if (c.summary.find (text) != std::string::npos)
                return true;

        return false;
    }

    std::string all (const ProjectDiff& d)
    {
        std::string s;
        for (auto& c : d.changes)
            s += c.scopeName + ": " + c.summary + "\n";
        return s;
    }
}

TEST_CASE ("identical projects have no diff")
{
    auto a = full();
    auto d = diffProjects (a, a);
    CHECK (d.empty());
    CHECK (d.changedScopeIds.empty());
}

TEST_CASE ("note changes are aggregated with a bar range")
{
    auto a = full();
    auto b = a;
    auto& clip = b.findTrack (drums)->midiClips[0];
    clip.notes[0].velocity = 50;                                             // 変更（1小節目）
    clip.notes.push_back ({ "n-new-1", 3840 * 2, 240, 42, 90 });            // 追加（3小節目）
    clip.notes.push_back ({ "n-new-2", 3840 * 3 + 10, 240, 42, 90 });       // 追加（4小節目）

    auto d = diffProjects (a, b);
    CAPTURE (all (d));
    REQUIRE (d.changes.size() == 1);
    CHECK (d.changes[0].scopeId == drums);
    CHECK (d.changes[0].scopeName == "Drums");
    CHECK (d.changes[0].category == Change::Category::notes);
    CHECK (d.changes[0].summary == "1〜4小節目 ノート変更 3件（追加2・変更1）");
    CHECK (d.changes[0].fromTick == 0);
    CHECK (d.changedScopeIds == std::vector<std::string> { drums });
}

TEST_CASE ("drum sample swap is described in Japanese")
{
    auto a = full();
    auto b = a;
    b.findTrack (drums)->instrument->params["pieces"]["snare"]["sample"] = "synth/snare_01";
    b.findTrack (drums)->instrument->params["pieces"]["kick"]["volumeDb"] = -3.0;

    auto d = diffProjects (a, b);
    CAPTURE (all (d));
    CHECK (hasSummary (d, "スネアのサンプルを差し替え（synth/snare_02 → synth/snare_01）"));
    CHECK (hasSummary (d, "キックの音量を変更（なし → -3.0）"));
}

TEST_CASE ("track, audio clip, render and structure changes")
{
    auto a = full();
    auto b = a;
    b.findTrack (gt)->name = "Guitar";
    b.findTrack (gt)->mute = false;
    b.findTrack (gt)->audioClips[0].startTick += 960;
    b.findTrack (gt)->audioClips[0].fadeInSamples = 100;
    b.findTrack (lead)->render->audioHash = std::string (64, 'd');
    b.tracks.erase (b.tracks.begin());                    // Drums を削除

    Track nt;
    nt.id = "new-track";
    nt.name = "Pad";
    nt.midiClips.push_back ({ "c", 0, 3840, {} });
    b.tracks.push_back (nt);

    auto d = diffProjects (a, b);
    CAPTURE (all (d));
    CHECK (hasSummary (d, "名前を変更（Gt → Guitar）"));
    CHECK (hasSummary (d, "ミュート解除"));
    CHECK (hasSummary (d, "オーディオ「Gt_take3」: 移動・フェード"));
    CHECK (hasSummary (d, "バウンスを更新"));
    CHECK (hasSummary (d, "トラックを削除"));
    CHECK (hasSummary (d, "トラックを追加"));
    CHECK (d.touches (drums));
    CHECK (d.touches ("new-track"));
    CHECK (d.touches (gt));
    CHECK (d.touches (lead));
    CHECK_FALSE (d.touches (a.tempoTrack.id));
}

TEST_CASE ("tempo, meter and chord changes")
{
    auto a = full();
    auto b = a;
    b.tempoTrack.events[1].bpm = 100.0;
    b.meterTrack.events.push_back ({ "m-new", 9, 7, 8 });
    b.chordTrack.events[0].chord->quality = "m7";
    b.chordTrack.playback.enabled = false;

    auto d = diffProjects (a, b);
    CAPTURE (all (d));
    CHECK (hasSummary (d, "5小節目 テンポを変更（90.0 → 100.0）"));
    CHECK (hasSummary (d, "9小節目 拍子 7/8 を追加"));
    CHECK (hasSummary (d, "1小節目 コード変更 1件（CM7(9) → Cm7(9)）"));
    CHECK (hasSummary (d, "コードの発音をオフ"));
    CHECK (d.touches (a.tempoTrack.id));
    CHECK (d.touches (a.meterTrack.id));
    CHECK (d.touches (a.chordTrack.id));
}

TEST_CASE ("pull keeps locally modified locked scopes and adopts head elsewhere")
{
    auto base = full();

    auto local = base;
    local.findTrack (drums)->volumeDb = -6.0;            // 自分がロック中
    local.chordTrack.events.clear();                     // 自分がロック中
    Track mine;
    mine.id = "local-new";
    mine.name = "New";
    local.tracks.insert (local.tracks.begin() + 1, mine); // 新規トラック（Drums の後ろ）

    auto head = base;
    head.findTrack (gt)->name = "Guitar (head)";         // 他人の変更
    head.findTrack (drums)->pan = 0.5;                    // 強制解除されて他人が変更した（ローカルを優先）
    head.tempoTrack.events[0].bpm = 100.0;

    auto r = mergeForPull (base, local, head, { drums, base.chordTrack.id });
    auto& m = r.merged;

    CHECK (m.findTrack (drums)->volumeDb == -6.0);
    CHECK (m.findTrack (drums)->pan == 0.0);
    CHECK (m.chordTrack.events.empty());
    CHECK (m.findTrack (gt)->name == "Guitar (head)");
    CHECK (m.tempoTrack.events[0].bpm == 100.0);
    REQUIRE (m.indexOfTrack ("local-new") == 1);
    CHECK (r.conflictScopes.empty());
    CHECK (r.conflictCopyTrackIds.empty());
}

TEST_CASE ("pull: unlocked local changes become conflict copies")
{
    auto base = full();
    auto local = base;
    local.findTrack (gt)->volumeDb = -12.0;              // ロックなし（本来起きない）
    auto head = base;
    head.findTrack (gt)->volumeDb = -3.0;

    auto r = mergeForPull (base, local, head, {});
    CHECK (r.merged.findTrack (gt)->volumeDb == -3.0);
    REQUIRE (r.conflictCopyTrackIds.size() == 1);
    auto* copy = r.merged.findTrack (r.conflictCopyTrackIds[0]);
    REQUIRE (copy != nullptr);
    CHECK (copy->name == "Gt（競合コピー）");
    CHECK (copy->volumeDb == -12.0);
    CHECK (copy->audioClips[0].id != local.findTrack (gt)->audioClips[0].id);
    CHECK (r.conflictScopes == std::vector<std::string> { gt });

    // 結果はスキーマに適合する
    CHECK (validateProjectJson (nlohmann::json::parse (serialiseProject (r.merged))).empty());
}

TEST_CASE ("pull: locked local deletion stays deleted, unlocked deletion is restored")
{
    auto base = full();
    auto local = base;
    local.tracks.erase (local.tracks.begin(), local.tracks.begin() + 2);   // Drums と Synth Lead を削除
    auto head = base;

    auto r = mergeForPull (base, local, head, { drums });
    CHECK (r.merged.findTrack (drums) == nullptr);
    CHECK (r.merged.findTrack (lead) != nullptr);
    CHECK (r.conflictScopes == std::vector<std::string> { lead });
}
