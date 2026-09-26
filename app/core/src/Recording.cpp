#include "collab/Recording.h"

#include <algorithm>
#include <cmath>

namespace collab
{

namespace
{
    struct Beat { double seconds; int perBar; };

    Beat beatAt (const TempoMap& map, double startSeconds)
    {
        const auto tick = (Tick) std::llround (map.secondsToTick (std::max (0.0, startSeconds)));
        const auto sig = map.timeSignatureAtTick (tick);
        return { 60.0 / map.bpmAtTick (tick) * 4.0 / sig.denominator, sig.numerator };
    }
}

std::vector<Click> countInClicks (const TempoMap& map, double startSeconds, int bars)
{
    std::vector<Click> clicks;

    if (bars <= 0)
        return clicks;

    const auto beat = beatAt (map, startSeconds);
    const int total = bars * beat.perBar;

    for (int i = 0; i < total; ++i)
        clicks.push_back ({ startSeconds - (total - i) * beat.seconds, i % beat.perBar == 0 });

    return clicks;
}

double countInSeconds (const TempoMap& map, double startSeconds, int bars)
{
    if (bars <= 0)
        return 0.0;

    const auto beat = beatAt (map, startSeconds);
    return bars * beat.perBar * beat.seconds;
}

AudioClip makeRecordedClip (const TempoMap& map, std::string id, std::string audioHash, std::string displayName,
                            double startSeconds, double offsetSeconds, double lengthSeconds, SampleCount fileLengthSamples,
                            double punchInSeconds)
{
    AudioClip c;
    c.id = std::move (id);
    c.audioHash = std::move (audioHash);
    c.displayName = std::move (displayName);

    // カウントイン中・曲の先頭より前に録れた部分は使わない
    const double from = std::max (0.0, punchInSeconds);

    if (startSeconds < from)
    {
        offsetSeconds += from - startSeconds;
        lengthSeconds -= from - startSeconds;
        startSeconds = from;
    }

    c.startTick = (Tick) std::llround (map.secondsToTick (startSeconds));
    c.sourceOffsetSamples = std::clamp<SampleCount> ((SampleCount) std::llround (offsetSeconds * kSampleRate), 0, fileLengthSamples);
    c.lengthSamples = std::clamp<SampleCount> ((SampleCount) std::llround (lengthSeconds * kSampleRate), 0,
                                               fileLengthSamples - c.sourceOffsetSamples);
    return c;
}

} // namespace collab
