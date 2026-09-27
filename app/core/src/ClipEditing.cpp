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

MidiClip trimMidiClipStart (const MidiClip& c, Tick newStart, Tick minLength)
{
    newStart = std::clamp<Tick> (newStart, 0, c.endTick() - std::max<Tick> (1, minLength));
    const Tick delta = newStart - c.startTick;

    MidiClip r = c;
    r.startTick = newStart;
    r.lengthTick = c.lengthTick - delta;

    for (auto& n : r.notes)
        n.tick -= delta;

    return r;
}

MidiClip glueMidiClips (const MidiClip& a, const MidiClip& b)
{
    MidiClip r = a;
    const Tick start = std::min (a.startTick, b.startTick);
    const Tick end = std::max (a.endTick(), b.endTick());

    for (auto& n : r.notes)
        n.tick += a.startTick - start;

    for (auto n : b.notes)
    {
        n.tick += b.startTick - start;
        r.notes.push_back (n);
    }

    r.startTick = start;
    r.lengthTick = end - start;
    return r;
}

std::optional<AudioClip> glueAudioClips (const AudioClip& a, const AudioClip& b, const TempoMap& map)
{
    const auto& first = a.startTick <= b.startTick ? a : b;
    const auto& second = a.startTick <= b.startTick ? b : a;

    // 同じ実体で、元ファイル上もタイムライン上も続いていること（分割したものを元に戻す）
    if (first.audioHash != second.audioHash
        || first.sourceOffsetSamples + first.lengthSamples != second.sourceOffsetSamples
        || std::llabs (audioClipEndTick (first, map) - second.startTick) > 2)
        return std::nullopt;

    AudioClip r = first;
    r.lengthSamples = first.lengthSamples + second.lengthSamples;
    r.fadeOutSamples = second.fadeOutSamples;
    return r;
}

} // namespace collab
