#pragma once

#include "ui/AppContext.h"
#include "ui/EqGraph.h"

/**
    トラックの EQ・コンプ（チャンネルストリップ）の編集画面。
    値はプロジェクト JSON（Track::strip）を書き換え、EngineBridge が音に反映する。
*/
class ChannelStripEditor  : public juce::Component,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    ChannelStripEditor (AppContext&, std::string trackId);
    ~ChannelStripEditor() override;

    void setTrack (std::string trackId);
    const std::string& getTrackId() const noexcept     { return trackId; }

    /** トラック名（ウィンドウのタイトル用）。 */
    juce::String getTitle() const;
    std::function<void()> onTitleChanged;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob  : public juce::Component
    {
        Knob (const juce::String& name, std::function<juce::String (double)> format);
        void resized() override;

        juce::Label label;
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        std::function<juce::String (double)> formatValue;
    };

    struct GainReductionMeter  : public juce::Component
    {
        void paint (juce::Graphics&) override;
        float db = 0.0f;
    };

    AppContext& ctx;
    std::string trackId;
    juce::String mergeId;

    juce::ToggleButton eqEnabled { "EQ"_ju }, compEnabled { "コンプ"_ju };
    juce::ComboBox compType;
    juce::TextButton resetEq { "EQ をリセット"_ju }, resetComp { "コンプをリセット"_ju };

    EqGraph eqGraph;
    juce::Label eqHint;
    Knob threshold, ratio, attack, release, makeup;
    GainReductionMeter grMeter;
    juce::Label grLabel, optoNote;

    juce::Rectangle<int> eqArea, compArea;

    std::vector<Knob*> compKnobs()    { return { &threshold, &ratio, &attack, &release, &makeup }; }

    const collab::Track* getTrack() const;
    void edit (const juce::String& description, std::function<void (collab::ChannelStrip&)>, bool merge);
    void bind (Knob&, double min, double max, double defaultValue, double skewMid,
               const juce::String& description, std::function<void (collab::ChannelStrip&, double)>);
    void update();
    void changeListenerCallback (juce::ChangeBroadcaster*) override    { update(); }
    void timerCallback() override;
};
