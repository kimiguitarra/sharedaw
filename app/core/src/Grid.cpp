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
    return "1/" + std::to_string (division) + (triplet ? " 3連" : "");
}

std::vector<Grid> Grid::presets()
{
    std::vector<Grid> g;

    for (int d : { 1, 2, 4, 8, 16, 32 })
        g.push_back ({ d, false, true });

    for (int d : { 2, 4, 8, 16, 32 })
        g.push_back ({ d, true, true });

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
