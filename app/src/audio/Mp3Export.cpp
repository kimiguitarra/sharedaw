#include "audio/Mp3Export.h"

#include <lame.h>

namespace Mp3Export
{

juce::Result encode (const juce::File& source, const juce::File& mp3, int kbps, const juce::String& title, const juce::String& artist)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (source));

    if (reader == nullptr)
        return juce::Result::fail ("書き出した音を読めません"_ju);

    const int sampleRate = (int) reader->sampleRate;

    if (sampleRate != 44100 && sampleRate != 48000 && sampleRate != 32000)
        return juce::Result::fail ("MP3 にできないサンプルレートです"_ju);

    // LAME の設定（後片付けは必ずする）
    std::unique_ptr<lame_global_flags, decltype (&lame_close)> lame (lame_init(), &lame_close);

    if (lame == nullptr)
        return juce::Result::fail ("MP3 エンコーダーを初期化できません"_ju);

    auto* gf = lame.get();
    lame_set_in_samplerate (gf, sampleRate);
    lame_set_out_samplerate (gf, sampleRate);
    lame_set_num_channels (gf, 2);
    lame_set_mode (gf, JOINT_STEREO);
    lame_set_brate (gf, kbps);
    lame_set_VBR (gf, vbr_off);
    lame_set_quality (gf, 2);               // 2 = 高品質（0 は遅いだけで差がほぼない）
    lame_set_bWriteVbrTag (gf, 1);          // 先頭の LAME タグ（長さ・前後の無音の情報）
    lame_set_write_id3tag_automatic (gf, 0);   // タグは自分で書く（LAME タグの位置を知るため）

    id3tag_init (gf);
    id3tag_add_v2 (gf);
    id3tag_v2_only (gf);

    if (title.isNotEmpty())
        id3tag_set_title (gf, title.toRawUTF8());

    if (artist.isNotEmpty())
        id3tag_set_artist (gf, artist.toRawUTF8());

    if (lame_init_params (gf) < 0)
        return juce::Result::fail ("MP3 の設定が正しくありません"_ju);

    mp3.deleteFile();
    juce::FileOutputStream out (mp3);

    if (! out.openedOk())
        return juce::Result::fail ("保存できません: "_ju + mp3.getFullPathName());

    // ID3v2 タグ
    std::vector<unsigned char> buffer (1 << 16);
    const auto tagSize = lame_get_id3v2_tag (gf, buffer.data(), buffer.size());

    if (tagSize > buffer.size())
        return juce::Result::fail ("ID3 タグを作れません"_ju);

    out.write (buffer.data(), tagSize);
    const auto audioStart = out.getPosition();

    // 本体: 少しずつ読んでエンコードする
    constexpr int block = 4096;
    juce::AudioBuffer<float> samples (2, block);
    std::vector<unsigned char> encoded ((size_t) (1.25 * block + 7200));

    for (juce::int64 pos = 0; pos < reader->lengthInSamples; pos += block)
    {
        const int n = (int) std::min<juce::int64> (block, reader->lengthInSamples - pos);
        samples.clear();
        reader->read (&samples, 0, n, pos, true, true);

        // モノラルの元なら左右に同じ音
        if (reader->numChannels == 1)
            samples.copyFrom (1, 0, samples, 0, 0, n);

        const int bytes = lame_encode_buffer_ieee_float (gf, samples.getReadPointer (0), samples.getReadPointer (1), n,
                                                         encoded.data(), (int) encoded.size());
        if (bytes < 0)
            return juce::Result::fail ("MP3 にできませんでした（"_ju + juce::String (bytes) + "）"_ju);

        out.write (encoded.data(), (size_t) bytes);
    }

    const int last = lame_encode_flush (gf, encoded.data(), (int) encoded.size());

    if (last < 0)
        return juce::Result::fail ("MP3 にできませんでした"_ju);

    out.write (encoded.data(), (size_t) last);

    // 先頭のフレームを LAME タグ（正しい長さ）で置き換える
    const auto tagFrame = lame_get_lametag_frame (gf, buffer.data(), buffer.size());

    if (tagFrame > 0 && tagFrame <= buffer.size())
    {
        out.flush();
        out.setPosition (audioStart);
        out.write (buffer.data(), tagFrame);
    }

    out.flush();
    return out.getStatus().wasOk() ? juce::Result::ok() : juce::Result::fail ("保存できません: "_ju + mp3.getFullPathName());
}

}
