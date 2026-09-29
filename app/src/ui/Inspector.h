#pragma once

#include "ui/AppContext.h"
#include "ui/MixerView.h"

/**
    インスペクター（Cubase と同じく、画面の左に選択中のトラックの設定を出す）。
    上から 入出力（入力・出力とそれぞれのモノ / ステレオ）、音源（MIDI トラック）、チャンネルストリップ
    （インサート・EQ・コンプ・センド・パン・フェーダー・メーター・M/S）。
*/
class Inspector  : public juce::Component,
                   private juce::ChangeListener
{
public:
    explicit Inspector (AppContext&);
    ~Inspector() override;

    static constexpr int defaultWidth = 200, minWidth = 170, maxWidth = 420;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Content;

    AppContext& ctx;
    juce::Viewport viewport;
    std::unique_ptr<Content> content;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
};
