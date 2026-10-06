#pragma once

#include "ui/AppContext.h"
#include "ui/MasterImager.h"
#include "ui/Theme.h"

/**
    マスターの画面: ヴィンテージ系リミッター（THRESHOLD・CEILING・CHARACTER・モード）、ラウドネスメーター（LUFS）とイメージャー（見るだけ）。
    目標は -14 LUFS（配信サービスの基準）。「-14 LUFS に合わせる」でインテグレーテッドの値から THRESHOLD を調整する。
    リミッターの設定はプロジェクト JSON（Project::master）に保存し、全員で共通。
*/
class MasterPanel  : public juce::Component,
                     private juce::ChangeListener,
                     private juce::Timer
{
public:
    explicit MasterPanel (AppContext&);
    ~MasterPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr double targetLufs = -14.0;

private:
    class Look;
    std::unique_ptr<Look> look;

    AppContext& ctx;
    juce::String mergeId;

    Theme::BypassButton bypass;
    MasterImager imager { ctx };
    juce::TextButton resetLimiter { "リセット"_ju };
    juce::Slider threshold { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    juce::Slider ceiling { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    juce::Slider character { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::TextButton modeButtons[3];

    juce::TextButton matchButton { "-14 LUFS に合わせる"_ju }, resetLoudness { "測り直す"_ju };

    EngineBridge::MasterStatus status;
    int frameCounter = 0;
    float inShown = -100.0f, outShown = -100.0f, grShown = 0.0f;
    // 波形の表示（Ozone の Vintage Limiter のように、入力・出力の大きさとゲインリダクションを右から左へ流す）
    struct Frame { float inDb = -100.0f, outDb = -100.0f, grDb = 0.0f; };
    static constexpr int historyFrames = 360;   // 60 fps で 6 秒
    std::vector<Frame> history;
    std::vector<float> shortTermHistory;

    juce::Rectangle<int> limiterArea, loudnessArea, inMeter, outMeter, waveGraph, characterLabel, loudnessNumbers, loudnessBar, historyGraph;

    void edit (const juce::String& description, std::function<void (collab::MasterLimiter&)>, bool merge);
    void update();
    void matchTarget();

    void paintLimiter (juce::Graphics&);
    void paintWaveform (juce::Graphics&);
    void paintLoudness (juce::Graphics&);

    void changeListenerCallback (juce::ChangeBroadcaster*) override    { update(); }
    void timerCallback() override;
};
