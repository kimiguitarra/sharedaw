#pragma once

#include <cstdint>
#include <vector>

#include "Project.h"
#include "TempoMap.h"

namespace collab
{

/**
    MIDI ファイル（SMF タイプ 1、960 PPQ）に書き出す。
    - 1 本目: テンポ・拍子・キー・マーカー
    - MIDI トラックごとに 1 本（クリップをまとめる。クリップの外にはみ出したノートは鳴らないので入れない・クリップの端で切る）
      ドラム（内蔵ドラム）はチャンネル 10、それ以外は 1 から順（10 を飛ばす）。内蔵音源には GM のプログラムチェンジを付ける
    - コードがあれば「Chords」（コードトラックで鳴らしている音）
*/
std::vector<std::uint8_t> writeMidiFile (const Project&, const TempoMap&, bool includeChordTrack = true);

} // namespace collab
