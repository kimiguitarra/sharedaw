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

namespace
{
    constexpr int shortHashLength = 8;

    bool isHex (const juce::String& s)    { return s.isNotEmpty() && s.containsOnly ("0123456789abcdef"); }

    /** audio フォルダの読みやすい名前のファイル（ハッシュの頭 8 文字 → ファイル）。フォルダごとに覚えておき、見つからなければ読み直す。 */
    struct NameIndex
    {
        juce::CriticalSection lock;
        std::map<juce::String, std::multimap<std::string, juce::File>> folders;

        void scan (const juce::File& audioDir)
        {
            auto& files = folders[audioDir.getFullPathName()];
            files.clear();

            for (auto& f : audioDir.findChildFiles (juce::File::findFiles, false, "*.wav"))
                if (auto key = hashKeyOf (f); key.size() == (size_t) shortHashLength)
                    files.insert ({ key, f });
        }

        juce::File find (const juce::File& audioDir, const std::string& hash, bool rescan)
        {
            const juce::ScopedLock sl (lock);

            if (rescan || folders.count (audioDir.getFullPathName()) == 0)
                scan (audioDir);

            auto& files = folders[audioDir.getFullPathName()];
            auto [from, to] = files.equal_range (hash.substr (0, shortHashLength));
            std::vector<juce::File> found;

            for (auto it = from; it != to; ++it)
                if (it->second.existsAsFile())
                    found.push_back (it->second);

            if (found.size() == 1)
                return found.front();

            // 頭の 8 文字が同じ別の実体があるとき（めったにない）は、中身のハッシュで確かめる
            for (auto& f : found)
                if (hashFile (f) == hash)
                    return f;

            return {};
        }

        void add (const juce::File& file)
        {
            const juce::ScopedLock sl (lock);
            folders[file.getParentDirectory().getFullPathName()].insert ({ hashKeyOf (file), file });
        }
    };

    NameIndex& nameIndex()
    {
        static NameIndex index;
        return index;
    }
}

std::string hashKeyOf (const juce::File& file)
{
    if (! file.hasFileExtension ("wav"))
        return {};

    const auto name = file.getFileNameWithoutExtension();

    if (name.length() == 64 && isHex (name))
        return toStd (name);

    const auto tail = name.fromLastOccurrenceOf ("_", false, false);

    if (name.containsChar ('_') && tail.length() == shortHashLength && isHex (tail))
        return toStd (tail);

    return {};
}

juce::File fileForHash (const juce::File& projectDir, const std::string& hash)
{
    const auto audioDir = projectDir.getChildFile ("audio");
    const auto plain = audioDir.getChildFile (toJuce (hash) + ".wav");

    if (hash.empty() || plain.existsAsFile())
        return plain;

    if (auto f = nameIndex().find (audioDir, hash, false); f != juce::File())
        return f;

    // ほかの所（ダウンロードなど）で増えたかもしれないので、読み直してもう一度
    if (auto f = nameIndex().find (audioDir, hash, true); f != juce::File())
        return f;

    return plain;
}

juce::File newFileForHash (const juce::File& projectDir, const std::string& hash, const juce::String& label)
{
    if (auto existing = fileForHash (projectDir, hash); existing.existsAsFile())
        return existing;

    auto name = juce::File::createLegalFileName (label.trim()).replaceCharacter (' ', '_').substring (0, 60);

    if (name.isEmpty())
        name = "audio";

    return projectDir.getChildFile ("audio").getChildFile (name + "_" + toJuce (hash.substr (0, shortHashLength)) + ".wav");
}

