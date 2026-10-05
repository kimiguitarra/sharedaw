#include "collab/Automation.h"

#include <algorithm>

namespace collab
{

AutomationRange automationRange (const std::string& param)
{
    if (param == "pan")
        return { -1.0, 1.0, 0.0 };

    return { -60.0, 6.0, 0.0 };   // volume
}

std::vector<std::string> automationParams()
{
    return { "volume", "pan" };
}

double automationValueAt (const std::vector<AutomationPoint>& points, Tick tick, double fallback)
{
    if (points.empty())
        return fallback;

    if (tick <= points.front().tick)
        return points.front().value;

    for (size_t i = 1; i < points.size(); ++i)
    {
        const auto& a = points[i - 1];
        const auto& b = points[i];

        if (tick <= b.tick)
        {
            if (b.tick == a.tick)
                return b.value;

            const double t = (double) (tick - a.tick) / (double) (b.tick - a.tick);
            return a.value + (b.value - a.value) * t;
        }
    }

    return points.back().value;
}

void replaceAutomation (std::vector<AutomationPoint>& points, Tick from, Tick to, const std::vector<AutomationPoint>& added,
                        const AutomationRange& range)
{
    if (to < from)
        std::swap (from, to);

    std::erase_if (points, [&] (const AutomationPoint& p) { return p.tick >= from && p.tick <= to; });

    for (auto p : added)
    {
        p.tick = std::max<Tick> (0, p.tick);
        p.value = std::clamp (p.value, range.min, range.max);
        std::erase_if (points, [&] (const AutomationPoint& q) { return q.tick == p.tick; });
        points.push_back (p);
    }

    std::stable_sort (points.begin(), points.end(), [] (auto& a, auto& b) { return a.tick < b.tick; });
}

void setAutomation (Track& t, const std::string& param, std::vector<AutomationPoint> points)
{
    std::stable_sort (points.begin(), points.end(), [] (auto& a, auto& b) { return a.tick < b.tick; });
    auto it = std::find_if (t.automation.begin(), t.automation.end(), [&] (auto& l) { return l.param == param; });

    if (points.empty())
    {
        if (it != t.automation.end())
            t.automation.erase (it);

        return;
    }

    if (it == t.automation.end())
    {
        t.automation.push_back ({ param, std::move (points) });

        // いつも同じ順に並べる（差分が順番で出ないように）
        const auto order = automationParams();
        std::stable_sort (t.automation.begin(), t.automation.end(), [&] (auto& a, auto& b)
        {
            return std::find (order.begin(), order.end(), a.param) < std::find (order.begin(), order.end(), b.param);
        });
        return;
    }

    it->points = std::move (points);
}

double mixerValue (const Track& t, const std::string& param)
{
    return param == "pan" ? t.pan : t.volumeDb;
}

} // namespace collab
