#pragma once

#include "ui/AppContext.h"
#include "ui/MidiInputPanel.h"

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

/**
    画面上部のツールバー（Cubase のプロジェクトウィンドウのツールバー）:
    ツール、クオンタイズ値、スナップ、自動スクロール、メトロノーム、MIDI 入力、オーディオ設定。
*/
class ToolBar  : public juce::Component,
                 private juce::ChangeListener
{
public:
    explicit ToolBar (AppContext&);
    ~ToolBar() override;

    std::function<void()> onAudioSettings;

    /** 同期の状態のバッジ（右側に置く）。 */
    void setSyncBadge (juce::Component*);

    /** 定期的に呼ぶ（MIDI 入力のランプ）。 */
    void update();

    void paint (juce::Graphics&) override;
    void resized() override;

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
    juce::TextButton snapButton, autoScrollButton, metronomeButton, settingsButton;
    juce::Slider metronomeVolume { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    MidiActivityLight midiLight;
    juce::Component* syncBadge = nullptr;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
};

/**
    画面下部のトランスポート（Cubase と同じ並び）:
    左にループ範囲（開始・終了）、中央にループ・停止・再生・録音、右に現在の位置とテンポ・拍子。
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
    juce::Label loopStartTitle, loopEndTitle;
    ValueLabel loopStartLabel, loopEndLabel;
    juce::TextButton loopButton, stopButton, playButton, recordButton;
    juce::Label barBeatLabel, timeLabel;
    ValueLabel bpmLabel, meterLabel;
    juce::String wheelMergeId;
    juce::uint32 lastWheelTime = 0;
    bool wasPlaying = false;

    collab::Tick playheadTick() const;
    void setTempoAtPlayhead (double bpm, const juce::String& mergeId = {});
    void setMeterAtPlayhead (int numerator, int denominator);

    juce::String formatPosition (collab::Tick) const;
    std::optional<collab::Tick> parsePosition (const juce::String&) const;
    void setLoopEdge (bool start, collab::Tick);
    void refreshLoop();

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
};
