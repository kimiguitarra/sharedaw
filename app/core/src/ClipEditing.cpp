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

    // 直線の途中で切るときは、左の終わり・右の頭に、その位置の値の点を置く（形を変えない）
    const bool later = std::any_of (c.pitchBends.begin(), c.pitchBends.end(), [rel] (auto& b) { return b.tick >= rel; });

    if (later && ! left.pitchBends.empty() && left.pitchBends.back().tick < rel - 1)
        left.pitchBends.push_back ({ rel - 1, pitchBendAt (c.pitchBends, rel - 1) });

    if (const int v = pitchBendAt (c.pitchBends, rel); (v != 0 || (later && ! left.pitchBends.empty()))
         && std::none_of (c.pitchBends.begin(), c.pitchBends.end(), [rel] (auto& b) { return b.tick == rel; }))
        right.pitchBends.push_back ({ 0, v });

    for (auto b : c.pitchBends)
        if (b.tick >= rel)
            right.pitchBends.push_back ({ b.tick - rel, b.value, b.curve });

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

    const bool crossing = std::any_of (c.pitchBends.begin(), c.pitchBends.end(), [delta] (auto& b) { return b.tick < delta; })
                          && std::any_of (c.pitchBends.begin(), c.pitchBends.end(), [delta] (auto& b) { return b.tick > delta; });

    if (startValue != 0 || crossing)   // 直線の途中から始まるときも、その値の点を頭に置く
        bends.push_back ({ 0, startValue });

    for (auto b : c.pitchBends)
        if (b.tick - delta > 0 || (b.tick - delta == 0 && startValue == 0))
            bends.push_back ({ b.tick - delta, b.value, b.curve });

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

    // a の最後の値は b の頭まで続き、b の頭で b の値になる（点の間は直線なので、b の頭の手前にも点を置く）
    if (! a.pitchBends.empty() && b.startTick - start - 1 > a.pitchBends.back().tick + a.startTick - start)
        bends.push_back ({ b.startTick - start - 1, a.pitchBends.back().value });

    if (! a.pitchBends.empty() || ! b.pitchBends.empty())
        bends.push_back ({ b.startTick - start, pitchBendAt (b.pitchBends, 0) });

    for (auto pb : b.pitchBends)
        bends.push_back ({ pb.tick + b.startTick - start, pb.value, pb.curve });

    std::stable_sort (bends.begin(), bends.end(), [] (auto& x, auto& y) { return x.tick < y.tick; });
    r.pitchBends = bends;

    r.startTick = start;
    r.lengthTick = end - start;
    return r;
}

std::string addRecordedMidi (std::vector<MidiClip>& clips, MidiClip recorded, bool mergeIntoExisting)
{
    const auto bends = recorded.pitchBends;
    recorded.pitchBends.clear();

    MidiClip* target = nullptr;

    if (mergeIntoExisting)
        for (auto& c : clips)
            if (c.startTick <= recorded.endTick() && c.endTick() >= recorded.startTick
                 && (target == nullptr || c.startTick < target->startTick))
                target = &c;

    if (target == nullptr)
    {
        if (! bends.empty())
            replacePitchBends (recorded.pitchBends, 0, recorded.lengthTick - 1, bends);

        clips.push_back (recorded);
        return recorded.id;
    }

    // 録音した範囲まで伸ばす（前へ伸ばすときは、元のノートの位置（クリップ先頭から）をずらす）
    const Tick start = std::min (target->startTick, recorded.startTick);
    const Tick end = std::max (target->endTick(), recorded.endTick());
    const Tick shift = target->startTick - start, recordedShift = recorded.startTick - start;

    for (auto& n : target->notes)
        n.tick += shift;

    for (auto& b : target->pitchBends)
        b.tick += shift;

    target->startTick = start;
    target->lengthTick = end - start;

    for (auto n : recorded.notes)
    {
        n.tick += recordedShift;
        target->notes.push_back (n);
    }

    std::stable_sort (target->notes.begin(), target->notes.end(), [] (auto& a, auto& b) { return a.tick < b.tick; });

    // ピッチベンドは、録ったイベントのある範囲だけ置き換える（その後は元の値に戻る）
    if (! bends.empty())
    {
        std::vector<PitchBend> moved;

        for (auto b : bends)
            moved.push_back ({ b.tick + recordedShift, b.value });

        replacePitchBends (target->pitchBends, moved.front().tick, moved.back().tick, moved);
    }

    return target->id;
}

std::optional<AudioClip> glueAudioClips (const AudioClip& a, const AudioClip& b, const TempoMap& map)
{
    const auto& first = a.startTick <= b.startTick ? a : b;
    const auto& second = a.startTick <= b.startTick ? b : a;

    // 同じ実体で、元ファイル上もタイムライン上も続いていること（分割したものを元に戻す）
    if (first.audioHash != second.audioHash || first.pitchSemitones != second.pitchSemitones
        || first.sourceOffsetSamples + first.lengthSamples != second.sourceOffsetSamples
        || std::llabs (audioClipEndTick (first, map) - second.startTick) > 2)
        return std::nullopt;

    AudioClip r = first;
    r.lengthSamples = first.lengthSamples + second.lengthSamples;
    r.fadeOutSamples = second.fadeOutSamples;
    return r;
}

