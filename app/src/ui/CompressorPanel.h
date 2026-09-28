#pragma once

#include "ui/AppContext.h"

/**
    Compressor の操作パネル。種類に合わせて見た目を切り替える。
    - FET: 1176 風（黒いパネル、INPUT / OUTPUT / ATTACK / RELEASE、レシオのボタン 4・8・12・20）
    - Optical: LA-2A 風（明るいパネル、GAIN / PEAK REDUCTION、COMPRESS / LIMIT の切り替え）
    どちらにも LOW THRU（この周波数より下をかかり具合の検出に使わない。OFF〜500 Hz）がある。
    どちらもゲインリダクションを針の VU メーターで見せる。
*/
class CompressorPanel  : public juce::Component,
                         private juce::Timer
{
public:
    CompressorPanel();
    ~CompressorPanel() override;

    void setComp (const collab::ChannelComp&);
    void setGainReduction (float db)                 { targetGr = db; }

    /** 編集を適用する。merge が true ならドラッグ中の一連の変更を 1 つの取り消し単位にまとめる。 */
    std::function<void (const juce::String& description, std::function<void (collab::ChannelComp&)>, bool merge)> onEdit;
    std::function<void()> onEditEnd;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    /** ゲインリダクションを針で示す VU メーター。 */
    struct VuMeter  : public juce::Component
    {
        void paint (juce::Graphics&) override;
        float grDb = 0.0f;
        bool light = false;   // 明るいパネル（Optical）用の色
    };

    /** つまみ（下に値を出す）。 */
    struct Knob  : public juce::Component
    {
        Knob (juce::String name, bool big);
        void resized() override;
        void paint (juce::Graphics&) override;

        juce::String name;
        bool big;
        bool light = false;
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
        std::function<juce::String (double)> format;
    };

    class KnobLook;
    std::unique_ptr<KnobLook> look;

    collab::ChannelComp comp;
    bool opto = false;
    float targetGr = 0.0f;
    bool dragging = false;

    VuMeter meter;
    Knob input { "INPUT", false }, output { "OUTPUT", false }, attack { "ATTACK", false }, release { "RELEASE", false };
    Knob gain { "GAIN", true }, peakReduction { "PEAK REDUCTION", true };
    Knob lowThru { "LOW THRU", false };   // 低域のスルー（両方の種類）
    juce::TextButton ratioButtons[4];
    juce::TextButton compressButton { "COMPRESS" }, limitButton { "LIMIT" };
    static constexpr double ratios[4] = { 4.0, 8.0, 12.0, 20.0 };

    juce::Rectangle<int> faceplate;

    void bind (Knob&, double min, double max, double skewMid, double reset, const juce::String& description,
               std::function<double (const collab::ChannelComp&)> get, std::function<void (collab::ChannelComp&, double)> set);
    std::vector<std::pair<Knob*, std::function<double (const collab::ChannelComp&)>>> getters;
    void setRatio (double ratio, const juce::String& description);
    void updateVisibility();
    void timerCallback() override;
};
