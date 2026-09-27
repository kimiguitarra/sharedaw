#pragma once

#include "ui/AppContext.h"
#include "ui/CompressorPanel.h"
#include "ui/EqGraph.h"

/**
    トラックの EQ・Compressor（チャンネルストリップ）の編集画面。
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
    AppContext& ctx;
    std::string trackId;
    juce::String mergeId;

    juce::ToggleButton eqEnabled { "EQ"_ju }, compEnabled { "Compressor"_ju };
    juce::ComboBox compType;
    juce::TextButton resetEq { "EQ をリセット"_ju }, resetComp { "Compressor をリセット"_ju };

    EqGraph eqGraph;
    juce::Label eqHint;
    CompressorPanel compressor;

    juce::Rectangle<int> eqArea, compArea;

    const collab::Track* getTrack() const;
    void edit (const juce::String& description, std::function<void (collab::ChannelStrip&)>, bool merge);
    void update();
    void changeListenerCallback (juce::ChangeBroadcaster*) override    { update(); }
    void timerCallback() override;
};
