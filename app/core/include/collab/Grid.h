#pragma once

#include <string>
#include <vector>

#include "TempoMap.h"

namespace collab
{

/** スナップ・クオンタイズのグリッド（Cubase と同じく 1/1〜1/128、3連符・5連符・7連符）。 */
struct Grid
{
    int division = 16;        // 1, 2, 4, …, 128（n 分音符）
    int tuplet = 1;           // 1 = 通常、3 = 3連符（2 つ分に 3 つ）、5 / 7 = 5連符・7連符（4 つ分に 5 / 7 つ）
    bool enabled = true;

    /** 1 マスの長さ（tick、端数あり。7連符は割り切れない）。 */
    double stepExact() const noexcept
    {
        const double base = (double) kPpq * 4.0 / division;

        switch (tuplet)
        {
            case 3:  return base * 2.0 / 3.0;
            case 5:  return base * 4.0 / 5.0;
            case 7:  return base * 4.0 / 7.0;
            default: return base;
        }
    }

    /** 1 マスの長さ（tick に丸めたもの。ノートの長さなどに使う）。 */
    Tick stepTicks() const noexcept          { return (Tick) (stepExact() + 0.5); }

    /** 小節の頭から k マス目の位置（小節の頭からの tick）。 */
    Tick offsetOf (long long k) const noexcept { return (Tick) ((double) k * stepExact() + 0.5); }

    bool sameValue (const Grid& o) const noexcept { return division == o.division && (tuplet <= 1 ? 1 : tuplet) == (o.tuplet <= 1 ? 1 : o.tuplet); }

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
