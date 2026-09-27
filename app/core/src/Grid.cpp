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
    const double step = std::max (1.0, stepExact());
    const Tick snapped = barStart + offsetOf (std::llround (double (tick - barStart) / step));

    // 次の小節の頭を越える場合は、次の小節の頭に揃える
    const Tick nextBar = map.barToTick (map.tickToBar (tick) + 1);
    return std::max<Tick> (0, std::min (snapped, nextBar));
}

Tick Grid::snapFloor (Tick tick, const TempoMap& map) const
{
    if (! enabled)
        return tick;

    const Tick barStart = map.barToTick (map.tickToBar (tick));
    const double step = std::max (1.0, stepExact());
    auto k = (long long) std::floor (double (tick - barStart) / step);

    // 端数の丸めでずれた分を合わせる（丸めた位置がちょうど tick のときはそこを選ぶ）
    while (k > 0 && barStart + offsetOf (k) > tick)
        --k;

    while (barStart + offsetOf (k + 1) <= tick)
        ++k;

    return std::max<Tick> (0, barStart + offsetOf (k));
}

std::string Grid::label() const
{
    // Cubase と同じ表記（例: 1/16、1/4 3連符）
    auto s = "1/" + std::to_string (division);

    if (tuplet > 1)
        s += " " + std::to_string (tuplet) + "連符";

    return s;
}

std::vector<Grid> Grid::presets()
{
    // Cubase のクオンタイズ値の一覧と同じ並び
    std::vector<Grid> g;

    for (int d : { 1, 2, 4, 8, 16, 32, 64, 128 })
        g.push_back ({ d, 1, true });

    for (int d : { 2, 4, 8, 16, 32 })
        g.push_back ({ d, 3, true });

    for (int t : { 5, 7 })
        for (int d : { 4, 8, 16 })
            g.push_back ({ d, t, true });

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
