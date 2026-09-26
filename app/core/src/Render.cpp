#include "collab/Render.h"

#include "collab/ProjectJson.h"
#include "collab/Sha256.h"

namespace collab
{

bool usesExternalPlugin (const Track& t)
{
    if (t.instrument && t.instrument->kind == Instrument::Kind::external)
        return true;

    for (auto& e : t.effects)
        if (! e.bypass)
            return true;

    return false;
}

std::string trackSourceFingerprint (const Track& source, const std::function<std::string (const std::string&)>& stateHash)
{
    // 影響しない項目をそろえてから、正規化した JSON のハッシュを取る
    Track t = source;
    t.name.clear();
    t.color.clear();
    t.volumeDb = 0;
    t.pan = 0;
    t.mute = false;
    t.solo = false;
    t.render.reset();

    Project p;
    p.projectId = "00000000-0000-4000-8000-000000000000";
    p.tempoTrack.id = p.meterTrack.id = p.chordTrack.id = p.projectId;
    p.tracks.push_back (t);

    auto json = projectToJson (p)["tracks"][0];

    auto stateOf = [&] (const std::string& ref) { return ref.empty() || ! stateHash ? std::string() : stateHash (ref); };

    if (t.instrument && t.instrument->kind == Instrument::Kind::external)
        json["instrument"]["stateHash"] = stateOf (t.instrument->stateRef);

    if (json.contains ("effects"))
        for (size_t i = 0; i < t.effects.size(); ++i)
            json["effects"][i]["stateHash"] = stateOf (t.effects[i].stateRef);

    return Sha256::hashHex (json.dump());
}

RenderStatus renderStatus (const Track& t, const std::string& currentFingerprint)
{
    if (! usesExternalPlugin (t))
        return RenderStatus::notNeeded;

    if (! t.render)
        return RenderStatus::missing;

    return t.render->sourceFingerprint == currentFingerprint ? RenderStatus::upToDate : RenderStatus::stale;
}

} // namespace collab
