#pragma once

#include "Common.h"
#include "collab/ChannelStripDsp.h"

/**
    各トラックに標準で付く EQ とコンプ（チャンネルストリップ）。音量・パンの直前に挿す。
    設定はプロジェクト JSON が正なので Edit には保存せず、EngineBridge が setStrip() で渡す。
*/
class ChannelStripPlugin  : public te::Plugin
{
public:
    ChannelStripPlugin (te::PluginCreationInfo);
    ~ChannelStripPlugin() override;

    static const char* getPluginName()                          { return "ShareDAWChannelStrip"; }
    static const char* xmlTypeName;

    juce::String getName() const override                       { return getPluginName(); }
    juce::String getPluginType() override                       { return xmlTypeName; }
    juce::String getSelectableDescription() override            { return getName(); }

    bool takesAudioInput() override                             { return true; }
    bool producesAudioWhenNoAudioInput() override               { return false; }
    int getNumOutputChannelsGivenInputs (int numInputs) override { return juce::jmax (2, numInputs); }

    void initialise (const te::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void reset() override;
    void applyToBuffer (const te::PluginRenderContext&) override;

    /** 設定を渡す（メッセージスレッドから）。次のブロックから反映される。 */
    void setStrip (const collab::ChannelStrip&);
    const collab::ChannelStrip& getStrip() const noexcept       { return current; }

    /** 直近のゲインリダクション（dB）。メーター表示用。 */
    float getGainReductionDb() const noexcept                   { return dsp.getGainReductionDb(); }

private:
    collab::ChannelStripDsp dsp;
    collab::ChannelStrip current, pending;
    juce::SpinLock lock;
    std::atomic<bool> hasPending { false }, needsReset { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelStripPlugin)
};
