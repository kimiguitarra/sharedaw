#pragma once

#include "ui/AppContext.h"

/**
    マーカートラック（Cubase のマーカートラック準拠）。マーカーには左から順に 1, 2, 3 … と番号を振り、
    Shift + 数字キーでその位置へジャンプできる。
    鉛筆ツール: クリックで追加。選択ツール: クリックで選択、ドラッグで移動（クオンタイズ値、Alt で自由）、
    ダブルクリックで名前の変更、Delete で削除。右クリックでメニュー。
*/
class MarkerLane  : public juce::Component,
                    public juce::SettableTooltipClient,
                    private juce::ChangeListener
{
public:
    explicit MarkerLane (AppContext&);
    ~MarkerLane() override;

    /** 横の位置の基準（既定はタイムライン。ピアノロールの画面ではピアノロールの軸を使う）。 */
    void setAxis (TimeAxis* a)                     { axisOverride = a; repaint(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void paintOverChildren (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;

    /** 選択中のマーカーを削除する（削除したら true）。 */
    bool deleteSelected();

    /** 位置順（番号順）に並べたマーカー。 */
    static std::vector<collab::Marker> sorted (const collab::Project&);

    /** 指定した位置にマーカーを追加する（同じ位置にあれば何もしない）。 */
    static void addMarker (AppContext&, collab::Tick);

private:
    AppContext& ctx;
    TimeAxis* axisOverride = nullptr;
    TimeAxis& timeAxis() const                     { return axisOverride != nullptr ? *axisOverride : ctx.state.timeline; }
    std::string dragId;
    collab::Tick dragOrigTick = 0;
    double dragDownTick = 0;
    juce::String mergeId;

    juce::Rectangle<float> labelBounds (const collab::Marker&, int number) const;
    std::string findHit (float x) const;
    collab::Tick snap (double tick, const juce::ModifierKeys&) const;
    void rename (const std::string& id);
    double ghostTick = -1.0;   // 鉛筆ツールで置く位置（なければ -1）
    void setGhost (double tick);
    void changeListenerCallback (juce::ChangeBroadcaster*) override    { repaint(); }
};
