#include "HostSyncedPlugin.h"

void HostTempo::setTempoMap (const collab::TempoMap& map)
{
    auto& slot = slots[nextSlot];
    nextSlot = (nextSlot + 1) % slots.size();

    slot = std::make_unique<collab::TempoMap> (map);
    current.store (slot.get(), std::memory_order_release);
}

void HostTempo::setLoopRange (double startSeconds, double endSeconds) noexcept
{
    loopStart.store (startSeconds, std::memory_order_relaxed);
    loopEnd.store (endSeconds, std::memory_order_relaxed);
}

//==============================================================================
/**
    Tracktion の再生位置（inner）をもとに、テンポ・拍子・PPQ を曲のテンポマップの値にして返す。
    inner は Tracktion がプラグインの準備のたびに作り直すので、applyToBuffer で毎回確かめて差し替える。
*/
class HostSyncedExternalPlugin::PlayHead  : public juce::AudioPlayHead
{
public:
    std::atomic<juce::AudioPlayHead*> inner { nullptr };
    std::shared_ptr<const HostTempo> tempo;

    juce::Optional<PositionInfo> getPosition() const override
    {
        auto* in = inner.load();

        if (in == nullptr)
            return {};

        auto pos = in->getPosition();
        auto* map = tempo != nullptr ? tempo->getTempoMap() : nullptr;

        if (! pos.hasValue() || map == nullptr || ! pos->getTimeInSeconds().hasValue())
            return pos;

        const double tick = map->secondsToTick (*pos->getTimeInSeconds());
        const auto at = (collab::Tick) std::max (0.0, std::floor (tick));
        const auto sig = map->timeSignatureAtTick (at);

        // 小節の頭（曲の頭より前のカウントインは、最初の拍子で数える）
        double barStart = 0.0;
        int barIndex = 0;

        if (tick >= 0.0)
        {
            const int bar = map->tickToBar (at);
            barStart = (double) map->barToTick (bar);
            barIndex = bar - 1;
        }
        else
        {
            const double perBar = (double) sig.ticksPerBar();
            barIndex = (int) std::floor (tick / perBar);
            barStart = barIndex * perBar;
        }

        pos->setBpm (map->bpmAtTick (at));
        pos->setTimeSignature (TimeSignature { sig.numerator, sig.denominator });
        pos->setPpqPosition (tick / collab::kPpq);
        pos->setPpqPositionOfLastBarStart (barStart / collab::kPpq);
        pos->setBarCount (barIndex);

        if (pos->getIsLooping())
            pos->setLoopPoints (LoopPoints { map->secondsToTick (tempo->getLoopStart()) / collab::kPpq,
                                             map->secondsToTick (tempo->getLoopEnd()) / collab::kPpq });

        return pos;
    }
};

//==============================================================================
const char* HostSyncedExternalPlugin::xmlTypeName = "shareDawExternal";

HostSyncedExternalPlugin::HostSyncedExternalPlugin (te::PluginCreationInfo info)
    : te::ExternalPlugin (info), playHead (std::make_unique<PlayHead>())
{
}

HostSyncedExternalPlugin::~HostSyncedExternalPlugin()
{
    // この後 ExternalPlugin がプラグインを片付けるまで、消えた再生位置を指したままにしない
    if (auto* pi = getAudioPluginInstance(); pi != nullptr && pi->getPlayHead() == playHead.get())
        pi->setPlayHead (playHead->inner.load());
}

juce::ValueTree HostSyncedExternalPlugin::create (te::Engine& engine, const juce::PluginDescription& desc)
{
    auto v = te::ExternalPlugin::create (engine, desc);
    v.setProperty (te::IDs::type, xmlTypeName, nullptr);
    return v;
}

void HostSyncedExternalPlugin::setHostTempo (std::shared_ptr<const HostTempo> t)
{
    playHead->tempo = std::move (t);
}

void HostSyncedExternalPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    // Tracktion は準備（initialise）のたびに自分の再生位置を設定し直すので、ここで包み直す。
    // ポインタを入れ替えるだけなので、音の処理のスレッドでもよい
    if (auto* pi = getAudioPluginInstance())
    {
        if (auto* current = pi->getPlayHead(); current != nullptr && current != playHead.get())
        {
            playHead->inner = current;
            pi->setPlayHead (playHead.get());
        }
    }

    if (fc.bufferForMidiMessages != nullptr)
        liveGate.filter (*fc.bufferForMidiMessages);   // キーボードの音は、選択中のトラックだけ

    te::ExternalPlugin::applyToBuffer (fc);
}
