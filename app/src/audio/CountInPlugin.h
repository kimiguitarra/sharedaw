#pragma once

#include "Common.h"
#include "collab/Recording.h"

/**
    録音のカウントインのクリック（§3.5）。マスターに挿して、指定した時刻（曲の先頭からの秒）で鳴らす。

    カウントインは録音開始位置より前、曲の先頭より前（負の時刻）にもなるので、
    メトロノームのトラック（クリップは 0 秒以降にしか置けない）ではなく、再生位置を見て自分で鳴らす。
*/
class CountInPlugin  : public te::Plugin
{
public:
    CountInPlugin (te::PluginCreationInfo);
    ~CountInPlugin() override;

    static const char* getPluginName()                          { return "CollabCountIn"; }
    static const char* xmlTypeName;

    juce::String getName() const override                       { return getPluginName(); }
    juce::String getPluginType() override                       { return xmlTypeName; }
    juce::String getSelectableDescription() override            { return getName(); }

    bool takesAudioInput() override                             { return true; }
    bool producesAudioWhenNoAudioInput() override               { return true; }
    int getNumOutputChannelsGivenInputs (int) override          { return 2; }

    void initialise (const te::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void applyToBuffer (const te::PluginRenderContext&) override;

    /** 鳴らすクリック（メッセージスレッドから）。空にすると止まる。 */
    void setClicks (std::vector<collab::Click>, float volumeDb);

private:
    juce::SpinLock lock;
    std::vector<collab::Click> clicks;
    float gain = 0.5f;
    double sampleRate = 48000.0;

    // 鳴っているクリック
    int remaining = 0, elapsed = 0;
    double frequency = 1000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CountInPlugin)
};
