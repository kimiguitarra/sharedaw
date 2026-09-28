#pragma once

#include "ui/AppContext.h"
#include "ui/Theme.h"

/** クリックで入力、ホイールで増減できる値（テンポ・拍子・ループ範囲）。 */
struct ValueLabel  : public juce::Label
{
    std::function<void (int direction)> onWheel;

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (onWheel != nullptr && ! isBeingEdited() && std::abs (w.deltaY) > 0.0f)
            onWheel (w.deltaY > 0 ? 1 : -1);
        else
            juce::Label::mouseWheelMove (e, w);
    }
};

/** 旗のアイコン（左右のロケーター）。 */
struct FlagIcon  : public juce::Component,
                   public juce::SettableTooltipClient
{
    explicit FlagIcon (bool isLeft) : left (isLeft) {}

    void paint (juce::Graphics& g) override
    {
        g.setColour (Theme::accent);
        g.fillPath (Theme::iconPath (left ? "flagL" : "flagR", getLocalBounds().toFloat().reduced (2.0f)));
    }

    bool left;
};

/**
    画面上部のツールバー（Cubase のプロジェクトウィンドウのツールバー）:
    ツール、クオンタイズ値・スナップ・自動スクロール、メトロノーム、曲のテンポ・拍子・キー。
*/
class ToolBar  : public juce::Component,
                 private juce::ChangeListener
{
public:
    explicit ToolBar (AppContext&);
    ~ToolBar() override;

    /** 定期的に呼ぶ（再生位置のテンポ・拍子・キー）。 */
    void update();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    /** ツール（選択・鉛筆・はさみ）のボタン。アイコンだけを描く。 */
    struct ToolButton  : public juce::Button
    {
        explicit ToolButton (EditTool t) : juce::Button ({}), tool (t) {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        EditTool tool;
    };

    AppContext& ctx;
    ToolButton selectTool { EditTool::select }, pencilTool { EditTool::pencil }, splitTool { EditTool::split };
    juce::ComboBox quantiseBox;
    Theme::IconButton snapButton { "snap" }, autoScrollButton { "follow" }, metronomeButton { "metronome" };
    std::vector<juce::Rectangle<int>> groups;   // ガラスのまとまり（ツール、クオンタイズ、メトロノーム…）
    juce::Slider metronomeVolume { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    ValueLabel bpmLabel, meterLabel;
    juce::Label keyLabel;
    juce::String wheelMergeId;
    juce::uint32 lastWheelTime = 0;
    collab::Tick lastTick = -1;

    collab::Tick playheadTick() const;
    void setTempoAtPlayhead (double bpm, const juce::String& mergeId = {});
    void setMeterAtPlayhead (int numerator, int denominator);
    void refreshTempo();

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
};

/**
    画面下部のトランスポート（Cubase と同じ並び）:
    左に左右のロケーター（旗）、中央にサイクル・停止・再生・録音、その右に現在の位置（小節. 拍. tick）。
*/
class TransportBar  : public juce::Component,
                      private juce::ChangeListener
{
public:
    explicit TransportBar (AppContext&);
    ~TransportBar() override;

    /** 定期的に呼んで位置表示を更新する。 */
    void updatePosition (double tick, double seconds, bool playing);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AppContext& ctx;
    FlagIcon loopStartFlag { true }, loopEndFlag { false };
    ValueLabel loopStartLabel, loopEndLabel;
    Theme::IconButton loopButton { "loop" }, stopButton { "stop" }, playButton { "play" }, recordButton { "record" };
    std::vector<juce::Rectangle<int>> groups;
    juce::Label barBeatLabel;
    bool wasPlaying = false;

    juce::String formatPosition (collab::Tick) const;
    std::optional<collab::Tick> parsePosition (const juce::String&) const;
    void setLoopEdge (bool start, collab::Tick);
    void refreshLoop();

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
};
