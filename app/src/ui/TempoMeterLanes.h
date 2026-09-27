#pragma once

#include "ProjectDocument.h"
#include "EditorState.h"

/**
    テンポトラック（§3.9）: 任意の位置にテンポ変更イベント（階段状）。
    鉛筆ツール: クリックで追加。選択ツール: クリックで選択、ドラッグで移動、ダブルクリックで編集、Delete で削除。
    右クリックでメニュー。
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
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void paintOverChildren (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;

    /** 選択中のテンポ変更を削除する（先頭は消せない）。削除したら true。 */
    bool deleteSelected();

private:
    ProjectDocument& document;
    EditorState& state;
    std::string dragId;
    juce::String mergeId;

    std::string findHit (float x) const;
    void editEvent (const std::string& id);
    void addEventAt (collab::Tick tick);
    void showMenu (const std::string& id, collab::Tick tick);
    double ghostTick = -1.0;   // 鉛筆ツールで置く位置（なければ -1）
    void setGhost (double tick);
    void changeListenerCallback (juce::ChangeBroadcaster*) override    { repaint(); }
};

/** 拍子トラック（§3.9）: 小節の頭に拍子変更イベント。操作はテンポトラックと同じ。 */
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
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void paintOverChildren (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;

    /** 選択中の拍子変更を削除する（1 小節目は消せない）。削除したら true。 */
    bool deleteSelected();

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
    int barAt (float x) const;
    void showMenu (const std::string& id, int bar);
    double ghostTick = -1.0;   // 鉛筆ツールで置く位置（なければ -1）
    void setGhost (double tick);
    void changeListenerCallback (juce::ChangeBroadcaster*) override    { repaint(); }
};
