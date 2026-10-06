#pragma once

#include "ui/AppContext.h"
#include "ui/CompressorPanel.h"
#include "ui/EqGraph.h"
#include "ui/Theme.h"

/**
    トラックの EQ または Compressor（チャンネルストリップ）の編集画面。EQ と Compressor は別々のウィンドウで開く。
    値はプロジェクト JSON（Track::strip）を書き換え、EngineBridge が音に反映する。
*/
class ChannelStripEditor  : public juce::Component,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    enum class Section { eq, comp };

    ChannelStripEditor (AppContext&, std::string trackId, Section);
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
    const Section section;
    juce::String mergeId;

    Theme::BypassButton bypass;
    juce::ComboBox compType;
    juce::TextButton resetButton;

    std::unique_ptr<EqGraph> eqGraph;
    juce::Label eqHint;
    std::unique_ptr<CompressorPanel> compressor;

    const collab::Track* getTrack() const;
    bool isMaster() const;
    std::optional<collab::ChannelStrip> currentStrip() const;
    void edit (const juce::String& description, std::function<void (collab::ChannelStrip&)>, bool merge);
    void update();
    void changeListenerCallback (juce::ChangeBroadcaster*) override    { update(); }
    void timerCallback() override;
};