std::vector<AudibleSegment> audibleSegments (const std::vector<AudioClip>& clips, const TempoMap& map, double xf,
                                            const std::function<double (const AudioClip&)>& sourceSeconds)
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

    // ぴったりくっついている（間が joinToleranceSeconds 以内の）別のクリップ
    auto joinedAt = [&] (size_t index, double time, bool atEnd) -> std::optional<size_t>
    {
        for (size_t k = 0; k < clips.size(); ++k)
            if (k != index && std::abs ((atEnd ? ranges[k].first : ranges[k].second) - time) <= joinToleranceSeconds
                 && ranges[k].second - ranges[k].first > 2.0 * joinToleranceSeconds)
                return k;

        return std::nullopt;
    };

    // 元ファイルの、クリップの後ろ・前の余り（秒）
    auto spareAfter = [&] (size_t i)
    {
        const double total = sourceSeconds ? sourceSeconds (clips[i]) : -1.0;
        const double used = (double) (clips[i].sourceOffsetSamples + clips[i].lengthSamples) / rate;
        return total < 0.0 ? 1.0e9 : std::max (0.0, total - used);
    };

    auto spareBefore = [&] (size_t i) { return (double) clips[i].sourceOffsetSamples / rate; };

    // くっついたつなぎ目: 前のクリップを後ろへ fwd、後ろのクリップを前へ back だけ延ばし、足りない分は後ろのクリップを前へずらす。
    // ずらしたクリップに続くクリップも同じだけずらす（つなぎ目をそろえたまま）
    std::vector<double> extendStart (clips.size(), 0.0), extendEnd (clips.size(), 0.0), shift (clips.size(), 0.0);
    std::vector<bool> joinStart (clips.size(), false), joinEnd (clips.size(), false);
    std::vector<size_t> byStart (clips.size());

    for (size_t i = 0; i < clips.size(); ++i)
        byStart[i] = i;

    std::stable_sort (byStart.begin(), byStart.end(), [&] (size_t x, size_t y) { return ranges[x].first < ranges[y].first; });

    for (auto b : byStart)
    {
        const auto a = joinedAt (b, ranges[b].first, false);

        if (! a || xf <= 0.0
             || (double) clips[b].fadeInSamples / rate >= xf || (double) clips[*a].fadeOutSamples / rate >= xf
             || coveredBelow (b, ranges[b].first))
            continue;

        double fwd = std::min (spareAfter (*a), xf * 0.5), back = std::min (spareBefore (b), xf * 0.5);

        if (back < xf * 0.5) fwd = std::min (spareAfter (*a), xf - back);
        if (fwd < xf * 0.5)  back = std::min (spareBefore (b), xf - fwd);

        extendEnd[*a] = fwd;
        extendStart[b] = back;
        shift[b] = shift[*a] + std::max (0.0, xf - fwd - back);
        joinEnd[*a] = joinStart[b] = true;
    }

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
            else if (joinStart[i])
            {
                // くっついた前のクリップと xf だけ重ねてクロスフェード
                start = vs - extendStart[i];
                seg.fadeInSeconds = xf;
                seg.crossfadeIn = true;
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
            else if (joinEnd[i])
            {
                // くっついた後ろのクリップと xf だけ重ねる
                end = ve + extendEnd[i];
                seg.fadeOutSeconds = xf;
                seg.crossfadeOut = true;
            }
            else
            {
                seg.fadeOutSeconds = userFadeOut;
            }

            seg.startSeconds = start - shift[i];
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

double pitchBendCurve (double t, double curve)
{
    t = std::clamp (t, 0.0, 1.0);
    return curve == 0.0 ? t : std::pow (t, std::pow (6.0, std::clamp (curve, -1.0, 1.0)));
}

double pitchBendCurveFor (double fraction)
{
    // 真ん中（t = 0.5）でその割合になるカーブ: 0.5 ^ 6^c = fraction
    fraction = std::clamp (fraction, 0.02, 0.98);
    return std::clamp (std::log (std::log (fraction) / std::log (0.5)) / std::log (6.0), -1.0, 1.0);
}

int pitchBendAt (const std::vector<PitchBend>& bends, Tick tick)
{
    if (bends.empty() || tick < bends.front().tick)
        return 0;

    for (size_t i = 0; i + 1 < bends.size(); ++i)
    {
        const auto& a = bends[i];
        const auto& b = bends[i + 1];

        if (tick >= a.tick && tick < b.tick)
        {
            if (b.tick == a.tick)
                return b.value;

            const double t = pitchBendCurve ((double) (tick - a.tick) / (double) (b.tick - a.tick), a.curve);
            return (int) std::lround (a.value + (b.value - a.value) * t);
        }
    }

    return bends.back().value;
}

std::vector<PitchBend> densePitchBends (const std::vector<PitchBend>& bends, Tick stepTicks)
{
    std::vector<PitchBend> result;
    stepTicks = std::max<Tick> (1, stepTicks);

    for (size_t i = 0; i < bends.size(); ++i)
    {
        result.push_back (bends[i]);

        if (i + 1 < bends.size() && bends[i + 1].value != bends[i].value)
            for (Tick t = bends[i].tick + stepTicks; t < bends[i + 1].tick; t += stepTicks)
                result.push_back ({ t, pitchBendAt (bends, t) });
    }

    return result;
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

    // 同じ tick は後のものだけ残す
    std::vector<PitchBend> unique;

    for (auto& b : bends)
    {
        if (! unique.empty() && unique.back().tick == b.tick)
            unique.back() = b;
        else
            unique.push_back (b);
    }

    // 形が変わらない点は省く（点の間は直線なので、前と後ろが同じ値のときだけ。最初の点より前は 0 = 中央）
    std::vector<PitchBend> compact;

    for (size_t i = 0; i < unique.size(); ++i)
    {
        const int before = compact.empty() ? 0 : compact.back().value;
        const int after = i + 1 < unique.size() ? unique[i + 1].value : unique[i].value;

        if (! (unique[i].value == before && unique[i].value == after))
            compact.push_back (unique[i]);
    }

    bends = compact;
}

} // namespace collab
