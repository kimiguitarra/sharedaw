#pragma once

#include "ui/AppContext.h"

/**
    内蔵エフェクトの画面（つまみを横に並べる）。値はプロジェクト JSON の effect.params を書き換える。
    つまみはドラッグで回し、ダブルクリックで既定値、数字をクリックで打ち込める。段階のあるつまみ（レシオなど）は選んだ値に吸い付く。
    バスコンプはゲインリダクションのメーター（VU の針）を出す。
*/
class BuiltinEffectEditor  : public juce::Component,
                             private juce::ChangeListener,
                             private juce::Timer
{
public:
    BuiltinEffectEditor (AppContext&, std::string trackId, std::string effectId);
    ~BuiltinEffectEditor() override;

    juce::String getTitle() const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob;

    AppContext& ctx;
    std::string trackId, effectId;
    std::optional<collab::fx::Type> type;
    juce::OwnedArray<Knob> knobs;
    juce::TextButton bypassButton, presetButton;
    juce::Rectangle<int> meterArea, titleArea;
    float shownGr = 0.0f;
    juce::String mergeId;

    const collab::Effect* effect() const;
    void setParam (const std::string& key, double value);
    void showPresets();
    void refresh();
    void changeListenerCallback (juce::ChangeBroadcaster*) override    { refresh(); }
    void timerCallback() override;
};
