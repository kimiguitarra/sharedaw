#pragma once

#include "ui/AppContext.h"

/** トラックヘッダー（名前、音源、ミュート・ソロ、音量、パン）。§3.11 */
class TrackHeader  : public juce::Component
{
public:
    TrackHeader (AppContext&, const std::string& trackId);

    const std::string& getTrackId() const noexcept     { return trackId; }

    void update();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    AppContext& ctx;
    std::string trackId;

    juce::Label nameLabel;
    juce::TextButton instrumentButton, muteButton { "M" }, soloButton { "S" };
    juce::Slider volumeSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider panSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::String dragMergeId;
    juce::String problem;

    void select();
    void showMenu();
    void showInstrumentMenu();
    void editTrack (const juce::String& description, std::function<void (collab::Track&)> fn, const juce::String& mergeId = {});
};
