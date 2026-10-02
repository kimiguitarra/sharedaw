#pragma once

#include "Common.h"

/** MP3 の書き出し（LAME）。 */
namespace Mp3Export
{
    /**
        WAV（など JUCE で読めるファイル）を MP3 にする。ステレオ・固定ビットレート（kbps）・最高品質寄りの設定。
        title / artist があれば ID3v2 タグに入れる。先頭に LAME タグ（曲の長さ・ギャップレス情報）を書く。
    */
    juce::Result encode (const juce::File& source, const juce::File& mp3, int kbps = 320,
                         const juce::String& title = {}, const juce::String& artist = {});
}
