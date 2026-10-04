#pragma once

#include <map>

#include "Common.h"

/**
    オーディオ実体（§7.3）: 録音・読み込み・バウンスの結果を 48kHz / 32bit float WAV に変換し、
    SHA-256 を計算して audio フォルダに置く。実体は不変で、どの実体かはハッシュで決まる（同期もハッシュで行う）。
    ファイル名は「<トラック名>_take03_<ハッシュの頭 8 文字>.wav」のように、何の音か分かる名前にする
    （前のバージョンで作った「<ハッシュ>.wav」もそのまま使える）。
*/
namespace AudioFiles
{
    struct Imported
    {
        std::string hash;
        juce::int64 lengthSamples = 0;
        juce::String displayName;
        int numChannels = 2;   // 1 = モノ、2 = ステレオ（読み込んだトラックの入出力を合わせる）
    };

    /** 読み込めるファイルの拡張子（ファイル選択用）。 */
    juce::String supportedWildcard();

    /**
        audioDir に変換して保存する。すでに同じ内容があれば再利用する。
        サンプルレートが 48kHz でなければリサンプリングする。
    */
    juce::Result importFile (const juce::File& source, const juce::File& audioDir, Imported& result, const juce::String& label = {});

    /** ファイルの SHA-256（16進）。 */
    std::string hashFile (const juce::File&);

    /** 実体のファイル（読みやすい名前のものも探す）。なければ「<ハッシュ>.wav」のパス。 */
    juce::File fileForHash (const juce::File& projectDir, const std::string& hash);

    /** 新しく置くときのファイル（「<label>_<ハッシュの頭 8 文字>.wav」）。同じ実体がもうあれば、そのファイル。 */
    juce::File newFileForHash (const juce::File& projectDir, const std::string& hash, const juce::String& label);

    /** ファイル名から分かるハッシュの頭（読みやすい名前なら 8 文字、「<ハッシュ>.wav」なら全部）。分からなければ空。 */
    std::string hashKeyOf (const juce::File&);

    /** 録音のテイクの名前（「<トラック名>_take03」。audio フォルダにある同じトラックのテイクの数 + 1）。 */
    juce::String nextTakeLabel (const juce::File& projectDir, const juce::String& trackName);

    /**
        波形を描く（一般的な DAW と同じく、チャンネルごとに中心線から上下に塗りつぶした形。不透明）。
        start〜end は元ファイルの秒。拡大しても点々にならないよう、ピクセルごとの最大・最小を線でつないで塗る。
    */
    void drawWaveform (juce::Graphics&, juce::AudioThumbnail&, juce::Rectangle<float> area,
                       double startSeconds, double endSeconds, float gain, juce::Colour);

    /** 音の立ち上がりの位置に縦線を引く（area の左端 = startSeconds、右端 = endSeconds。transients が nullptr なら何もしない）。 */
    void drawTransients (juce::Graphics&, const std::vector<double>* transients, juce::Rectangle<float> area,
                         double startSeconds, double endSeconds);
}

/** 波形表示と長さのキャッシュ（ハッシュ単位）。メッセージスレッドで使う。 */
class AudioFileCache  : private juce::ChangeListener
{
public:
    AudioFileCache();
    ~AudioFileCache() override;

    /** 波形。まだ読み込み中なら途中まで描ける。 */
    juce::AudioThumbnail* getThumbnail (const juce::File& projectDir, const std::string& hash);

    /**
        音の立ち上がり（アタック）の位置（ファイルの先頭からの秒）。波形の上に縦線で出して、グリッドに合わせる目安にする。
        波形を読み込み終わるまでは nullptr。
        既定は Cubase のヒットポイント・Pro Tools の解析マーカーと同じく、音の鳴り始め（アタックの頭）。
        transientsAtPeak なら、その音がいちばん大きくなった所（聴いた感じの「拍」に近い）。
    */
    const std::vector<double>* getTransients (const juce::File& projectDir, const std::string& hash);

    bool transientsAtPeak = false;

    /** 実体の長さ（サンプル）。ファイルがなければ 0。 */
    juce::int64 getLengthSamples (const juce::File& projectDir, const std::string& hash);

    std::function<void()> onThumbnailChanged;

private:
    juce::AudioFormatManager formats;
    juce::AudioThumbnailCache thumbnailCache { 64 };
    std::map<std::string, std::unique_ptr<juce::AudioThumbnail>> thumbnails;
    std::map<std::string, juce::int64> lengths;
    struct Hits { std::vector<double> onsets, peaks; };
    std::map<std::string, Hits> transients;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
};
