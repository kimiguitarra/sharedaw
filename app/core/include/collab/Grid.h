#pragma once

#include <string>
#include <vector>

#include "TempoMap.h"

namespace collab
{

/** スナップ・クオンタイズのグリッド（1/1〜1/32、3連符）。 */
struct Grid
{
    int division = 16;        // 1, 2, 4, 8, 16, 32（n 分音符）
    bool triplet = false;
    bool enabled = true;

    Tick stepTicks() const noexcept
    {
        const Tick base = (Tick) kPpq * 4 / division;
        return triplet ? base * 2 / 3 : base;
    }

    /** 小節の頭を基準に最も近いグリッドへ丸める。 */
    Tick snap (Tick, const TempoMap&) const;

    /** 小節の頭を基準に、直前のグリッドへ切り捨てる。 */
    Tick snapFloor (Tick, const TempoMap&) const;

    std::string label() const;

    static std::vector<Grid> presets();
};

/** クリップ内ノートの開始位置をグリッドに合わせる。strength は 0〜1。 */
void quantiseNotes (MidiClip&, const std::vector<std::string>& noteIds, const Grid&, const TempoMap&, double strength = 1.0);

} // namespace collab
