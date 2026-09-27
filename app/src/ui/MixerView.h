#pragma once

#include "ui/AppContext.h"

/**
    ミキサー（F3）。Cubase の MixConsole のように、ストリップごとに上から
    出力先（ROUTING）・インサート・EQ・コンプ・センド・パン・フェーダーとメーター・M/S・名前を並べる。
    値はトラックヘッダーと同じくプロジェクト JSON を書き換える。
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
    juce::TextButton addBusButton;
    juce::Label hint;
    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<Strip> strips;

    void rebuild();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
};
