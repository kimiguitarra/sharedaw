#include "PitchShift.h"

#include <map>
#include <tuple>
#include <mutex>

namespace PitchShift
{
namespace
{
    constexpr int blockSize = 512;

    /**
        input 全体の高さを変え、speed 倍の速さにする（長さは 1/speed）。出力は入力と同じ位置から始まるように
        lag だけ先を捨てる（足りない分は 0）。
    */
    std::vector<std::vector<float>> process (const std::vector<std::vector<float>>& input, double sampleRate, double semitones,
                                             double speed, int lag)
    {
        const int numChannels = (int) input.size();
        const int inputLength = numChannels > 0 ? (int) input[0].size() : 0;
        const int length = (int) std::lround (inputLength / speed);   // 出力の長さ

        te::TimeStretcher stretcher;
        stretcher.initialise (sampleRate, blockSize, numChannels, te::TimeStretcher::soundtouchBetter, {}, false);
        stretcher.setSpeedAndPitch ((float) (1.0 / speed), (float) semitones);   // Tracktion の speedRatio は「長さの倍率」

        // 終わりまで出し切るため、後ろに少し無音を足して流す
        const int padded = inputLength + (int) std::lround ((lag + 16384) * speed);
        std::vector<std::vector<float>> in ((size_t) numChannels, std::vector<float> ((size_t) padded, 0.0f));
        std::vector<std::vector<float>> out ((size_t) numChannels);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            std::copy (input[(size_t) ch].begin(), input[(size_t) ch].end(), in[(size_t) ch].begin());
            out[(size_t) ch].reserve ((size_t) (padded / speed) + 2 * blockSize);
        }

        std::vector<std::vector<float>> block ((size_t) numChannels, std::vector<float> ((size_t) blockSize * 4, 0.0f));
        std::vector<const float*> inPtrs ((size_t) numChannels);
        std::vector<float*> outPtrs ((size_t) numChannels);

        for (int ch = 0; ch < numChannels; ++ch)
            outPtrs[(size_t) ch] = block[(size_t) ch].data();

        auto append = [&] (int n)
        {
            for (int ch = 0; ch < numChannels; ++ch)
                out[(size_t) ch].insert (out[(size_t) ch].end(), block[(size_t) ch].begin(), block[(size_t) ch].begin() + n);
        };

        int pos = 0;

        while (pos < padded && (int) out[0].size() < length + lag)
        {
            const int n = std::min (std::max (0, stretcher.getFramesNeeded()), padded - pos);

            for (int ch = 0; ch < numChannels; ++ch)
                inPtrs[(size_t) ch] = in[(size_t) ch].data() + pos;

            const int got = stretcher.processData (inPtrs.data(), n, outPtrs.data());
            append (got);
            pos += n;

            if (n == 0 && got == 0)
                break;   // 進まない（念のため）
        }

        for (int guard = 0; guard < 1000 && (int) out[0].size() < length + lag; ++guard)
        {
            const int n = stretcher.flush (outPtrs.data());

            if (n <= 0)
                break;

            append (n);
        }

        std::vector<std::vector<float>> result ((size_t) numChannels, std::vector<float> ((size_t) length, 0.0f));

        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < length; ++i)
                if (const int k = i + lag; k >= 0 && k < (int) out[(size_t) ch].size())
                    result[(size_t) ch][(size_t) i] = out[(size_t) ch][(size_t) k];

