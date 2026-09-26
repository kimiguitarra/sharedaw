#pragma once

#include "Common.h"

namespace sfz { class Sfizz; }

/**
    sfizz を Tracktion の内部プラグインとして鳴らす（§3.3）。
    状態はプロジェクト JSON が正なので、Tracktion の Edit には保存しない。
    EngineBridge が setSfz() / setGainAndPan() で設定する。
*/
class SfizzPlugin  : public te::Plugin
{
public:
    SfizzPlugin (te::PluginCreationInfo);
    ~SfizzPlugin() override;

    static const char* getPluginName()                          { return "CollabSfizz"; }
    static const char* xmlTypeName;

    juce::String getName() const override                       { return getPluginName(); }
    juce::String getPluginType() override                       { return xmlTypeName; }
    juce::String getSelectableDescription() override            { return getName(); }

    bool isSynth() override                                     { return true; }
    bool takesMidiInput() override                              { return true; }
    bool takesAudioInput() override                             { return false; }
    bool producesAudioWhenNoAudioInput() override               { return true; }
    bool noTail() override                                      { return false; }
    double getTailLength() const override                       { return 2.0; }
    int getNumOutputChannelsGivenInputs (int) override          { return 2; }

    void initialise (const te::PluginInitialisationInfo&) override;
    void deinitialise() override;
    void reset() override;
    void midiPanic() override;
    void applyToBuffer (const te::PluginRenderContext&) override;

    /** SFZ テキストを読み込む（メッセージスレッドから呼ぶ）。virtualPath は #include の基準になるファイルパス。 */
    bool setSfz (const juce::String& virtualPath, const juce::String& sfzText);

    /** 空の音（音源が見つからない場合など）。 */
    void clearSfz();

    /** 音源全体の音量・パン（SFZ の再読み込みなしで変更できる）。 */
    void setGainAndPan (float gainDb, float pan);

    const juce::String& getLoadedSfzText() const noexcept       { return loadedText; }

private:
    std::unique_ptr<sfz::Sfizz> createSynth (const juce::String& path, const juce::String& text) const;

    juce::SpinLock synthLock;
    std::unique_ptr<sfz::Sfizz> synth;

    juce::String loadedPath, loadedText;
    double sampleRate = 48000.0;
    int maxBlockSize = 512;

    std::atomic<float> gainLeft { 1.0f }, gainRight { 1.0f };
    juce::AudioBuffer<float> scratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SfizzPlugin)
};
