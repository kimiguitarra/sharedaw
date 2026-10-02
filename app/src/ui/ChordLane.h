#pragma once

#include "ui/AppContext.h"

/**
    コードトラック（§3.8）。Cubase のコードトラックと同様に、任意の拍にコードイベントを置く。
    鉛筆ツール: クリックした拍に空のコードを置く。ダブルクリック（どのツールでも）でコードエディタ。
    右クリック →「コードを一括入力…」: 範囲と間隔を決めて空のコードをまとめて置き、「1625」のようにディグリーで順に入れる。
    コードを選んで数字（1〜7）を打つと、キーに合ったコードが入って次のコードへ進む。
    選択ツール: クリックで選択、ドラッグで移動（1拍にスナップ、Alt で解除）、Delete で削除。
    コードは置いた所で 1 回だけ鳴る（次のコードまで、最長 1 小節）。
*/
class ChordLane  : public juce::Component,
                   public juce::SettableTooltipClient,
                   private juce::ChangeListener
{
public:
    explicit ChordLane (AppContext&);
    ~ChordLane() override;

    /** 横の位置の基準（既定はタイムライン。ピアノロールの画面ではピアノロールの軸を使う）。 */
    void setAxis (TimeAxis* a)                     { axisOverride = a; repaint(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    /** 他の場所を触ったら選択を外す（選んだままだと数字キーでコードが変わってしまうので）。 */
    void focusLost (FocusChangeType) override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void paintOverChildren (juce::Graphics&) override;

    /** 選択中のコードイベントを削除する（削除したら true）。 */
    bool deleteSelected();

    /** コピー・切り取り・貼り付け（Ctrl+C / X / V。貼り付けは再生位置の拍へ）。 */
    bool copySelected (bool cut);
    bool paste (double playheadTick);
    static bool hasClipboard()                     { return clipboard.has_value(); }

    /** イベントの表示用テキスト。 */
    static juce::String displayText (const collab::ChordEvent&);

private:
    AppContext& ctx;
    TimeAxis* axisOverride = nullptr;
    TimeAxis& timeAxis() const                     { return axisOverride != nullptr ? *axisOverride : ctx.state.timeline; }
    static inline std::optional<collab::ChordEvent> clipboard;
    std::string dragId;
    collab::Tick dragOrigTick = 0;
    double dragDownTick = 0;
    juce::String mergeId;

    std::string findHit (float x) const;
    collab::Tick snapToBeat (double tick, const juce::ModifierKeys&) const;
    void openEditor (const std::string& id);
    void addAt (collab::Tick tick);                  // 置いてコードエディタを開く
    std::string addEmptyAt (collab::Tick tick);      // 空のコードを置く（ID を返す）

    /** 一括入力の画面（bar は初期値の開始小節）。 */
    void showBulkDialog (int bar);

    /** fromBar〜toBar に stepTicks（0 なら 1 小節ごと、負なら -n 小節ごと）で空のコードを置き、degrees を順に入れる。 */
    void bulkFill (int fromBar, int toBar, int beatsPerStep, int barsPerStep, const juce::String& degrees);

    /** tick が画面に入るようにタイムラインを横に送る（呼んだ側で state.changed() する）。 */
    void scrollToShow (collab::Tick tick);

    /** ディグリー（'1'〜'7'）をその位置のキーのコードにする。 */
    std::optional<collab::ChordEvent> chordForDegree (juce::juce_wchar digit, collab::Tick) const;

    /** 表示するコードイベントの枠（小さな札。鳴る長さは細い線で示す）。 */
    struct Box
    {
        std::string id;
        juce::String name, degree;
        bool empty = false, noChord = false;
        juce::Rectangle<float> box;
        float soundEndX = 0.0f;
    };

    std::vector<Box> layoutBoxes() const;
    double ghostTick = -1.0;   // 鉛筆ツールで置く位置（なければ -1）
    void setGhost (double tick);
    void changeListenerCallback (juce::ChangeBroadcaster*) override    { repaint(); }
};