        return result;
    }

    /** 音の重心（エネルギーで重み付けした位置）。 */
    double centroid (const std::vector<float>& x)
    {
        double sum = 0.0, weighted = 0.0;

        for (size_t i = 0; i < x.size(); ++i)
        {
            const double e = (double) x[i] * x[i];
            sum += e;
            weighted += e * (double) i;
        }

        return sum > 0.0 ? weighted / sum : 0.0;
    }

    /**
        処理で音が遅れる量（サンプル数）。短い音の粒を通して、出てくる位置との差を測る（高さと処理の設定で決まり、音の中身にはよらない）。
    */
    int lagFor (double sampleRate, double semitones, double speed)
    {
        static std::mutex lock;
        static std::map<std::tuple<int, int, int>, int> cache;
        const auto key = std::make_tuple ((int) sampleRate, (int) std::lround (semitones * 100.0), (int) std::lround (speed * 10000.0));

        {
            const std::lock_guard<std::mutex> l (lock);

            if (auto it = cache.find (key); it != cache.end())
                return it->second;
        }

        // 0.2 秒おきに 20 ms の粒（いろいろな高さが混ざった音）を 12 個。処理の区切りとの位置関係で少しずつ違うので平均する
        constexpr int numGrains = 12;
        const int spacing = (int) (sampleRate * 0.2), grain = (int) (sampleRate * 0.02), first = (int) (sampleRate * 0.3);
        const int length = first + spacing * numGrains + (int) sampleRate;
        std::vector<std::vector<float>> probe (1, std::vector<float> ((size_t) length, 0.0f));
        juce::Random random (1234);

        for (int k = 0; k < numGrains; ++k)
            for (int i = 0; i < grain; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / (grain - 1));
                probe[0][(size_t) (first + k * spacing + i)] = (float) (w * (random.nextFloat() * 2.0f - 1.0f) * 0.5f);
            }

        const auto shifted = process (probe, sampleRate, semitones, speed, 0);
        double total = 0.0;

        for (int k = 0; k < numGrains; ++k)
        {
            // 粒のまわり（前後 0.09 秒）の重心の差（出力の位置は入力の 1/speed）
            const int from = first + k * spacing - spacing / 2 + grain / 2;
            const int to = from + spacing;
            const int outFrom = (int) (from / speed), outTo = std::min ((int) shifted[0].size(), (int) (to / speed));
            const double in = from + centroid (std::vector<float> (probe[0].begin() + from, probe[0].begin() + to));
            const double out = outFrom + centroid (std::vector<float> (shifted[0].begin() + outFrom, shifted[0].begin() + outTo));
            total += out - in / speed;
        }

        const int lag = (int) std::lround (total / numGrains);

        const std::lock_guard<std::mutex> l (lock);
        cache[key] = lag;
        return lag;
    }
}

juce::File cachedFile (const juce::File& projectDir, const std::string& hash, double semitones, double speed)
{
    const int cents = (int) std::lround (semitones * 100.0);
    auto name = toJuce (hash) + "_" + (cents >= 0 ? "+" : "") + juce::String (cents);

    if (std::abs (speed - 1.0) > 1.0e-6)
        name << "_x" << juce::String (speed, 5);

    return projectDir.getChildFile ("cache").getChildFile ("pitch").getChildFile (name + ".wav");
}

bool render (const juce::File& source, const juce::File& dest, double semitones, double speed)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (source));

    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->lengthInSamples > std::numeric_limits<int>::max() / 2)
        return false;

    const int numChannels = (int) reader->numChannels;
    const int length = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> buffer (numChannels, length);
    reader->read (&buffer, 0, length, 0, true, true);

    std::vector<std::vector<float>> input ((size_t) numChannels);

    for (int ch = 0; ch < numChannels; ++ch)
        input[(size_t) ch].assign (buffer.getReadPointer (ch), buffer.getReadPointer (ch) + length);

    const auto shifted = process (input, reader->sampleRate, semitones, speed, lagFor (reader->sampleRate, semitones, speed));
    const int outLength = numChannels > 0 ? (int) shifted[0].size() : 0;
    buffer.setSize (numChannels, outLength);

    for (int ch = 0; ch < numChannels; ++ch)
        std::copy (shifted[(size_t) ch].begin(), shifted[(size_t) ch].end(), buffer.getWritePointer (ch));

    // 書き終わってから名前を付ける（途中のファイルを鳴らさない）
    dest.getParentDirectory().createDirectory();
    const auto temp = dest.getSiblingFile (dest.getFileNameWithoutExtension() + ".part.wav");
    temp.deleteFile();

    {
        std::unique_ptr<juce::OutputStream> stream (temp.createOutputStream());

        if (stream == nullptr)
            return false;

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), reader->sampleRate, (unsigned int) numChannels,
                                                                              32, {}, 0));

        if (writer == nullptr)
            return false;

        stream.release();   // writer が持つ

        if (! writer->writeFromAudioSampleBuffer (buffer, 0, outLength))
            return false;
    }

    return temp.moveFileTo (dest);
}
}
