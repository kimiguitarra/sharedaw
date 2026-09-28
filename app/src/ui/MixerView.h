#pragma once

#include "ui/AppContext.h"

/**
    ミキサー（F3）。Cubase の MixConsole のように、ストリップごとに上から
    出力先（ROUTING）・インサート・EQ・コンプ・センド・パン・フェーダーとメーター・M/S・名前を並べる。
    左端に入力（オーディオ機器の入力レベル）、右端にメトロノームとマスターを固定し、間のトラックだけ横にスクロールする。
    音量はフェーダー下の数字をクリック、パン・センドはダブルクリックで数値を打ち込める。
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
    class InputStrip;

    AppContext& ctx;
    juce::TextButton addBusButton;
    juce::Label hint;
    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<Strip> strips, fixedStrips;   // fixedStrips: 右端のメトロノームとマスター
    std::unique_ptr<InputStrip> inputStrip;         // 左端の入力

    void rebuild();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
};
