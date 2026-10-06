#pragma once

#include "ui/AppContext.h"

/**
    マスターのイメージャー（見るだけ。音は変えない）。Ozone Imager のように、
    左右の音の広がりを半円の点（Polar Sample: 真上がモノ、左右に倒れるほど広い・逆相）で、
    左右の似ている度合いを相関メーター（+1 = モノ、0 = 広い、-1 = 逆相）で見せる。
*/
class MasterImager  : public juce::Component
{
public:
    explicit MasterImager (AppContext&);

    /** 直近の音を読み直す（親のタイマーから 60 fps で呼ぶ）。 */
    void update();

    void paint (juce::Graphics&) override;

private:
    AppContext& ctx;
    static constexpr int windowSamples = 2048;
    std::vector<float> left, right;
    std::vector<juce::Point<float>> dots;   // 半円の中の位置（x: -1〜1、y: 0〜1）
    float correlation = 1.0f, width = 0.0f, balance = 0.0f;
    bool hasSignal = false;
};
