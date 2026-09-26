#include "collab/ClipEditing.h"

#include <algorithm>
#include <cmath>

namespace collab
{

namespace
{
    constexpr SampleCount minLength = kSampleRate / 100;   // 10ms
}

Tick audioClipEndTick (const AudioClip& c, const TempoMap& map)
{
    const double end = map.tickToSeconds ((double) c.startTick) + (double) c.lengthSamples / kSampleRate;
    return (Tick) std::llround (map.secondsToTick (end));
}

SampleCount samplesBetween (Tick from, Tick to, const TempoMap& map)
{
    return (SampleCount) std::llround ((map.tickToSeconds ((double) to) - map.tickToSeconds ((double) from)) * kSampleRate);
}

std::optional<std::pair<AudioClip, AudioClip>> splitAudioClip (const AudioClip& c, Tick at, const TempoMap& map, const std::string& newId)
{
    const auto leftLength = samplesBetween (c.startTick, at, map);

    if (at <= c.startTick || leftLength < minLength || c.lengthSamples - leftLength < minLength)
        return std::nullopt;

    AudioClip left = c, right = c;
    left.lengthSamples = leftLength;
    left.fadeOutSamples = 0;
    left.fadeInSamples = std::min (left.fadeInSamples, leftLength);

    right.id = newId;
    right.startTick = at;
    right.sourceOffsetSamples = c.sourceOffsetSamples + leftLength;
    right.lengthSamples = c.lengthSamples - leftLength;
    right.fadeInSamples = 0;
    right.fadeOutSamples = std::min (right.fadeOutSamples, right.lengthSamples);

    return std::make_pair (left, right);
}

std::optional<std::pair<MidiClip, MidiClip>> splitMidiClip (const MidiClip& c, Tick at, const std::string& newId,
                                                            const std::function<std::string()>& makeNoteId)
{
    const Tick rel = at - c.startTick;

    if (rel <= 0 || rel >= c.lengthTick)
        return std::nullopt;

    MidiClip left = c, right = c;
    left.lengthTick = rel;
    left.notes.clear();

    right.id = newId;
    right.startTick = at;
    right.lengthTick = c.lengthTick - rel;
    right.notes.clear();

    for (auto n : c.notes)
    {
        if (n.tick < rel)
        {
            n.lengthTick = std::min (n.lengthTick, rel - n.tick);
            left.notes.push_back (n);
        }
        else
        {
            n.tick -= rel;
            n.id = makeNoteId();
            right.notes.push_back (n);
        }
    }

    return std::make_pair (left, right);
}

AudioClip trimAudioClipStart (const AudioClip& c, Tick newStart, const TempoMap& map)
{
    const Tick end = audioClipEndTick (c, map);
    newStart = std::clamp<Tick> (newStart, 0, end);

    auto delta = samplesBetween (c.startTick, newStart, map);
    delta = std::max (delta, -c.sourceOffsetSamples);           // 実体の頭より前には伸ばせない
    delta = std::min (delta, c.lengthSamples - minLength);

    AudioClip r = c;
    r.sourceOffsetSamples += delta;
    r.lengthSamples -= delta;
    r.startTick = (Tick) std::llround (map.secondsToTick (map.tickToSeconds ((double) c.startTick) + (double) delta / kSampleRate));
    r.fadeInSamples = std::min (r.fadeInSamples, r.lengthSamples);
    r.fadeOutSamples = std::min (r.fadeOutSamples, r.lengthSamples);
    return r;
}

AudioClip trimAudioClipEnd (const AudioClip& c, Tick newEnd, SampleCount sourceLength, const TempoMap& map)
{
    AudioClip r = c;
    auto length = samplesBetween (c.startTick, newEnd, map);
    length = std::max (length, minLength);

    if (sourceLength > 0)
        length = std::min (length, sourceLength - c.sourceOffsetSamples);

    r.lengthSamples = std::max<SampleCount> (1, length);
    r.fadeInSamples = std::min (r.fadeInSamples, r.lengthSamples);
    r.fadeOutSamples = std::min (r.fadeOutSamples, r.lengthSamples);
    return r;
}

} // namespace collab
