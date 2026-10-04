#include "audio/Export.h"

#include "EngineBridge.h"
#include "ProjectDocument.h"
#include "audio/Mp3Export.h"
#include "collab/ChordPlayback.h"
#include "collab/MasterDsp.h"
#include "collab/Render.h"
#include "collab/MidiExport.h"

namespace Export
{

namespace
{
    collab::Tick songEnd (const ProjectDocument& doc)
    {
        return collab::chordTrackEndTick (doc.getProject(), doc.getTempoMap());
    }
}

juce::Result mixdownWav (EngineBridge& bridge, const ProjectDocument& doc, const juce::File& wav)
{
    if (! bridge.renderToFile (wav, songEnd (doc), bridge.tailSecondsFor ({}), 24))
        return juce::Result::fail ("ミックスダウンを書き出せませんでした。"_ju);

    return juce::Result::ok();
}

juce::Result mixdownMp3 (EngineBridge& bridge, const ProjectDocument& doc, const juce::File& mp3)
{
    // 44.1 kHz の 32 bit float WAV に書き出してから MP3 にする（途中で丸めない）
    juce::TemporaryFile temp (mp3.withFileExtension ("wav"));

    if (! bridge.renderToFile (temp.getFile(), songEnd (doc), bridge.tailSecondsFor ({}), 32, 44100.0))
        return juce::Result::fail ("ミックスダウンを書き出せませんでした。"_ju);

    return Mp3Export::encode (temp.getFile(), mp3, 320, toJuce (doc.getProject().name));
}

juce::Result stems (EngineBridge& bridge, const ProjectDocument& doc, const juce::File& folder, juce::Array<juce::File>& written)
{
    if (! folder.isDirectory() && ! folder.createDirectory())
        return juce::Result::fail ("フォルダを作れません: "_ju + folder.getFullPathName());

    const auto& project = doc.getProject();
    const double end = doc.getTempoMap().tickToSeconds ((double) songEnd (doc)) + bridge.tailSecondsFor ({});
    int number = 1;

    auto fileFor = [&] (const juce::String& name)
    {
        auto safe = juce::File::createLegalFileName (name.trim());
        return folder.getChildFile (juce::String (number++).paddedLeft ('0', 2) + "_" + (safe.isEmpty() ? juce::String ("Track") : safe) + ".wav");
    };

    for (auto& t : project.tracks)
    {
        if (t.type == collab::TrackType::bus || t.mute || collab::isHiddenBounceTrack (project, t))
            continue;

        auto file = fileFor (toJuce (t.name));

        if (auto r = bridge.renderStem (t.id, file, end); r.failed())
            return juce::Result::fail (toJuce (t.name) + ": "_ju + r.getErrorMessage());

        written.add (file);
    }

    if (project.chordTrack.playback.enabled && ! collab::renderChordTrack (project, doc.getTempoMap()).empty())
    {
        auto file = fileFor ("コード"_ju);

        if (auto r = bridge.renderStem ({}, file, end); r.failed())
            return juce::Result::fail ("コード: "_ju + r.getErrorMessage());

        written.add (file);
    }

    if (written.isEmpty())
        return juce::Result::fail ("書き出すトラックがありません（ミュート中のトラックとバスは書き出しません）。"_ju);

    return juce::Result::ok();
}

juce::Result midi (const ProjectDocument& doc, const juce::File& mid)
{
    const auto bytes = collab::writeMidiFile (doc.getProject(), doc.getTempoMap(), true);
    mid.deleteFile();

    if (! mid.replaceWithData (bytes.data(), bytes.size()))
        return juce::Result::fail ("保存できません: "_ju + mid.getFullPathName());

    return juce::Result::ok();
}

juce::String loudnessSummary (const juce::File& audio)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (audio));

    if (reader == nullptr)
        return {};

    collab::LoudnessBlocks blocks;
    blocks.prepare (reader->sampleRate);
    collab::LoudnessStats stats;
    std::vector<double> out;
    juce::AudioBuffer<float> buffer (2, 48000);
    float peak = 0.0f;

    for (juce::int64 pos = 0; pos < reader->lengthInSamples; pos += buffer.getNumSamples())
    {
        const int n = (int) juce::jmin ((juce::int64) buffer.getNumSamples(), reader->lengthInSamples - pos);
        reader->read (&buffer, 0, n, pos, true, true);
        peak = juce::jmax (peak, buffer.getMagnitude (0, n));
        out.clear();
        const float* ch[2] = { buffer.getReadPointer (0), buffer.getReadPointer (reader->numChannels > 1 ? 1 : 0) };
        blocks.process (ch, 2, n, out);

        for (double b : out)
            stats.addBlock (b);
    }

    return "ラウドネス: "_ju + juce::String (stats.integratedLufs(), 1) + " LUFS（目標 -14）\nピーク: "_ju
           + juce::String (juce::Decibels::gainToDecibels (peak, -100.0f), 1) + " dBFS"_ju;
}

}
