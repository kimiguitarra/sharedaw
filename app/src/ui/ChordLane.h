#pragma once

#include "ui/AppContext.h"

/**
    コードトラック（§3.8）。Cubase のコードトラックと同様に、任意の拍にコードイベントを置く。
    ダブルクリック: 追加／コードエディタ、ドラッグ: 移動（1拍にスナップ、Alt で解除）、Delete: 削除。
*/
class ChordLane  : public juce::Component,
                   public juce::SettableTooltipClient,
                   private juce::ChangeListener
{
public:
    explicit ChordLane (AppContext&);
    ~ChordLane() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    /** 選択中のコードイベントを削除する（削除したら true）。 */
    bool deleteSelected();

    /** イベントの表示用テキスト。 */
    static juce::String displayText (const collab::ChordEvent&);

private:
    AppContext& ctx;
    std::string dragId;
    collab::Tick dragOrigTick = 0;
    double dragDownTick = 0;
    juce::String mergeId;

    std::string findHit (float x) const;
    collab::Tick snapToBeat (double tick, const juce::ModifierKeys&) const;
    void openEditor (const std::string& id);
    void addAt (collab::Tick tick);
    void changeListenerCallback (juce::ChangeBroadcaster*) override    { repaint(); }
};
