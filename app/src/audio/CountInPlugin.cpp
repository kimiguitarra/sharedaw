#include "CountInPlugin.h"

const char* CountInPlugin::xmlTypeName = "collabCountIn";

namespace
{
    constexpr double kClickSeconds = 0.05;
}

CountInPlugin::CountInPlugin (te::PluginCreationInfo info)
    : te::Plugin (info)
{
}

CountInPlugin::~CountInPlugin()
{
    notifyListenersOfDeletion();
}

void CountInPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    sampleRate = info.sampleRate;
}

void CountInPlugin::setClicks (std::vector<collab::Click> newClicks, float volumeDb)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    clicks = std::move (newClicks);
    gain = juce::Decibels::decibelsToGain (volumeDb);
    remaining = 0;
}

void CountInPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr || ! fc.isPlaying)
        return;

    const juce::SpinLock::ScopedTryLockType sl (lock);

    if (! sl.isLocked() || (clicks.empty() && remaining <= 0))
        return;

    auto& dest = *fc.destBuffer;
    const int start = fc.bufferStartSample;
    const int numSamples = fc.bufferNumSamples;
    const double blockStart = fc.editTime.getStart().inSeconds();
    const double blockEnd = fc.editTime.getEnd().inSeconds();
    const double secondsPerSample = numSamples > 0 ? (blockEnd - blockStart) / numSamples : 0.0;

    // このブロックで始まるクリック（ブロックに 2 つ以上入ることはない）
    int clickStart = -1;
    double clickFrequency = frequency;

    for (auto& c : clicks)
    {
        if (c.seconds >= blockStart && c.seconds < blockEnd && secondsPerSample > 0.0)
        {
            clickStart = juce::jlimit (0, numSamples - 1, (int) ((c.seconds - blockStart) / secondsPerSample));
            clickFrequency = c.accent ? 1600.0 : 1000.0;
            break;
        }
    }

    for (int i = 0; i < numSamples; ++i)
    {
        if (i == clickStart)
        {
            remaining = (int) (kClickSeconds * sampleRate);
            elapsed = 0;
            frequency = clickFrequency;
        }

        if (remaining <= 0)
            continue;

        const double t = elapsed / sampleRate;
        const auto v = (float) (gain * std::sin (juce::MathConstants<double>::twoPi * frequency * t) * std::exp (-t * 60.0));

        for (int ch = 0; ch < dest.getNumChannels(); ++ch)
            dest.addSample (ch, start + i, v);

        ++elapsed;
        --remaining;
    }
}
