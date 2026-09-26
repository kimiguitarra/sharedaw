#pragma once

#include "ui/AppContext.h"

/**
    内蔵音源の調整（§3.3）。値はすべて instrument.params として JSON に保持する。
    - 全音源: 音量、パン、トーン
    - ドラム: キット、パーツごとのサンプル差し替え・音量・パン・チューニング
*/
class InstrumentPanel  : public juce::Component,
                         private juce::ChangeListener
{
public:
    InstrumentPanel (AppContext&, const std::string& trackId);
    ~InstrumentPanel() override;

    static void show (AppContext&, const std::string& trackId, juce::Component& target);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct PieceRow
    {
        std::string key;
        std::unique_ptr<juce::Label> name;
        std::unique_ptr<juce::ComboBox> sample;
        std::unique_ptr<juce::Slider> volume, pan, tune;
    };

    AppContext& ctx;
    std::string trackId;
    const collab::BuiltinInstrumentManifest* manifest = nullptr;

    juce::Label title;
    juce::Slider volume, pan, tone;
    juce::Label volumeLabel { {}, "音量"_ju }, panLabel { {}, "パン"_ju }, toneLabel { {}, "トーン"_ju };
    juce::ComboBox kitBox;
    juce::Label kitLabel { {}, "キット"_ju };
    juce::Label credits;
    std::vector<PieceRow> rows;
    juce::String mergeId;

    void refresh();
    void setParam (const juce::String& description, std::function<void (nlohmann::json&)> fn, bool merge = false);
    void setupSlider (juce::Slider&, double min, double max, double step, double reset, const juce::String& suffix);
    nlohmann::json currentParams() const;
    void changeListenerCallback (juce::ChangeBroadcaster*) override    { refresh(); }
};
