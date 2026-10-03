#include "collab/Stretch.h"

#include <algorithm>
#include <cmath>

namespace collab
{

Tick stretchTick (Tick t, double factor)
{
    return (Tick) std::llround ((double) t * factor);
}

MidiClip stretchMidiClip (const MidiClip& clip, double factor)
{
    auto c = clip;
    c.lengthTick = std::max<Tick> (1, stretchTick (clip.lengthTick, factor));

    for (auto& n : c.notes)
    {
        n.tick = stretchTick (n.tick, factor);
        n.lengthTick = std::max<Tick> (1, stretchTick (n.lengthTick, factor));
    }

    return c;
}

MidiClip stretchNotes (const MidiClip& clip, const std::set<std::string>& ids, double factor)
{
    auto c = clip;
    std::optional<Tick> origin;

    for (auto& n : c.notes)
        if (ids.count (n.id) > 0)
            origin = origin ? std::min (*origin, n.tick) : n.tick;

    if (! origin)
        return c;

    for (auto& n : c.notes)
    {
        if (ids.count (n.id) == 0)
            continue;

        n.tick = *origin + stretchTick (n.tick - *origin, factor);
        n.lengthTick = std::max<Tick> (1, stretchTick (n.lengthTick, factor));
        c.lengthTick = std::max (c.lengthTick, n.tick + n.lengthTick);
    }

    return c;
}

void stretchProject (Project& p, const ProjectStretch& s)
{
    const double f = s.factor;

    if (! (f > 0.0))
        return;

    // 小節で持っているもの（拍子・キー）の元の位置。拍子を変える前のテンポマップで求める
    const TempoMap oldMap (p);
    std::vector<Tick> meterTicks, keyTicks;

    for (auto& e : p.meterTrack.events)
        meterTicks.push_back (stretchTick (oldMap.barToTick (e.bar), f));

    for (auto& e : p.keyTrack.events)
        keyTicks.push_back (stretchTick (oldMap.barToTick (e.bar), f));

    for (auto& e : p.tempoTrack.events)
    {
        e.tick = stretchTick (e.tick, f);

        if (s.scaleTempo)
            e.bpm = std::clamp (e.bpm * f, 10.0, 999.0);
    }

    for (auto& e : p.chordTrack.events)
        e.tick = stretchTick (e.tick, f);

    for (auto& m : p.markerTrack.events)
        m.tick = stretchTick (m.tick, f);

    for (auto& t : p.tracks)
    {
        for (auto& c : t.midiClips)
        {
            const auto start = stretchTick (c.startTick, f);
            c = stretchMidiClip (c, f);
            c.startTick = start;
        }

        for (auto& c : t.audioClips)
            c.startTick = stretchTick (c.startTick, f);
    }

    // 拍子: 新しい位置（tick）から小節番号を数え直す（前の拍子の小節の長さで割る。割り切れなければその次の小節）
    {
        auto& events = p.meterTrack.events;
        std::vector<size_t> order (events.size());

        for (size_t i = 0; i < order.size(); ++i)
            order[i] = i;

        std::sort (order.begin(), order.end(), [&] (size_t a, size_t b) { return meterTicks[a] < meterTicks[b]; });

        int bar = 1;
        Tick at = 0;
        TimeSignature sig { 4, 4 };

        for (size_t k = 0; k < order.size(); ++k)
        {
            auto& e = events[order[k]];

            if (s.newMeter)
            {
                e.numerator = s.newMeter->numerator;
                e.denominator = s.newMeter->denominator;
            }

            if (k == 0)
            {
                e.bar = 1;
            }
            else
            {
                const auto perBar = std::max<Tick> (1, sig.ticksPerBar());
                const auto bars = (meterTicks[order[k]] - at + perBar - 1) / perBar;
                e.bar = bar + (int) std::max<Tick> (1, bars);
            }

            bar = e.bar;
            at = meterTicks[order[k]];
            sig = { e.numerator, e.denominator };
        }

        // 同じ小節に重なったら後のものだけ残す
        std::sort (events.begin(), events.end(), [] (auto& a, auto& b) { return a.bar < b.bar; });
        events.erase (std::unique (events.begin(), events.end(), [] (auto& a, auto& b) { return a.bar == b.bar; }), events.end());
    }

    // キー: 新しいテンポマップでの、いちばん近い小節の頭
    {
        const TempoMap newMap (p);

        for (size_t i = 0; i < p.keyTrack.events.size(); ++i)
        {
            const auto t = keyTicks[i];
            const int bar = newMap.tickToBar (t);
            const auto start = newMap.barToTick (bar), next = newMap.barToTick (bar + 1);
            p.keyTrack.events[i].bar = (t - start) * 2 >= (next - start) ? bar + 1 : bar;
        }
    }
}

} // namespace collab
