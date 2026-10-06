#pragma once

#include "ui/AppContext.h"
#include "ui/Theme.h"

/**
    内蔵エフェクトの画面（つまみを横に並べる）。値はプロジェクト JSON の effect.params を書き換える。
    つまみはドラッグで回し、ダブルクリックで既定値、数字をクリックで打ち込める。段階のあるつまみ（レシオなど）は選んだ値に吸い付く。
    バスコンプは SSL のバスコンプのような並び（ゲインリダクションの VU メーターの針）、
    ノイズゲートは入ってきた音・ゲインリダクション・出ていく音が流れていく画面（スレッショルドの線をドラッグで動かせる）。
*/
class BuiltinEffectEditor  : public juce::Component,
                             private juce::ChangeListener,
                             private juce::Timer
{
public:
    BuiltinEffectEditor (AppContext&, std::string trackId, std::string effectId);
    ~BuiltinEffectEditor() override;

    juce::String getTitle() const;

    /** メーターの値を 1 回分足す（入力・出力のピーク 0〜1、ゲインリダクション dB）。タイマーから呼ぶ（確認用に外からも渡せる）。 */
    void pushMeter (float inputPeak, float outputPeak, float gainReductionDb);

    /** ノイズゲートの波形を 1 列足す（右端が新しい）。 */
    void pushWave (const EngineBridge::WaveColumn&);

    /** エンジンからノイズゲートの波形の新しい列を読む。読めたら true。 */
    bool pollWave();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    struct Knob;

    AppContext& ctx;
    std::string trackId, effectId;
    std::optional<collab::fx::Type> type;
    juce::OwnedArray<Knob> knobs;
    Theme::BypassButton bypassButton;
    juce::Rectangle<int> meterArea, titleArea, displayArea;
    float shownGr = 0.0f;
    float vuGr = 0.0f;                         // バスコンプの VU の針（VU の速さでなめらかに動かす）
    juce::String mergeId;

    // ノイズゲートの画面: 入ってきた音・出ていく音の波形（256 サンプルごとの最小・最大）とゲインリダクションの流れ（古い順の輪）
    std::vector<EngineBridge::WaveColumn> wave = std::vector<EngineBridge::WaveColumn> (1600);
    size_t wavePos = 0;
    juce::uint64 waveCursor = 0;
    bool draggingThreshold = false;

    bool isBusComp() const;
    bool isGate() const;
    Knob* knobFor (const std::string& key) const;
    void paintBusComp (juce::Graphics&);
    void paintGate (juce::Graphics&);
    juce::Rectangle<float> gatePlot() const;
    float gateDistance (float db) const;   // 波形の真ん中からの距離（上下対称。真ん中が -80 dB、端が 0 dB）
    double thresholdDb() const;

    const collab::Effect* effect() const;
    void setParam (const std::string& key, double value);
    void refresh();
    void changeListenerCallback (juce::ChangeBroadcaster*) override    { refresh(); }
    void timerCallback() override;
};
