#include <doctest/doctest.h>

#include "collab/ProjectDiff.h"
#include "collab/ProjectJson.h"
#include "collab/Uuid.h"
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

TEST_CASE ("sync states: mine, theirs and conflicts per scope")
{
    auto base = full();
    auto local = base;
    local.findTrack (drums)->volumeDb = -6.0;             // 自分だけ
    local.findTrack (gt)->volumeDb = -12.0;               // 両方（違う内容）→ 競合
    local.tempoTrack.events[0].bpm = 99.0;                // 両方（同じ内容）→ 競合ではない
    Track mine;
    mine.id = "local-new";
    mine.name = "New";
    local.tracks.insert (local.tracks.begin() + 1, mine);

    auto head = base;
    head.findTrack (gt)->volumeDb = -3.0;
    head.findTrack (lead)->name = "Lead (head)";          // サーバーだけ
    head.tempoTrack.events[0].bpm = 99.0;

    auto states = syncStates (base, local, &head);
    auto find = [&] (const std::string& id) { return *std::find_if (states.begin(), states.end(), [&] (auto& s) { return s.id == id; }); };

    CHECK (find (drums).mine);
    CHECK_FALSE (find (drums).theirs);
    CHECK (find (gt).conflict);
    CHECK (find (lead).theirs);
    CHECK_FALSE (find (lead).mine);
    CHECK (find ("local-new").mine);
    CHECK_FALSE (find ("local-new").inHead);
    CHECK (find (base.tempoTrack.id).mine);
    CHECK (find (base.tempoTrack.id).theirs);
    CHECK_FALSE (find (base.tempoTrack.id).conflict);
    CHECK (find (base.tempoTrack.id).name == "テンポ");
    CHECK (find (gt).name == "Gt");
}

TEST_CASE ("resolve pull: defaults and per-scope choices")
{
    auto base = full();
    auto local = base;
    local.findTrack (drums)->volumeDb = -6.0;
    local.findTrack (gt)->volumeDb = -12.0;
    local.chordTrack.events.clear();
    // 新しいトラック（スキーマに合うように、既存のトラックを ID を変えて複製する）
    Track mine = *base.findTrack (lead);
    const auto newId = generateUuid();
    mine.id = newId;
    mine.name = "New";
    for (auto& c : mine.midiClips)
    {
        c.id = generateUuid();
        for (auto& n : c.notes)
            n.id = generateUuid();
    }
    local.tracks.insert (local.tracks.begin() + 1, mine);

    auto head = base;
    head.findTrack (gt)->volumeDb = -3.0;
    head.findTrack (lead)->name = "Lead (head)";
    REQUIRE_FALSE (head.chordTrack.events.empty());
    head.chordTrack.events[0].tick += 960;

    SUBCASE ("no choices: my own changes stay, everything else follows the server")
    {
        auto m = resolvePull (base, local, head, {});
        CHECK (m.findTrack (drums)->volumeDb == -6.0);
        CHECK (m.findTrack (gt)->volumeDb == -3.0);           // 競合は選ばなければサーバー
        CHECK (m.findTrack (lead)->name == "Lead (head)");
        REQUIRE (m.indexOfTrack (newId) == 1);
        CHECK (m.chordTrack == head.chordTrack);
    }

    SUBCASE ("choose mine for a conflict, and keep both for another")
    {
        auto m = resolvePull (base, local, head, { { gt, Resolution::both }, { base.chordTrack.id, Resolution::mine } });
        CHECK (m.findTrack (gt)->volumeDb == -3.0);
        const int at = m.indexOfTrack (gt);
        REQUIRE (at + 1 < (int) m.tracks.size());
        const auto& copy = m.tracks[(size_t) at + 1];
        CHECK (copy.name == "Gt（自分の版）");
        CHECK (copy.volumeDb == -12.0);
        CHECK (copy.audioClips[0].id != local.findTrack (gt)->audioClips[0].id);
        CHECK (m.chordTrack.events.empty());
        CHECK (validateProjectJson (nlohmann::json::parse (serialiseProject (m))) == "");
    }

    SUBCASE ("reject the server's change to a track I did not touch")
    {
        auto m = resolvePull (base, local, head, { { lead, Resolution::mine } });
        CHECK (m.findTrack (lead)->name == base.findTrack (lead)->name);
    }
}

TEST_CASE ("replace scopes: add, change and delete tracks")
{
    auto base = full();
    auto local = base;
    local.tracks.erase (local.tracks.begin());           // Drums を削除
    local.findTrack (gt)->volumeDb = -9.0;

    auto up = replaceScopes (base, local, { drums });
    CHECK (up.findTrack (drums) == nullptr);
    CHECK (up.findTrack (gt)->volumeDb == base.findTrack (gt)->volumeDb);   // 選んでいないものはそのまま

    auto up2 = replaceScopes (base, local, { gt });
    CHECK (up2.findTrack (drums) != nullptr);
    CHECK (up2.findTrack (gt)->volumeDb == -9.0);
}

TEST_CASE ("marker track: stable id, JSON round trip, diff and merge")
{
    auto a = full();
    CHECK (isValidUuid (a.markerTrack.id));
    CHECK (a.markerTrack.id == markerTrackIdFor (a.projectId));   // 古いプロジェクトでも全員で同じ ID
    CHECK (a.markerTrack.events.empty());

    auto b = a;
    b.markerTrack.events.push_back ({ generateUuid(), 3840, "サビ" });
    const auto text = serialiseProject (b);
    CHECK (parseProject (text) == b);

    const auto d = diffProjects (a, b);
    REQUIRE (d.changes.size() == 1);
    CHECK (d.changes[0].scopeKind == ScopeKind::marker);
    CHECK (d.changes[0].summary.find ("サビ") != std::string::npos);

    // 自分だけが変えたマーカーは取り込んでも残る
    auto merged = resolvePull (a, b, a, {});
    CHECK (merged.markerTrack == b.markerTrack);
}
