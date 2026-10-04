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
    friend class TrackChannelStrip;
    class Strip;
    class InputStrip;

    AppContext& ctx;
    juce::TextButton addBusButton;
    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<Strip> strips, fixedStrips;   // fixedStrips: 右端のメトロノームとマスター
    std::unique_ptr<InputStrip> inputStrip;         // 左端の入力

    void rebuild();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
};

/**
    1 トラック分のチャンネルストリップ（ミキサーと同じ部品）。左のインスペクターに出す。
    インサート・EQ・コンプ・センド・パン・フェーダー・メーター・M/S。
*/
class TrackChannelStrip  : public juce::Component,
                           private juce::ChangeListener,
                           private juce::Timer
{
public:
    explicit TrackChannelStrip (AppContext&);
    ~TrackChannelStrip() override;

    void setTrack (const std::string& trackId);
    void resized() override;

private:
    AppContext& ctx;
    std::string trackId;
    std::unique_ptr<MixerView::Strip> strip;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
};
