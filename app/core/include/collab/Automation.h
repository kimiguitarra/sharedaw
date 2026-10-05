#pragma once

// トラックのオートメーション（音量・パン）。点の値の求め方と、描いたときの置き換え。

#include <string>
#include <vector>

#include "Project.h"

namespace collab
{

/** パラメーターの範囲（音量は dB、パンは -1〜1）と、点がないときの値。 */
struct AutomationRange
{
    double min = 0.0, max = 1.0, initial = 0.0;
};

AutomationRange automationRange (const std::string& param);

/** オートメーションにできるパラメーター（表示の順）。 */
std::vector<std::string> automationParams();

/** tick での値（点の間は直線。最初より前・最後より後はその点の値）。点がなければ fallback。 */
double automationValueAt (const std::vector<AutomationPoint>&, Tick, double fallback);

/** from〜to（両端を含む）の点を points で置き換える（範囲に収め、tick の順にそろえ、同じ tick は後のものを残す）。 */
void replaceAutomation (std::vector<AutomationPoint>&, Tick from, Tick to, const std::vector<AutomationPoint>& points,
                        const AutomationRange&);

/** トラックのレーンの点を変える（空にしたらレーンを消す）。 */
void setAutomation (Track&, const std::string& param, std::vector<AutomationPoint> points);

/** ミキサーの値（点がないとき）。 */
double mixerValue (const Track&, const std::string& param);

} // namespace collab
