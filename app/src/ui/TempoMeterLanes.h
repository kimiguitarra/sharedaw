#pragma once

#include "ProjectDocument.h"
#include "EditorState.h"

/**
    テンポトラック（§3.9）: 任意の位置にテンポ変更イベント（階段状）。
    ダブルクリックで追加・編集、ドラッグで移動、右クリックでメニュー。
*/
class TempoLane  : public juce::Component,
                   public juce::SettableTooltipClient,
                   private juce::ChangeListener
{
public:
    TempoLane (ProjectDocument&, EditorState&);
    ~TempoLane() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    ProjectDocument& document;
    EditorState& state;
    std::string dragId;
    juce::String mergeId;

    std::string findHit (float x) const;
    void editEvent (const std::string& id);
    void addEventAt (collab::Tick tick);
    void changeListenerCallback (juce::ChangeBroadcaster*) override    { repaint(); }
};

/** 拍子トラック（§3.9）: 小節の頭に拍子変更イベント。 */
class MeterLane  : public juce::Component,
                   public juce::SettableTooltipClient,
                   private juce::ChangeListener
{
public:
    MeterLane (ProjectDocument&, EditorState&);
    ~MeterLane() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    /** "3/4" のような文字列を解釈する。 */
    static std::optional<std::pair<int, int>> parseMeter (const juce::String&);

private:
    ProjectDocument& document;
    EditorState& state;
    std::string dragId;
    juce::String mergeId;

    std::string findHit (float x) const;
    void editEvent (const std::string& id);
    void addEventAt (int bar);
    void changeListenerCallback (juce::ChangeBroadcaster*) override    { repaint(); }
};
