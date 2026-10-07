#include "collab/ProjectHistory.h"

#include <algorithm>

namespace collab
{

namespace
{
    bool hasTrack (const std::vector<Track>& tracks, const std::string& id)
    {
        return std::any_of (tracks.begin(), tracks.end(), [&] (const Track& t) { return t.id == id; });
    }
}

ProjectDelta makeDelta (Project&& before, const Project& after)
{
    ProjectDelta d;
    auto tracks = std::move (before.tracks);
    before.tracks.clear();
    d.shell = std::move (before);

    for (auto& t : tracks)
    {
        d.order.push_back (t.id);
        auto* now = after.findTrack (t.id);

        if (now == nullptr || ! (*now == t))
            d.tracks.push_back (std::move (t));
    }

    return d;
}

ProjectDelta makeDelta (const Project& before, const Project& after)
{
    // トラック以外だけを写し、トラックは変わったものだけを写す
    ProjectDelta d;
    d.shell.schemaVersion = before.schemaVersion;
    d.shell.projectId = before.projectId;
    d.shell.name = before.name;
    d.shell.sampleRate = before.sampleRate;
    d.shell.ppq = before.ppq;
    d.shell.tempoTrack = before.tempoTrack;
    d.shell.meterTrack = before.meterTrack;
    d.shell.chordTrack = before.chordTrack;
    d.shell.markerTrack = before.markerTrack;
    d.shell.keyTrack = before.keyTrack;
    d.shell.master = before.master;

    for (auto& t : before.tracks)
    {
        d.order.push_back (t.id);
        auto* now = after.findTrack (t.id);

        if (now == nullptr || ! (*now == t))
            d.tracks.push_back (t);
    }

    return d;
}

void extendDelta (ProjectDelta& d, const Project& before, const Project& after)
{
    for (auto& t : before.tracks)
    {
        // 最初からあったトラックで、まだ前の状態を持っていないものが変わった・消えた
        if (std::find (d.order.begin(), d.order.end(), t.id) == d.order.end() || hasTrack (d.tracks, t.id))
            continue;

        auto* now = after.findTrack (t.id);

        if (now == nullptr || ! (*now == t))
            d.tracks.push_back (t);
    }
}

Project applyDelta (const ProjectDelta& d, const Project& current)
{
    Project p = d.shell;
    p.tracks.reserve (d.order.size());

    for (auto& id : d.order)
    {
        auto stored = std::find_if (d.tracks.begin(), d.tracks.end(), [&] (const Track& t) { return t.id == id; });

        if (stored != d.tracks.end())
            p.tracks.push_back (*stored);
        else if (auto* t = current.findTrack (id))
            p.tracks.push_back (*t);
    }

    return p;
}

}
