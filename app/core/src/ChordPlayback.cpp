#include "collab/ChordPlayback.h"

#include <algorithm>

namespace collab
{

chord::Chord toChord (const ChordSymbol& s)
{
    return { s.root, s.quality, s.tensions, s.bass };
}

ChordSymbol toChordSymbol (const chord::Chord& c)
{
    return { c.root, c.quality, c.tensions, c.bass };
}

Tick chordTrackEndTick (const Project& p, const TempoMap& map)
{
    Tick last = 0;

    for (auto& e : p.chordTrack.events)
        last = std::max (last, e.tick);

    return std::max (contentEndTick (p, map), map.barToTick (map.tickToBar (last) + 1));
}

std::vector<GeneratedNote> renderChordTrack (const Project& p, const TempoMap& map, int velocity)
{
    std::vector<GeneratedNote> notes;
    auto events = p.chordTrack.events;

    if (events.empty())
        return notes;

    std::stable_sort (events.begin(), events.end(), [] (auto& a, auto& b) { return a.tick < b.tick; });

    std::vector<std::optional<chord::Chord>> chords;

    for (auto& e : events)
        chords.push_back (! e.noChord && e.chord ? std::optional (toChord (*e.chord)) : std::nullopt);

    const auto voicings = chord::voiceProgression (chords);
    const Tick end = chordTrackEndTick (p, map);
    constexpr Tick gap = 10;   // 弾き直しがはっきり聞こえるよう、少しだけ離す

    for (size_t i = 0; i < events.size(); ++i)
    {
        if (! voicings[i])
            continue;

        const Tick start = events[i].tick;
        const Tick stop = i + 1 < events.size() ? events[i + 1].tick : end;

        // 小節の頭ごとに区切る
        for (Tick segStart = start; segStart < stop;)
        {
            const Tick nextBar = map.barToTick (map.tickToBar (segStart) + 1);
            const Tick segEnd = std::min (stop, nextBar);
            const Tick length = std::max<Tick> (1, segEnd - segStart - (segEnd < end ? gap : 0));

            notes.push_back ({ segStart, length, voicings[i]->bass, velocity });

            for (int n : voicings[i]->upper)
                notes.push_back ({ segStart, length, n, velocity });

            segStart = segEnd;
        }
    }

    return notes;
}

} // namespace collab
