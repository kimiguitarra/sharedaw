#include "Takes.h"

#include "AudioFiles.h"
#include "collab/Recording.h"
#include "collab/Uuid.h"

namespace Takes
{

namespace
{
    /** ステレオのトラック: 左と右の入力で別々に録れたファイルを、1 つのステレオの WAV にする。 */
    juce::Result mergeStereo (const juce::File& left, const juce::File& right, const juce::File& dest)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> l (formats.createReaderFor (left)), r (formats.createReaderFor (right));

        if (l == nullptr || r == nullptr)
            return juce::Result::fail ("録音したファイルを読めませんでした"_ju);

        const auto length = (int) juce::jmax (l->lengthInSamples, r->lengthInSamples);
        juce::AudioBuffer<float> buffer (2, length);
        buffer.clear();
        juce::AudioBuffer<float> one (1, length);

        for (auto [reader, ch] : { std::pair (l.get(), 0), std::pair (r.get(), 1) })
        {
            one.clear();
            reader->read (&one, 0, (int) reader->lengthInSamples, 0, true, false);
            buffer.copyFrom (ch, 0, one, 0, 0, length);
        }

        dest.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (dest.createOutputStream());
        std::unique_ptr<juce::AudioFormatWriter> writer (stream != nullptr
            ? wav.createWriterFor (stream.get(), l->sampleRate, 2, 32, {}, 0) : nullptr);

        if (writer == nullptr)
            return juce::Result::fail ("ステレオのファイルを書けませんでした"_ju);

        stream.release();   // writer が持つ
        writer->writeFromAudioSampleBuffer (buffer, 0, length);
        return juce::Result::ok();
    }
}

juce::Result import (const std::vector<EngineBridge::RecordedTake>& input, const juce::File& projectDir,
                     const collab::TempoMap& map, std::vector<Clip>& result, const std::map<std::string, juce::String>& trackNames)
{
    juce::StringArray errors;
    const auto name = "テイク"_ju;   // 番号はトラックに加えるときに付ける（addToProject）

    // ステレオのトラックは左右の 2 つのテイクを 1 つにまとめる
    std::vector<EngineBridge::RecordedTake> takes;
    std::vector<bool> used (input.size(), false);

    for (size_t i = 0; i < input.size(); ++i)
    {
        if (used[i])
            continue;

        auto take = input[i];
        used[i] = true;

        if (take.stereo)
        {
            for (size_t k = i + 1; k < input.size(); ++k)
            {
                auto& other = input[k];

                if (! used[k] && other.stereo && other.trackId == take.trackId && other.channel != take.channel
                    && std::abs (other.startSeconds - take.startSeconds) < 0.01)
                {
                    used[k] = true;
                    const auto& left = take.channel == 0 ? take : other;
                    const auto& right = take.channel == 0 ? other : take;
                    auto merged = left.file.getSiblingFile (left.file.getFileNameWithoutExtension() + "-stereo.wav");

                    if (auto r = mergeStereo (left.file, right.file, merged); r.failed())
                    {
                        errors.add (r.getErrorMessage());
                        break;
                    }

                    left.file.deleteFile();
                    right.file.deleteFile();
                    take.file = merged;
                    take.lengthSeconds = juce::jmax (left.lengthSeconds, right.lengthSeconds);
                    break;
                }
            }
        }

        takes.push_back (take);
    }

    for (auto& take : takes)
    {
        AudioFiles::Imported imported;

        const auto it = trackNames.find (take.trackId);
        const auto label = AudioFiles::nextTakeLabel (projectDir, it != trackNames.end() ? it->second : juce::String ("Audio"));

        if (auto r = AudioFiles::importFile (take.file, projectDir.getChildFile ("audio"), imported, label); r.failed())
        {
            errors.add (r.getErrorMessage());
            continue;
        }

        take.file.deleteFile();

        auto clip = collab::makeRecordedClip (map, collab::generateUuid(), imported.hash, toStd (name),
                                              take.startSeconds, take.offsetSeconds, take.lengthSeconds, imported.lengthSamples,
                                              take.punchInSeconds);

        if (clip.lengthSamples > 0)
            result.push_back ({ take.trackId, clip });
    }

    return errors.isEmpty() ? juce::Result::ok() : juce::Result::fail (errors.joinIntoString ("\n"));
}

void addToProject (ProjectDocument& document, const std::vector<Clip>& clips)
{
    if (clips.empty())
        return;

    document.perform ("録音"_ju, [clips] (collab::Project& p)
    {
        for (auto& c : clips)
        {
            auto* t = p.findTrack (c.trackId);

            if (t == nullptr || t->type != collab::TrackType::audio)
                continue;

            // 名前は「テイク 1」「テイク 2」…（そのトラックのいちばん大きい番号の次）
            int number = 0;

            for (auto& existing : t->audioClips)
            {
                const auto existingName = toJuce (existing.displayName);

                if (existingName.startsWith ("テイク "_ju) && existingName.fromFirstOccurrenceOf (" ", false, false).containsOnly ("0123456789"))
                    number = std::max (number, existingName.fromFirstOccurrenceOf (" ", false, false).getIntValue());
            }

            auto clip = c.clip;
            clip.displayName = toStd ("テイク "_ju + juce::String (number + 1));
            t->audioClips.push_back (clip);
        }
    });
}

}
