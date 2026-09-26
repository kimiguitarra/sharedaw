#pragma once

#include "ui/AppContext.h"

/**
    ミキサー（F3）: トラックごとの音量フェーダー・パン・ミュート・ソロとレベルメーター。
    値はトラックヘッダーと同じくプロジェクト JSON（音量・パン・ミュート・ソロ）を書き換える。
*/
class MixerView  : public juce::Component,
                   private juce::ChangeListener,
                   private juce::Timer
{
public:
    explicit MixerView (AppContext&);
    ~MixerView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Strip;

    AppContext& ctx;
    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<Strip> strips;

    void rebuild();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
};
