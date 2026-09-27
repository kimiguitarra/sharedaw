#include "collab/Grid.h"

#include <algorithm>
#include <cmath>

namespace collab
{

Tick Grid::snap (Tick tick, const TempoMap& map) const
{
    if (! enabled)
        return tick;

    const Tick barStart = map.barToTick (map.tickToBar (tick));
    const Tick step = std::max<Tick> (1, stepTicks());
    const double n = std::round (double (tick - barStart) / (double) step);
    const Tick snapped = barStart + Tick (n) * step;

    // 次の小節の頭を越える場合は、次の小節の頭に揃える
    const Tick nextBar = map.barToTick (map.tickToBar (tick) + 1);
    return std::max<Tick> (0, std::min (snapped, nextBar));
}

Tick Grid::snapFloor (Tick tick, const TempoMap& map) const
{
    if (! enabled)
        return tick;

    const Tick barStart = map.barToTick (map.tickToBar (tick));
    const Tick step = std::max<Tick> (1, stepTicks());
    return std::max<Tick> (0, barStart + ((tick - barStart) / step) * step);
}

std::string Grid::label() const
{
    // 3連符は 1/3（2分3連）、1/6（4分3連）… のように Cubase と同じ「1 小節を何分割するか」で表す
    if (triplet)
        return "1/" + std::to_string (division * 3 / 2) + "（" + std::to_string (division) + "分3連）";

    return "1/" + std::to_string (division);
}

std::vector<Grid> Grid::presets()
{
    std::vector<Grid> g;

    // 細かさの順（1/1, 1/2, 1/3, 1/4, 1/6, 1/8, 1/12, …）
    for (int d : { 1, 2, 4, 8, 16, 32 })
    {
        g.push_back ({ d, false, true });

        if (d >= 2)
            g.push_back ({ d, true, true });
    }

    std::stable_sort (g.begin(), g.end(), [] (const Grid& a, const Grid& b) { return a.stepTicks() > b.stepTicks(); });

    return g;
}

void quantiseNotes (MidiClip& clip, const std::vector<std::string>& noteIds, const Grid& grid,
                    const TempoMap& map, double strength)
{
    strength = std::clamp (strength, 0.0, 1.0);
    Grid g = grid;
    g.enabled = true;

    for (auto& n : clip.notes)
    {
        if (! noteIds.empty() && std::find (noteIds.begin(), noteIds.end(), n.id) == noteIds.end())
            continue;

        const Tick absolute = clip.startTick + n.tick;
        const Tick target = g.snap (absolute, map);
        const Tick moved = absolute + (Tick) std::llround (double (target - absolute) * strength);
        n.tick = std::max<Tick> (0, moved - clip.startTick);
    }
}

} // namespace collab
