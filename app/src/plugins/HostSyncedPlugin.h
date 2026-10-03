#pragma once

#include <array>

#include "Common.h"
#include "collab/TempoMap.h"

/**
    外部プラグインに知らせる曲のテンポ・拍子（Follow Host 用）。

    Edit のテンポは 60BPM・4/4 に固定している（EngineBridge.h）ので、Tracktion がプラグインに渡す再生位置のままだと
    プラグインには 60BPM・4/4 に見える。HostSyncedExternalPlugin がテンポ・拍子・PPQ をこのテンポマップで置き換える。
*/
class HostTempo
{
public:
    /** メッセージスレッドから。 */
    void setTempoMap (const collab::TempoMap&);
    void setLoopRange (double startSeconds, double endSeconds) noexcept;

    /** 音の処理のスレッドからも呼ばれる。 */
    const collab::TempoMap* getTempoMap() const noexcept    { return current.load (std::memory_order_acquire); }
    double getLoopStart() const noexcept                    { return loopStart.load (std::memory_order_relaxed); }
    double getLoopEnd() const noexcept                      { return loopEnd.load (std::memory_order_relaxed); }

private:
    // 差し替えた古いテンポマップは、読んでいる途中のスレッドがあっても困らないよう、しばらく残してから使い回す
    std::array<std::unique_ptr<collab::TempoMap>, 8> slots;
    size_t nextSlot = 0;
    std::atomic<const collab::TempoMap*> current { nullptr };
    std::atomic<double> loopStart { 0.0 }, loopEnd { 0.0 };
};

/**
    外部プラグイン（VST3 / AU）。Tracktion の ExternalPlugin と同じだが、プラグインが受け取る再生位置の
    テンポ・拍子・PPQ・ループ範囲を曲のテンポマップどおりにする（時刻と再生中かどうかは Tracktion のものを使う）。
*/
class HostSyncedExternalPlugin  : public te::ExternalPlugin
{
public:
    HostSyncedExternalPlugin (te::PluginCreationInfo);
    ~HostSyncedExternalPlugin() override;

    static const char* xmlTypeName;

    /** Edit に挿す前に、メッセージスレッドから渡す。 */
    static juce::ValueTree create (te::Engine&, const juce::PluginDescription&);
    void setHostTempo (std::shared_ptr<const HostTempo>);

    void applyToBuffer (const te::PluginRenderContext&) override;

private:
    class PlayHead;
    std::unique_ptr<PlayHead> playHead;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HostSyncedExternalPlugin)
};