juce::String nextTakeLabel (const juce::File& projectDir, const juce::String& trackName)
{
    auto base = juce::File::createLegalFileName (trackName.trim()).replaceCharacter (' ', '_').substring (0, 40);

    if (base.isEmpty())
        base = "Audio";

    const auto prefix = (base + "_take").toLowerCase();
    int highest = 0;

    for (auto& f : projectDir.getChildFile ("audio").findChildFiles (juce::File::findFiles, false, "*.wav"))
    {
        const auto name = f.getFileNameWithoutExtension().toLowerCase();

        if (name.startsWith (prefix))
            highest = juce::jmax (highest, name.substring (prefix.length()).getIntValue());
    }

    return base + "_take" + juce::String (highest + 1).paddedLeft ('0', 2);
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

juce::Result importFile (const juce::File& source, const juce::File& audioDir, Imported& result, const juce::String& label)
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
        return juce::Result::fail ("オーディオファイルを読めません"_ju);

    auto target = newFileForHash (audioDir.getParentDirectory(), hash,
                                  label.isNotEmpty() ? label : source.getFileNameWithoutExtension());

    if (! target.existsAsFile())
    {
        if (! temp.getFile().moveFileTo (target))
            return juce::Result::fail ("保存できません: "_ju + target.getFullPathName());

        nameIndex().add (target);
    }

    std::unique_ptr<juce::AudioFormatReader> check (formats.createReaderFor (target));
    result.hash = hash;
    result.numChannels = numChannels;
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

void AudioFiles::drawWaveform (juce::Graphics& g, juce::AudioThumbnail& thumb, juce::Rectangle<float> area,
                               double startSeconds, double endSeconds, float gain, juce::Colour colour)
{
    const int channels = juce::jmax (1, thumb.getNumChannels());
    const auto clip = g.getClipBounds().toFloat();
    const float left = juce::jmax (area.getX(), clip.getX() - 1.0f), right = juce::jmin (area.getRight(), clip.getRight() + 1.0f);

    if (right <= left || endSeconds <= startSeconds || area.getHeight() < 2.0f)
        return;

    const double secondsPerPixel = (endSeconds - startSeconds) / (double) juce::jmax (1.0f, area.getWidth());
    const float laneHeight = area.getHeight() / (float) channels;

    for (int ch = 0; ch < channels; ++ch)
    {
        const auto lane = area.withY (area.getY() + laneHeight * (float) ch).withHeight (laneHeight);
        const float centre = lane.getCentreY(), half = lane.getHeight() * 0.5f - 1.0f;

        // 中心線（無音の所も波形の位置が分かるように）
        g.setColour (colour.withAlpha (0.45f));
        g.fillRect (left, centre - 0.5f, right - left, 1.0f);

        juce::Array<juce::Point<float>> top, bottom;

        for (float x = left; x <= right; x += 1.0f)
        {
            const double t0 = startSeconds + (double) (x - area.getX()) * secondsPerPixel;
            float mn = 0.0f, mx = 0.0f;
            thumb.getApproximateMinMax (t0, t0 + secondsPerPixel, ch, mn, mx);
            mx = juce::jlimit (-1.0f, 1.0f, mx * gain);
            mn = juce::jlimit (-1.0f, 1.0f, mn * gain);
            // 1 ピクセルは必ず塗る（小さな音も見える）
            top.add ({ x, juce::jmin (centre - 0.5f, centre - mx * half) });
            bottom.add ({ x, juce::jmax (centre + 0.5f, centre - mn * half) });
        }

        juce::Path p;
        p.startNewSubPath (top.getFirst());

        for (auto& pt : top)
            p.lineTo (pt);

        for (int i = bottom.size(); --i >= 0;)
            p.lineTo (bottom.getReference (i));

        p.closeSubPath();
        g.setColour (colour);
        g.fillPath (p);
    }
}

void AudioFiles::drawTransients (juce::Graphics& g, const std::vector<double>* transients, juce::Rectangle<float> area,
                                 double startSeconds, double endSeconds)
{
    if (transients == nullptr || endSeconds <= startSeconds || area.getWidth() < 2.0f)
        return;

    const double pixelsPerSecond = area.getWidth() / (endSeconds - startSeconds);
    const auto clip = g.getClipBounds().toFloat();
    g.setColour (juce::Colour (0xffff7a1a).withAlpha (0.85f));   // 暖色（波形の色と区別できるように）
    float lastX = -1000.0f;

    for (double t : *transients)
    {
        if (t < startSeconds || t >= endSeconds)
            continue;

        const float x = area.getX() + (float) ((t - startSeconds) * pixelsPerSecond);

        // 詰まりすぎるとき（縮小表示）は間引く
        if (x - lastX < 4.0f || x < clip.getX() - 1.0f || x > clip.getRight() + 1.0f)
            continue;

        g.fillRect (x - 0.5f, area.getY(), 1.0f, area.getHeight());
        lastX = x;
    }
}

juce::AudioThumbnail* AudioFileCache::getThumbnail (const juce::File& projectDir, const std::string& hash)
{
    if (auto it = thumbnails.find (hash); it != thumbnails.end())
        return it->second.get();

    auto file = AudioFiles::fileForHash (projectDir, hash);

    if (! file.existsAsFile())
        return nullptr;

    auto thumb = std::make_unique<juce::AudioThumbnail> (128, formats, thumbnailCache);
    thumb->setSource (new juce::FileInputSource (file));
    thumb->addChangeListener (this);
    return (thumbnails[hash] = std::move (thumb)).get();
}

const std::vector<double>* AudioFileCache::getTransients (const juce::File& projectDir, const std::string& hash)
{
    if (auto it = transients.find (hash); it != transients.end())
        return transientsAtPeak ? &it->second.peaks : &it->second.onsets;

    auto* thumb = getThumbnail (projectDir, hash);

    if (thumb == nullptr || ! thumb->isFullyLoaded() || thumb->getTotalLength() <= 0.0)
        return nullptr;

    // 3 ms ごとの大きさ（ピーク）。直前 30 ms のいちばん大きい所より 2 倍（+6 dB）以上に跳ね上がった所を立ち上がりとする。
    // 小さすぎる音（全体のピークの -24 dB 未満）と、前の立ち上がりから 70 ms 以内は数えない
    const double step = 0.003;
    const int count = (int) (thumb->getTotalLength() / step);
    std::vector<float> env ((size_t) juce::jmax (0, count));

    for (int i = 0; i < count; ++i)
        for (int ch = 0; ch < thumb->getNumChannels(); ++ch)
        {
            float mn = 0.0f, mx = 0.0f;
            thumb->getApproximateMinMax (i * step, (i + 1) * step, ch, mn, mx);
            env[(size_t) i] = juce::jmax (env[(size_t) i], std::abs (mn), std::abs (mx));
        }

    const float peak = env.empty() ? 0.0f : *std::max_element (env.begin(), env.end());
    auto& result = transients[hash];
    std::vector<double> rough;
    int last = -1000;

    for (int i = 1; i < count && peak > 0.0f; ++i)
    {
        float before = 0.0f;

        for (int k = juce::jmax (0, i - 10); k < i; ++k)
            before = juce::jmax (before, env[(size_t) k]);

        if (env[(size_t) i] >= peak * 0.063f && env[(size_t) i] > before * 2.0f && i - last >= 23)
        {
            rough.push_back (i * step);
            last = i;
        }
    }

    // 波形の概略（3 ms 刻み）で見つけた所を、実際のサンプルで詰める:
    // 鳴り始め = その音のピークの 10 % に初めて届いたサンプル、ピーク = 60 ms 以内でいちばん大きいサンプル
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (AudioFiles::fileForHash (projectDir, hash)));

    for (double t : rough)
    {
        if (reader == nullptr || reader->sampleRate <= 0.0)
        {
            result.onsets.push_back (t);
            result.peaks.push_back (t);
            continue;
        }

        const double rate = reader->sampleRate;
        const auto from = juce::jmax ((juce::int64) 0, (juce::int64) ((t - 0.006) * rate));
        const int length = (int) juce::jmin ((juce::int64) (0.066 * rate), reader->lengthInSamples - from);

        if (length <= 0)
            continue;

        juce::AudioBuffer<float> buffer ((int) juce::jmax (1u, reader->numChannels), length);
        reader->read (&buffer, 0, length, from, true, true);

        std::vector<float> level ((size_t) length, 0.0f);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < length; ++i)
                level[(size_t) i] = juce::jmax (level[(size_t) i], std::abs (buffer.getSample (ch, i)));

        const auto loudest = (int) (std::max_element (level.begin(), level.end()) - level.begin());
        const float top = level[(size_t) loudest];
        int start = loudest;

        for (int i = 0; i <= loudest; ++i)
            if (level[(size_t) i] >= top * 0.1f)
            {
                start = i;
                break;
            }

        result.onsets.push_back ((double) (from + start) / rate);
        result.peaks.push_back ((double) (from + loudest) / rate);
    }

    return transientsAtPeak ? &result.peaks : &result.onsets;
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
