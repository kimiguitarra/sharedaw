#pragma once

#include <optional>

#include "Common.h"
#include "collab/Project.h"

/**
    MIDI ファイル（SMF）の読み込み。トラック × チャンネルごとに 1 つのパートにまとめる。
    時刻はファイルの先頭からの tick（960 PPQ に換算）。先頭の空白は削らない（パート同士の位置関係を保つ）。
*/
namespace MidiImport
{
    struct Part
    {
        std::string name;
        int channel = 1;         // 1〜16（10 はドラム）
        int program = -1;        // 最初のプログラムチェンジ（なければ -1）
        std::vector<collab::Note> notes;
        std::vector<collab::PitchBend> pitchBends;   // ファイルの先頭からの tick
        collab::Tick endTick = 0;

        /** パートに合う内蔵音源の ID（ドラム / ベース / エレピ / ピアノ）。 */
        std::string builtinInstrument() const;
    };

    struct Result
    {
        std::vector<Part> parts;
        std::optional<double> bpm;                        // 最初のテンポ
        std::optional<std::pair<int, int>> meter;         // 最初の拍子
        juce::String error;

        bool ok() const noexcept          { return error.isEmpty(); }
        collab::Tick endTick() const;
    };

    bool isMidiFile (const juce::File&);
    Result read (const juce::File&);
}
