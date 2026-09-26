#pragma once

// クリップの非破壊編集（§3.6）。元ファイルは書き換えず、クリップのパラメータだけを変える。

#include <functional>
#include <optional>
#include <utility>

#include "Project.h"
#include "TempoMap.h"

namespace collab
{

/** オーディオクリップの終わりの位置（テンポに追従しないので、秒 → tick で求める）。 */
Tick audioClipEndTick (const AudioClip&, const TempoMap&);

/** クリップの開始位置 startTick からの秒数をサンプル数にしたもの。 */
SampleCount samplesBetween (Tick from, Tick to, const TempoMap&);

/**
    tick の位置で分割する。位置がクリップの内側でなければ std::nullopt。
    右側のクリップには newId が付く。フェードは外側だけ残す。
*/
std::optional<std::pair<AudioClip, AudioClip>> splitAudioClip (const AudioClip&, Tick at, const TempoMap&, const std::string& newId);

/** MIDI クリップを分割する。分割位置より後に始まるノートは右側へ移り、左側のノートは分割位置で切る。 */
std::optional<std::pair<MidiClip, MidiClip>> splitMidiClip (const MidiClip&, Tick at, const std::string& newId,
                                                            const std::function<std::string()>& makeNoteId);

/**
    開始位置を newStart に動かす（終わりの位置は変えない）。元ファイルの範囲を超えないよう制限する。
    sourceLength は実体の長さ（サンプル）。
*/
AudioClip trimAudioClipStart (const AudioClip&, Tick newStart, const TempoMap&);

/** 長さを変える（開始位置は変えない）。newEnd は tick、sourceLength を超えない。 */
AudioClip trimAudioClipEnd (const AudioClip&, Tick newEnd, SampleCount sourceLength, const TempoMap&);

} // namespace collab
