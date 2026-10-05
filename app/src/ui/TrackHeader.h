#pragma once

#include "ui/AppContext.h"

/** トラックヘッダー（名前、録音待機・ミュート・ソロ）。音源・入出力・音量・パンは左のインスペクター。§3.11 */
class TrackHeader  : public juce::Component,
                     public juce::SettableTooltipClient
{
public:
    TrackHeader (AppContext&, const std::string& trackId);

    const std::string& getTrackId() const noexcept     { return trackId; }

    void update();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    /** ヘッダーをドラッグして並べ替えたとき（y は親の座標でのヘッダーの中心）。 */
    std::function<void (const std::string& trackId, int y)> onReorderDrop;

private:
    // 下の端をドラッグで高さ、それ以外を上下にドラッグで並べ替え（Cubase と同じ）
    enum class Drag { none, pending, resize, reorder };
    Drag drag = Drag::none;
    int dragStartHeight = 0, dragStartY = 0;
    static constexpr int resizeEdge = 5;

    AppContext& ctx;
    std::string trackId;

    juce::Label nameLabel;
    juce::TextButton muteButton { "M" }, soloButton { "S" }, armButton;
    juce::TextButton automationButton { "A" };   // オートメーションのレーンを出す・隠す
    juce::ComboBox automationParam;              // レーンに出すパラメーター（レーンの左）
    juce::String dragMergeId;
    juce::String problem;
    juce::Rectangle<int> badgeArea;   // 同期中のロックの印（名前の右）
    bool badgeShown = false;

    void select();
    void showMenu();
    void editTrack (const juce::String& description, std::function<void (collab::Track&)> fn, const juce::String& mergeId = {});
};
