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
    constexpr Tick gap = 10;   // 次のコードとの切れ目が聞こえるよう、少しだけ離す

    // 1 つのコードイベントは 1 回だけ鳴らす（Cubase のコードトラックと同じ）。
    // 長さは次のコードイベントまで、ただし最長 1 小節（最後のコードが曲の終わりまで鳴り続けないように）
    for (size_t i = 0; i < events.size(); ++i)
    {
        if (! voicings[i])
            continue;

        const Tick start = events[i].tick;
        const Tick oneBar = (Tick) map.timeSignatureAtBar (map.tickToBar (start)).ticksPerBar();
        Tick stop = start + oneBar;

        if (i + 1 < events.size())
            stop = std::min (stop, events[i + 1].tick - gap);

        const Tick length = std::max<Tick> (1, stop - start);
        notes.push_back ({ start, length, voicings[i]->bass, velocity });

        for (int n : voicings[i]->upper)
            notes.push_back ({ start, length, n, velocity });
    }

    return notes;
}

std::optional<chord::Key> keyAt (const Project& p, const TempoMap& map, Tick tick)
{
    const KeyEvent* found = nullptr;

    for (auto& e : p.keyTrack.events)
        if (map.barToTick (e.bar) <= tick && (found == nullptr || e.bar > found->bar))
            found = &e;

    // 最初のキーより前は、最初のキーとみなす（曲の頭にキーを置き忘れても表示できるように）
    if (found == nullptr)
        for (auto& e : p.keyTrack.events)
            if (found == nullptr || e.bar < found->bar)
                found = &e;

    if (found == nullptr)
        return std::nullopt;

    return chord::Key { found->tonic, found->minor };
}

} // namespace collab
