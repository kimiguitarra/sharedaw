#include "SfizzPlugin.h"

#include <sfizz.hpp>

const char* SfizzPlugin::xmlTypeName = "collabSfizz";

namespace
{
    constexpr int kSynthBlockSize = 1024;
}

SfizzPlugin::SfizzPlugin (te::PluginCreationInfo info)
    : te::Plugin (info)
{
}

SfizzPlugin::~SfizzPlugin()
{
    notifyListenersOfDeletion();
}

std::unique_ptr<sfz::Sfizz> SfizzPlugin::createSynth (const juce::String& path, const juce::String& text) const
{
    auto s = std::make_unique<sfz::Sfizz>();
    s->setSampleRate ((float) sampleRate);
    s->setSamplesPerBlock (kSynthBlockSize);
    s->setNumVoices (64);

    if (text.isNotEmpty() && ! s->loadSfzString (path.toStdString(), text.toStdString()))
        return {};

    return s;
}

bool SfizzPlugin::setSfz (const juce::String& virtualPath, const juce::String& sfzText)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto newSynth = createSynth (virtualPath, sfzText);

    if (newSynth == nullptr)
        return false;

    loadedPath = virtualPath;
    loadedText = sfzText;

    std::unique_ptr<sfz::Sfizz> old;
    {
        const juce::SpinLock::ScopedLockType sl (synthLock);
        old = std::exchange (synth, std::move (newSynth));
        freeWheeling = false;   // 新しいシンセは通常モードで始まる（ロック中なのでオーディオスレッドと競合しない）
    }

    return true;   // old は（オーディオスレッドの外で）ここで破棄される
}

void SfizzPlugin::clearSfz()
{
    setSfz ({}, {});
}

void SfizzPlugin::setGainAndPan (float gainDb, float pan)
{
    const float gain = juce::Decibels::decibelsToGain (gainDb);
    pan = juce::jlimit (-1.0f, 1.0f, pan);

    // 等パワーのパン
    const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    gainLeft = gain * std::cos (angle) * juce::MathConstants<float>::sqrt2;
    gainRight = gain * std::sin (angle) * juce::MathConstants<float>::sqrt2;
}

void SfizzPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    const bool rateChanged = ! juce::exactlyEqual (info.sampleRate, sampleRate);
    sampleRate = info.sampleRate;
    maxBlockSize = info.blockSizeSamples;
    scratch.setSize (2, kSynthBlockSize);

    if (rateChanged && loadedText.isNotEmpty())
    {
        // サンプルレートが変わったら作り直す（sfizz の再初期化はリアルタイム安全ではないため）
        auto newSynth = createSynth (loadedPath, loadedText);
        const juce::SpinLock::ScopedLockType sl (synthLock);
        std::swap (synth, newSynth);
    }
    else
    {
        const juce::SpinLock::ScopedLockType sl (synthLock);

        if (synth != nullptr)
            synth->setSampleRate ((float) sampleRate);
    }
}

void SfizzPlugin::deinitialise()
{
}

void SfizzPlugin::reset()
{
    const juce::SpinLock::ScopedTryLockType sl (synthLock);

    if (sl.isLocked() && synth != nullptr)
        synth->allSoundOff();
}

void SfizzPlugin::midiPanic()
{
    reset();
}

void SfizzPlugin::queuePreview (const juce::MidiMessage& m)
{
    const auto scope = previewFifo.write (1);

    if (scope.blockSize1 > 0)
        previewQueue[(size_t) scope.startIndex1] = m;
    else if (scope.blockSize2 > 0)
        previewQueue[(size_t) scope.startIndex2] = m;
}

void SfizzPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr)
        return;

    auto& dest = *fc.destBuffer;
    const int numSamples = fc.bufferNumSamples;
    const int start = fc.bufferStartSample;

    for (int ch = 0; ch < dest.getNumChannels(); ++ch)
        dest.clear (ch, start, numSamples);

    const juce::SpinLock::ScopedTryLockType sl (synthLock);

    if (! sl.isLocked() || synth == nullptr)
        return;

    // 書き出し（バウンス・ミックスダウン）はリアルタイムより速く進むので、サンプルの読み込みを待つようにする
    // （待たないと、ディスクから少しずつ読むサンプルの後半＝シンバルの余韻などが途切れる）
    if (fc.isRendering != freeWheeling)
    {
        freeWheeling = fc.isRendering;

        if (freeWheeling)
            synth->enableFreeWheeling();
        else
            synth->disableFreeWheeling();
    }

    // MIDI をサンプル位置に変換（タイムスタンプはブロック先頭からの秒）
    struct Event { int sample; juce::MidiMessage msg; };
    juce::Array<Event, juce::DummyCriticalSection, 64> events;   // 通常はヒープ確保なし

    if (auto* midi = fc.bufferForMidiMessages)
    {
        if (midi->isAllNotesOff)
            synth->allSoundOff();

        for (auto& m : *midi)
        {
            const int pos = juce::jlimit (0, juce::jmax (0, numSamples - 1),
                                          juce::roundToInt ((m.getTimeStamp() + fc.midiBufferOffset) * sampleRate));
            events.add ({ pos, m });
        }
    }

    // 試し弾き（ブロックの頭で鳴らす）
    {
        const auto scope = previewFifo.read (previewFifo.getNumReady());

        for (int i = 0; i < scope.blockSize1; ++i)
            events.insert (0, { 0, previewQueue[(size_t) (scope.startIndex1 + i)] });

        for (int i = 0; i < scope.blockSize2; ++i)
            events.insert (0, { 0, previewQueue[(size_t) (scope.startIndex2 + i)] });
    }

    int eventIndex = 0;

    for (int done = 0; done < numSamples;)
    {
        const int n = juce::jmin (kSynthBlockSize, numSamples - done);

        for (; eventIndex < events.size() && events.getReference (eventIndex).sample < done + n; ++eventIndex)
        {
            auto& e = events.getReference (eventIndex);
            const int delay = juce::jmax (0, e.sample - done);
            auto& m = e.msg;

            if (m.isNoteOn())
                synth->noteOn (delay, m.getNoteNumber(), m.getVelocity());
            else if (m.isNoteOff())
                synth->noteOff (delay, m.getNoteNumber(), m.getVelocity());
            else if (m.isAllNotesOff() || m.isAllSoundOff())
                synth->allSoundOff();
            else if (m.isController())
                synth->cc (delay, m.getControllerNumber(), m.getControllerValue());
            else if (m.isPitchWheel())
                synth->pitchWheel (delay, m.getPitchWheelValue() - 8192);
        }

        float* outs[2] = { scratch.getWritePointer (0), scratch.getWritePointer (1) };
        synth->renderBlock (outs, (size_t) n, 1);

        const float gl = gainLeft.load(), gr = gainRight.load();

        if (dest.getNumChannels() >= 2)
        {
            dest.addFrom (0, start + done, scratch, 0, 0, n, gl);   // dest はクリア済み
            dest.addFrom (1, start + done, scratch, 1, 0, n, gr);
        }
        else if (dest.getNumChannels() == 1)
        {
            dest.addFrom (0, start + done, scratch, 0, 0, n, 0.5f * gl);
            dest.addFrom (0, start + done, scratch, 1, 0, n, 0.5f * gr);
        }

        done += n;
    }
}
