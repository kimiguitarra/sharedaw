#pragma once

#include "ui/AppContext.h"
#include "ui/MidiInputPanel.h"

/** トランスポート（§3.11）: 先頭へ、再生、停止、録音、ループ、メトロノーム、位置表示。 */
class TransportBar  : public juce::Component,
                      private juce::ChangeListener
{
public:
    explicit TransportBar (AppContext&);
    ~TransportBar() override;

    /** 定期的に呼んで位置表示を更新する。 */
    void updatePosition (double tick, double seconds, bool playing);

    std::function<void()> onAudioSettings;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    /** ツール（選択・鉛筆）のボタン。アイコンだけを描く。 */
    struct ToolButton  : public juce::Button
    {
        explicit ToolButton (EditTool t) : juce::Button ({}), tool (t) {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        EditTool tool;
    };

    AppContext& ctx;
    ToolButton selectTool { EditTool::select }, pencilTool { EditTool::pencil }, splitTool { EditTool::split },
               glueTool { EditTool::glue }, eraseTool { EditTool::erase };
    juce::TextButton toStartButton, playButton, stopButton, recordButton, loopButton, metronomeButton, settingsButton;
    juce::ComboBox quantiseBox;
    MidiActivityLight midiLight;
    juce::TextButton snapButton, autoScrollButton;
    juce::Slider metronomeVolume { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    /** クリックで入力、ホイールで増減できる値（テンポ・拍子）。 */
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

    juce::Label barBeatLabel, timeLabel;
    ValueLabel bpmLabel, meterLabel;
    juce::String wheelMergeId;
    juce::uint32 lastWheelTime = 0;

    collab::Tick playheadTick() const;
    void setTempoAtPlayhead (double bpm, const juce::String& mergeId = {});
    void setMeterAtPlayhead (int numerator, int denominator);
    bool wasPlaying = false;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
};
