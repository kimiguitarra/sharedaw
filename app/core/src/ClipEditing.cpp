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

    // ピッチベンドも分ける。右は、分けた位置での値から始める
    left.pitchBends.clear();
    right.pitchBends.clear();

    for (auto b : c.pitchBends)
        if (b.tick < rel)
            left.pitchBends.push_back (b);

    if (const int v = pitchBendAt (c.pitchBends, rel); v != 0 && std::none_of (c.pitchBends.begin(), c.pitchBends.end(), [rel] (auto& b) { return b.tick == rel; }))
        right.pitchBends.push_back ({ 0, v });

    for (auto b : c.pitchBends)
        if (b.tick >= rel)
            right.pitchBends.push_back ({ b.tick - rel, b.value });

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

    // ピッチベンドもずらす。頭より前になったものは、新しい頭での値にまとめる
    const int startValue = pitchBendAt (c.pitchBends, delta);
    std::vector<PitchBend> bends;

    if (startValue != 0)
        bends.push_back ({ 0, startValue });

    for (auto b : c.pitchBends)
        if (b.tick - delta > 0 || (b.tick - delta == 0 && startValue == 0))
            bends.push_back ({ b.tick - delta, b.value });

    r.pitchBends = bends;
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

    // ピッチベンド: それぞれのクリップの範囲ではそのクリップの値（後ろのクリップの頭で値を戻す）
    std::vector<PitchBend> bends;

    for (auto pb : a.pitchBends)
        bends.push_back ({ pb.tick + a.startTick - start, pb.value });

    if (! a.pitchBends.empty() || ! b.pitchBends.empty())
        bends.push_back ({ b.startTick - start, pitchBendAt (b.pitchBends, 0) });

    for (auto pb : b.pitchBends)
        bends.push_back ({ pb.tick + b.startTick - start, pb.value });

    std::stable_sort (bends.begin(), bends.end(), [] (auto& x, auto& y) { return x.tick < y.tick; });
    r.pitchBends = bends;

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

std::vector<AudibleSegment> audibleSegments (const std::vector<AudioClip>& clips, const TempoMap& map, double xf)
{
    constexpr double rate = (double) kSampleRate;
    std::vector<std::pair<double, double>> ranges;

    for (auto& c : clips)
    {
        const double start = map.tickToSeconds ((double) c.startTick);
        ranges.push_back ({ start, start + (double) c.lengthSamples / rate });
    }

    // time の所で、index より前（下）のクリップが鳴っているか
    auto coveredBelow = [&] (size_t index, double time)
    {
        for (size_t k = 0; k < index; ++k)
            if (ranges[k].first < time - 1.0e-9 && time + 1.0e-9 < ranges[k].second)
                return true;

        return false;
    };

    std::vector<AudibleSegment> result;

    for (size_t i = 0; i < clips.size(); ++i)
    {
        // 自分の範囲から、後ろのクリップの範囲を引いていく
        std::vector<std::pair<double, double>> visible { ranges[i] };

        for (size_t j = i + 1; j < clips.size(); ++j)
        {
            std::vector<std::pair<double, double>> next;
            const auto [cs, ce] = ranges[j];

            for (auto [vs, ve] : visible)
            {
                if (ce <= vs || cs >= ve)
                {
                    next.push_back ({ vs, ve });
                    continue;
                }

                if (cs > vs)  next.push_back ({ vs, cs });
                if (ce < ve)  next.push_back ({ ce, ve });
            }

            visible = std::move (next);
        }

        const auto& c = clips[i];
        const double userFadeIn = (double) c.fadeInSamples / rate, userFadeOut = (double) c.fadeOutSamples / rate;

        for (auto [vs, ve] : visible)
        {
            if (ve - vs < 1.0 / rate)
                continue;

            AudibleSegment seg;
            seg.clipIndex = i;
            const bool realStart = vs <= ranges[i].first + 1.0e-9;
            const bool realEnd = ve >= ranges[i].second - 1.0e-9;

            // 上のクリップに隠れて切れた所: 下のクリップを xf だけ延ばして、上のクリップと重ねて入れ替える
            double start = vs, end = ve;

            if (! realStart)
            {
                start = std::max (ranges[i].first, vs - xf);
                seg.fadeInSeconds = vs - start;
                seg.crossfadeIn = true;
            }
            else if (coveredBelow (i, vs))
            {
                // 自分が上: 下のクリップの上に始まるならクロスフェードで入る
                seg.fadeInSeconds = std::max (userFadeIn, xf);
                seg.crossfadeIn = userFadeIn < xf;
            }
            else
            {
                seg.fadeInSeconds = userFadeIn;
            }

            if (! realEnd)
            {
                end = std::min (ranges[i].second, ve + xf);
                seg.fadeOutSeconds = end - ve;
                seg.crossfadeOut = true;
            }
            else if (coveredBelow (i, ve))
            {
                seg.fadeOutSeconds = std::max (userFadeOut, xf);
                seg.crossfadeOut = userFadeOut < xf;
            }
            else
            {
                seg.fadeOutSeconds = userFadeOut;
            }

            seg.startSeconds = start;
            seg.lengthSeconds = end - start;
            seg.offsetSeconds = (double) c.sourceOffsetSamples / rate + (start - ranges[i].first);

            // フェードが長さを超えないように
            const double total = seg.fadeInSeconds + seg.fadeOutSeconds;

            if (total > seg.lengthSeconds && total > 0.0)
            {
                seg.fadeInSeconds *= seg.lengthSeconds / total;
                seg.fadeOutSeconds *= seg.lengthSeconds / total;
            }

            result.push_back (seg);
        }
    }

    return result;
}

int pitchBendAt (const std::vector<PitchBend>& bends, Tick tick)
{
    int value = 0;

    for (auto& b : bends)
    {
        if (b.tick > tick)
            break;

        value = b.value;
    }

    return value;
}

void replacePitchBends (std::vector<PitchBend>& bends, Tick from, Tick to, const std::vector<PitchBend>& points)
{
    if (to < from)
        std::swap (from, to);

    const int after = pitchBendAt (bends, to);
    std::erase_if (bends, [&] (const PitchBend& b) { return b.tick >= from && b.tick <= to; });

    for (auto p : points)
        if (p.tick >= from && p.tick <= to)
            bends.push_back ({ p.tick, std::clamp (p.value, kPitchBendMin, kPitchBendMax) });

    // 描いた範囲の後は、元の値に戻す（描いた最後の値が後ろまで続かないように）
    if (std::none_of (bends.begin(), bends.end(), [to] (auto& b) { return b.tick == to + 1; }))
        bends.push_back ({ to + 1, after });

    std::stable_sort (bends.begin(), bends.end(), [] (auto& a, auto& b) { return a.tick < b.tick; });

    // 前と同じ値のイベントは省く（最初は 0 = 中央から始まる）
    std::vector<PitchBend> compact;
    int current = 0;

    for (auto& b : bends)
        if (b.value != current)
        {
            compact.push_back (b);
            current = b.value;
        }

    bends = compact;
}

} // namespace collab
