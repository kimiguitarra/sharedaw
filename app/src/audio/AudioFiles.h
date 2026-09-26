#pragma once

#include <map>

#include "Common.h"

/**
    オーディオ実体（§7.3）: 録音・読み込み・バウンスの結果を 48kHz / 32bit float WAV に変換し、
    SHA-256 を計算して audio/<sha256>.wav に置く。実体は不変で、ファイル名はハッシュのみ。
*/
namespace AudioFiles
{
    struct Imported
    {
        std::string hash;
        juce::int64 lengthSamples = 0;
        juce::String displayName;
    };

    /** 読み込めるファイルの拡張子（ファイル選択用）。 */
    juce::String supportedWildcard();

    /**
        audioDir に変換して保存する。すでに同じ内容があれば再利用する。
        サンプルレートが 48kHz でなければリサンプリングする。
    */
    juce::Result importFile (const juce::File& source, const juce::File& audioDir, Imported& result);

    /** ファイルの SHA-256（16進）。 */
    std::string hashFile (const juce::File&);

    juce::File fileForHash (const juce::File& projectDir, const std::string& hash);
}

/** 波形表示と長さのキャッシュ（ハッシュ単位）。メッセージスレッドで使う。 */
class AudioFileCache  : private juce::ChangeListener
{
public:
    AudioFileCache();
    ~AudioFileCache() override;

    /** 波形。まだ読み込み中なら途中まで描ける。 */
    juce::AudioThumbnail* getThumbnail (const juce::File& projectDir, const std::string& hash);

    /** 実体の長さ（サンプル）。ファイルがなければ 0。 */
    juce::int64 getLengthSamples (const juce::File& projectDir, const std::string& hash);

    std::function<void()> onThumbnailChanged;

private:
    juce::AudioFormatManager formats;
    juce::AudioThumbnailCache thumbnailCache { 64 };
    std::map<std::string, std::unique_ptr<juce::AudioThumbnail>> thumbnails;
    std::map<std::string, juce::int64> lengths;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
};
