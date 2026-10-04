#include "collab/Render.h"

#include <algorithm>

#include "collab/ProjectJson.h"
#include "collab/Sha256.h"

namespace collab
{

bool usesExternalPlugin (const Track& t)
{
    if (t.instrument && t.instrument->kind == Instrument::Kind::external)
        return true;

    // 内蔵エフェクトは誰の PC でも同じに鳴るのでバウンスはいらない
    for (auto& e : t.effects)
        if (! e.bypass && ! e.isBuiltin())
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
    t.strip = {};   // EQ・コンプ・出力先・センドはバウンスに含めない
    t.output.clear();
    t.sends.clear();

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

namespace
{
    bool usesAudio (const Track& t, const std::string& hash)
    {
        return t.type == TrackType::audio && ! hash.empty()
            && std::any_of (t.audioClips.begin(), t.audioClips.end(), [&] (const AudioClip& c) { return c.audioHash == hash; });
    }
}

const Track* findBounceTrack (const Project& p, const Track& source)
{
    if (! source.render)
        return nullptr;

    for (auto& t : p.tracks)
        if (t.id != source.id && usesAudio (t, source.render->audioHash))
            return &t;

    return nullptr;
}

const Track* findBounceSource (const Project& p, const Track& audioTrack)
{
    for (auto& t : p.tracks)
        if (t.id != audioTrack.id && t.render && usesAudio (audioTrack, t.render->audioHash))
            return &t;

    return nullptr;
}

namespace
{
    std::function<bool (const Track&)>& ownedCheck()
    {
        static std::function<bool (const Track&)> check;
        return check;
    }

    void copyMixer (const Track& from, Track& to)
    {
        to.name = from.name + "（バウンス）";
        to.color = from.color;
        to.volumeDb = from.volumeDb;
        to.pan = from.pan;
        to.mute = from.mute;
        to.strip = from.strip;
        to.output = from.output;
        to.sends = from.sends;
        to.outputChannels = from.outputChannels;
    }
}

void setOwnedPluginTrackCheck (std::function<bool (const Track&)> check)
{
    ownedCheck() = std::move (check);
}

bool isOwnedPluginTrack (const Track& t)
{
    return usesExternalPlugin (t) && ownedCheck() && ownedCheck() (t);
}

bool isHiddenBounceTrack (const Project& p, const Track& t)
{
    if (t.type != TrackType::audio)
        return false;

    auto* source = findBounceSource (p, t);
    return source != nullptr && isOwnedPluginTrack (*source);
}

bool mirrorBounceMixers (Project& p)
{
    bool changed = false;

    for (auto& t : p.tracks)
    {
        if (! isHiddenBounceTrack (p, t))
            continue;

        auto copy = t;
        copyMixer (*findBounceSource (p, t), copy);

        if (! (copy == t))
        {
            t = copy;
            changed = true;
        }
    }

    return changed;
}

std::string applyBounce (Project& p, const std::string& sourceId, const Render& render, SampleCount lengthSamples,
                         const std::string& newTrackId, const std::string& newClipId)
{
    auto* source = p.findTrack (sourceId);

    if (source == nullptr)
        return {};

    std::string targetId;

    if (auto* existing = findBounceTrack (p, *source))
        targetId = existing->id;

    source->render = render;

    // 外部プラグインのトラックは、持ち主はそのまま鳴らす（バウンスしたトラックは隠して鳴らさない）
    if (! usesExternalPlugin (*source))
        source->mute = true;

    const Track original = *source;

    AudioClip clip;
    clip.id = newClipId;
    clip.startTick = 0;   // バウンスは曲の先頭から
    clip.audioHash = render.audioHash;
    clip.displayName = original.name;
    clip.lengthSamples = lengthSamples;

    if (auto* target = p.findTrack (targetId))
    {
        target->audioClips = { clip };
        mirrorBounceMixers (p);
        return targetId;
    }

    Track t;
    t.id = newTrackId;
    t.type = TrackType::audio;
    copyMixer (original, t);
    t.mute = false;
    t.audioClips = { clip };

    p.tracks.insert (p.tracks.begin() + p.indexOfTrack (sourceId) + 1, t);
    mirrorBounceMixers (p);
    return newTrackId;
}

} // namespace collab
