#include "AudioFiles.h"

#include "collab/Project.h"
#include "collab/Sha256.h"
#include "collab/Unicode.h"

namespace AudioFiles
{

juce::String supportedWildcard()
{
    return "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3";
}

juce::File fileForHash (const juce::File& projectDir, const std::string& hash)
{
    return projectDir.getChildFile ("audio").getChildFile (toJuce (hash) + ".wav");
}

std::string hashFile (const juce::File& file)
{
    juce::FileInputStream in (file);

    if (! in.openedOk())
        return {};

    collab::Sha256 sha;
    juce::HeapBlock<char> buffer (1 << 16);

    for (;;)
    {
        const auto n = in.read (buffer.get(), 1 << 16);

        if (n <= 0)
            break;

        sha.update (buffer.get(), (size_t) n);
    }

    return sha.finishHex();
}

juce::Result importFile (const juce::File& source, const juce::File& audioDir, Imported& result)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (source));

    if (reader == nullptr)
        return juce::Result::fail ("読み込めない形式です: "_ju + source.getFileName());

    const int numChannels = juce::jlimit (1, 2, (int) reader->numChannels);
    const double ratio = reader->sampleRate / (double) collab::kSampleRate;
    audioDir.createDirectory();

    juce::TemporaryFile temp (audioDir.getChildFile ("import.wav"));

    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> out (temp.getFile().createOutputStream());

        if (out == nullptr)
            return juce::Result::fail ("書き込めません: "_ju + temp.getFile().getFullPathName());

        // 32bit float WAV（メタデータなし: 同じ音なら同じハッシュになるように）
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (out.get(), (double) collab::kSampleRate,
                                                                              (unsigned int) numChannels, 32, {}, 0));
        if (writer == nullptr)
            return juce::Result::fail ("WAV を作成できません"_ju);

        out.release();

        constexpr int block = 8192;
        juce::AudioBuffer<float> buffer (numChannels, block);

        if (std::abs (ratio - 1.0) < 1e-9)
        {
            for (juce::int64 pos = 0; pos < reader->lengthInSamples; pos += block)
            {
                const int n = (int) std::min<juce::int64> (block, reader->lengthInSamples - pos);
                buffer.clear();
                reader->read (&buffer, 0, n, pos, true, numChannels > 1);
                writer->writeFromAudioSampleBuffer (buffer, 0, n);
            }
        }
        else
        {
            // 48kHz 以外はリサンプリングする（途切れないようにストリームとして処理）
            const auto totalOut = (juce::int64) std::ceil ((double) reader->lengthInSamples / ratio);
            juce::AudioFormatReaderSource readerSource (reader.get(), false);
            juce::ResamplingAudioSource resampler (&readerSource, false, numChannels);
            resampler.setResamplingRatio (ratio);
            resampler.prepareToPlay (block, (double) collab::kSampleRate);

            for (juce::int64 done = 0; done < totalOut;)
            {
                const int n = (int) std::min<juce::int64> (block, totalOut - done);
                juce::AudioSourceChannelInfo info (&buffer, 0, n);
                buffer.clear();
                resampler.getNextAudioBlock (info);
                writer->writeFromAudioSampleBuffer (buffer, 0, n);
                done += n;
            }

            resampler.releaseResources();
        }
    }

    const auto hash = hashFile (temp.getFile());

    if (hash.empty())
        return juce::Result::fail ("ハッシュを計算できません"_ju);

    auto target = audioDir.getChildFile (toJuce (hash) + ".wav");

    if (! target.existsAsFile() && ! temp.getFile().moveFileTo (target))
        return juce::Result::fail ("保存できません: "_ju + target.getFullPathName());

    std::unique_ptr<juce::AudioFormatReader> check (formats.createReaderFor (target));
    result.hash = hash;
    result.lengthSamples = check != nullptr ? check->lengthInSamples : 0;
    result.displayName = toJuce (collab::toNfc (toStd (source.getFileNameWithoutExtension())));
    return juce::Result::ok();
}

}

//==============================================================================
AudioFileCache::AudioFileCache()
{
    formats.registerBasicFormats();
}

AudioFileCache::~AudioFileCache()
{
    for (auto& [h, t] : thumbnails)
        t->removeChangeListener (this);
}

juce::AudioThumbnail* AudioFileCache::getThumbnail (const juce::File& projectDir, const std::string& hash)
{
    if (auto it = thumbnails.find (hash); it != thumbnails.end())
        return it->second.get();

    auto file = AudioFiles::fileForHash (projectDir, hash);

    if (! file.existsAsFile())
        return nullptr;

    auto thumb = std::make_unique<juce::AudioThumbnail> (512, formats, thumbnailCache);
    thumb->setSource (new juce::FileInputSource (file));
    thumb->addChangeListener (this);
    return (thumbnails[hash] = std::move (thumb)).get();
}

juce::int64 AudioFileCache::getLengthSamples (const juce::File& projectDir, const std::string& hash)
{
    if (auto it = lengths.find (hash); it != lengths.end())
        return it->second;

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (AudioFiles::fileForHash (projectDir, hash)));

    if (reader == nullptr)
        return 0;

    return lengths[hash] = reader->lengthInSamples;
}

void AudioFileCache::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (onThumbnailChanged)
        onThumbnailChanged();
}
