#include "collab/TempoMap.h"

#include <algorithm>
#include <cmath>

namespace collab
{

TempoMap::TempoMap()
{
    build ({ { {}, 0, 120.0 } }, { { {}, 1, 4, 4 } });
}

TempoMap::TempoMap (const TempoTrack& tt, const MeterTrack& mt)
{
    build (tt.events, mt.events);
}

void TempoMap::build (std::vector<TempoEvent> tempoEvents, std::vector<MeterEvent> meterEvents)
{
    std::stable_sort (tempoEvents.begin(), tempoEvents.end(), [] (auto& a, auto& b) { return a.tick < b.tick; });
    std::stable_sort (meterEvents.begin(), meterEvents.end(), [] (auto& a, auto& b) { return a.bar < b.bar; });

    tempos.clear();
    double seconds = 0.0;

    if (tempoEvents.empty() || tempoEvents.front().tick > 0)
        tempoEvents.insert (tempoEvents.begin(), { {}, 0, tempoEvents.empty() ? 120.0 : tempoEvents.front().bpm });

    for (auto& e : tempoEvents)
    {
        const double bpm = std::clamp (e.bpm, 1.0, 10000.0);
        const Tick tick = std::max<Tick> (0, e.tick);

        if (! tempos.empty())
        {
            auto& prev = tempos.back();

            if (tick == prev.tick)
            {
                prev.bpm = bpm;   // 同じ位置の重複は後勝ち
                continue;
            }

            seconds = prev.startSeconds + double (tick - prev.tick) / kPpq * 60.0 / prev.bpm;
        }

        tempos.push_back ({ tick, bpm, seconds });
    }

    meters.clear();

    if (meterEvents.empty() || meterEvents.front().bar > 1)
        meterEvents.insert (meterEvents.begin(), { {}, 1, 4, 4 });

    for (auto& e : meterEvents)
    {
        TimeSignature sig { std::max (1, e.numerator), std::max (1, e.denominator) };
        const int bar = std::max (1, e.bar);

        if (! meters.empty())
        {
            auto& prev = meters.back();

            if (bar == prev.bar)
            {
                prev.sig = sig;
                continue;
            }

            const Tick start = prev.startTick + Tick (bar - prev.bar) * prev.sig.ticksPerBar();
            meters.push_back ({ bar, start, sig });
        }
        else
        {
            meters.push_back ({ 1, 0, sig });
        }
    }
}

double TempoMap::tickToSeconds (double tick) const
{
    if (tick <= 0)
        return tick / kPpq * 60.0 / tempos.front().bpm;

    auto it = std::upper_bound (tempos.begin(), tempos.end(), tick,
                                [] (double t, const TempoSeg& s) { return t < (double) s.tick; });
    auto& seg = *std::prev (it);
    return seg.startSeconds + (tick - (double) seg.tick) / kPpq * 60.0 / seg.bpm;
}

double TempoMap::secondsToTick (double seconds) const
{
    if (seconds <= 0)
        return seconds * tempos.front().bpm / 60.0 * kPpq;

    auto it = std::upper_bound (tempos.begin(), tempos.end(), seconds,
                                [] (double s, const TempoSeg& seg) { return s < seg.startSeconds; });
    auto& seg = *std::prev (it);
    return (double) seg.tick + (seconds - seg.startSeconds) * seg.bpm / 60.0 * kPpq;
}

SampleCount TempoMap::tickToSamples (double tick, double sampleRate) const
{
    return (SampleCount) std::llround (tickToSeconds (tick) * sampleRate);
}

double TempoMap::bpmAtTick (Tick tick) const
{
    auto it = std::upper_bound (tempos.begin(), tempos.end(), tick,
                                [] (Tick t, const TempoSeg& s) { return t < s.tick; });
    return it == tempos.begin() ? tempos.front().bpm : std::prev (it)->bpm;
}

Tick TempoMap::barToTick (int bar) const
{
    bar = std::max (1, bar);
    auto it = std::upper_bound (meters.begin(), meters.end(), bar,
                                [] (int b, const MeterSeg& m) { return b < m.bar; });
    auto& m = *std::prev (it);
    return m.startTick + Tick (bar - m.bar) * m.sig.ticksPerBar();
}

int TempoMap::tickToBar (Tick tick) const
{
    if (tick < 0)
        return 1;

    auto it = std::upper_bound (meters.begin(), meters.end(), tick,
                                [] (Tick t, const MeterSeg& m) { return t < m.startTick; });
    auto& m = *std::prev (it);
    return m.bar + int ((tick - m.startTick) / m.sig.ticksPerBar());
}

BarBeat TempoMap::tickToBarBeat (Tick tick) const
{
    tick = std::max<Tick> (0, tick);
    const int bar = tickToBar (tick);
    const auto sig = timeSignatureAtBar (bar);
    const Tick inBar = tick - barToTick (bar);
    return { bar, int (inBar / sig.ticksPerBeat()) + 1, inBar % sig.ticksPerBeat() };
}

TimeSignature TempoMap::timeSignatureAtBar (int bar) const
{
    auto it = std::upper_bound (meters.begin(), meters.end(), bar,
                                [] (int b, const MeterSeg& m) { return b < m.bar; });
    return it == meters.begin() ? meters.front().sig : std::prev (it)->sig;
}

TimeSignature TempoMap::timeSignatureAtTick (Tick tick) const
{
    return timeSignatureAtBar (tickToBar (tick));
}

Tick contentEndTick (const Project& p, const TempoMap& map)
{
    Tick end = 0;

    for (auto& t : p.tracks)
    {
        for (auto& c : t.midiClips)
            end = std::max (end, c.endTick());

        for (auto& c : t.audioClips)
        {
            const double startSec = map.tickToSeconds ((double) c.startTick);
            const double speed = c.sourceBpm > 0.0 ? std::clamp (map.bpmAtTick (c.startTick) / c.sourceBpm, 0.25, 4.0) : 1.0;
            const double endSec = startSec + (double) c.lengthSamples / kSampleRate / speed;   // audioClipSeconds と同じ
            end = std::max (end, (Tick) std::ceil (map.secondsToTick (endSec)));
        }
    }

    for (auto& e : p.chordTrack.events)
        end = std::max (end, e.tick);

    return end;
}

} // namespace collab
