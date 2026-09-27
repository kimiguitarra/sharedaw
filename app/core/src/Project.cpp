#include "collab/Project.h"

#include <algorithm>
#include <tuple>

#include "collab/TempoMap.h"
#include "collab/Uuid.h"

namespace collab
{

const MidiClip* Track::findMidiClip (const std::string& clipId) const
{
    for (auto& c : midiClips)
        if (c.id == clipId)
            return &c;

    return nullptr;
}

MidiClip* Track::findMidiClip (const std::string& clipId)
{
    return const_cast<MidiClip*> (std::as_const (*this).findMidiClip (clipId));
}

const Track* Project::findTrack (const std::string& trackId) const
{
    for (auto& t : tracks)
        if (t.id == trackId)
            return &t;

    return nullptr;
}

Track* Project::findTrack (const std::string& trackId)
{
    return const_cast<Track*> (std::as_const (*this).findTrack (trackId));
}

int Project::indexOfTrack (const std::string& trackId) const
{
    for (size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i].id == trackId)
            return (int) i;

    return -1;
}

Tick Project::contentEndTick() const
{
    return collab::contentEndTick (*this, TempoMap (*this));
}

void Project::sortCanonical()
{
    std::stable_sort (tempoTrack.events.begin(), tempoTrack.events.end(),
                      [] (auto& a, auto& b) { return std::tie (a.tick, a.id) < std::tie (b.tick, b.id); });

    std::stable_sort (meterTrack.events.begin(), meterTrack.events.end(),
                      [] (auto& a, auto& b) { return std::tie (a.bar, a.id) < std::tie (b.bar, b.id); });

    std::stable_sort (chordTrack.events.begin(), chordTrack.events.end(),
                      [] (auto& a, auto& b) { return std::tie (a.tick, a.id) < std::tie (b.tick, b.id); });

    // トラックの並び順はユーザーが決める順序なので並べ替えない
    for (auto& t : tracks)
    {
        std::stable_sort (t.midiClips.begin(), t.midiClips.end(),
                          [] (auto& a, auto& b) { return std::tie (a.startTick, a.id) < std::tie (b.startTick, b.id); });

        for (auto& c : t.midiClips)
            std::stable_sort (c.notes.begin(), c.notes.end(),
                              [] (auto& a, auto& b) { return std::tie (a.tick, a.pitch, a.id) < std::tie (b.tick, b.pitch, b.id); });

        std::stable_sort (t.audioClips.begin(), t.audioClips.end(),
                          [] (auto& a, auto& b) { return std::tie (a.startTick, a.id) < std::tie (b.startTick, b.id); });
    }
}

Project Project::createEmpty (const std::string& name)
{
    Project p;
    p.projectId = generateUuid();
    p.name = name;
    p.tempoTrack.id = generateUuid();
    p.tempoTrack.events.push_back ({ generateUuid(), 0, 120.0 });
    p.meterTrack.id = generateUuid();
    p.meterTrack.events.push_back ({ generateUuid(), 1, 4, 4 });
    p.chordTrack.id = generateUuid();
    p.chordTrack.playback = { true, -6.0, { "builtin.piano", "1.0.0" } };
    return p;
}

std::string compTypeName (CompType t)
{
    return t == CompType::opto ? "opto" : "fet";
}

std::string trackTypeName (TrackType t)
{
    return t == TrackType::midi ? "midi" : "audio";
}

} // namespace collab
