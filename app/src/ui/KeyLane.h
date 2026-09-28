#pragma once

#include "ui/AppContext.h"
#include "collab/chord/Degree.h"

/**
    キートラック: 小節の頭にキー（調）を置く。コードのディグリー表示とディグリー入力の基準になる。
    鉛筆ツール: クリックした小節にキーを追加（一覧から選ぶ）。選択ツール: ドラッグで移動、ダブルクリックでキーを変更、Delete で削除。
    右クリックでメニュー（コード進行からキーを推定することもできる）。
*/
class KeyLane  : public juce::Component,
                 public juce::SettableTooltipClient,
                 private juce::ChangeListener
{
public:
    explicit KeyLane (AppContext&);
    ~KeyLane() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void paintOverChildren (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;

    /** 選択中のキーを削除する（削除したら true）。 */
    bool deleteSelected();

    /** キーを選ぶメニュー（current に印を付ける）。 */
    static juce::PopupMenu keyMenu (std::optional<collab::chord::Key> current, std::function<void (collab::chord::Key)> onPick);

    /** bar 小節目にキーを置く（あれば書き換える）。 */
    static void setKey (AppContext&, int bar, collab::chord::Key);

private:
    AppContext& ctx;
    std::string dragId;
    juce::String mergeId;

    std::string findHit (float x) const;
    int barAt (float x) const;
    void setKeyAt (int bar, collab::chord::Key);
    void changeKey (const std::string& id);
    void showMenu (const std::string& id, int bar);
    void estimateFromChords (int bar);
    double ghostTick = -1.0;   // 鉛筆ツールで置く位置（なければ -1）
    void setGhost (double tick);
    void changeListenerCallback (juce::ChangeBroadcaster*) override    { repaint(); }
};
