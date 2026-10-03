// EngineBridge の録音: 入力（オーディオ・MIDI）、入力レベル、録音の開始・終了と取り込み

#include "EngineBridge.h"
#include "EngineBridgeDetail.h"

#include <tracktion_graph/tracktion_graph.h>

#include "collab/ClipEditing.h"
#include <iostream>
#include <sstream>
#include "SfizzPlugin.h"
#include "audio/ChannelStripPlugin.h"
#include "audio/BuiltinEffectPlugin.h"
#include "audio/MasterLimiterPlugin.h"
#include "audio/CountInPlugin.h"
#include "collab/Recording.h"
#include "collab/ChordPlayback.h"
#include "collab/Render.h"
#include "audio/AudioFiles.h"
#include "collab/Time.h"
#include "collab/Uuid.h"
#include "plugins/PluginHost.h"

using namespace EngineBridgeDetail;

//==============================================================================
void EngineBridge::InputMeter::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                                                 int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    for (int ch = 0; ch < juce::jmin (numIn, maxChannels); ++ch)
    {
        if (in[ch] == nullptr)
            continue;

        const auto range = juce::FloatVectorOperations::findMinAndMax (in[ch], numSamples);
        const float peak = juce::jmax (std::abs (range.getStart()), std::abs (range.getEnd()));

        if (peak > peaks[(size_t) ch].load (std::memory_order_relaxed))
            peaks[(size_t) ch].store (peak, std::memory_order_relaxed);

        if (peak > recordingPeaks[(size_t) ch].load (std::memory_order_relaxed))
            recordingPeaks[(size_t) ch].store (peak, std::memory_order_relaxed);
    }

    // 録音（Tracktion の音の処理はこのコールバックより先に呼ばれるので、再生位置はこのブロックのもの）
    {
        const juce::SpinLock::ScopedTryLockType sl (recorderLock);

        if (sl.isLocked() && recorder != nullptr && recorder->playHead != nullptr && recorder->playHead->isPlaying()
             && numSamples <= (int) recorder->silence.size())
        {
            const auto position = recorder->playHead->getUnloopedPosition();

            for (auto& take : recorder->takes)
            {
                if (take->writer == nullptr)
                    continue;

                // 位置が飛んだ（ループ・位置の変更）ブロックは数えない
                if (take->numAnchors < (int) take->anchors.size()
                     && (take->lastPosition == std::numeric_limits<juce::int64>::min() || position == take->lastPosition + take->lastBlock))
                    take->anchors[(size_t) take->numAnchors++] = position - take->written;

                take->lastPosition = position;
                take->lastBlock = numSamples;

                const float* data[2] = { recorder->silence.data(), recorder->silence.data() };

                for (int c = 0; c < take->numChannels; ++c)
                    if (const int index = take->channels[(size_t) c]; index >= 0 && index < numIn && in[index] != nullptr)
                        data[c] = in[index];

                if (take->writer->write (data, numSamples))
                    take->written += numSamples;
            }
        }
    }

    // 音は出さない（ほかのコールバックの音に足されるので 0 にしておく）
    for (int ch = 0; ch < numOut; ++ch)
        if (out[ch] != nullptr)
            juce::FloatVectorOperations::clear (out[ch], numSamples);
}

std::vector<EngineBridge::InputLevel> EngineBridge::getInputLevels()
{
    std::vector<InputLevel> result;
    auto* device = engine.getDeviceManager().deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
        return result;

    const auto names = device->getInputChannelNames();
    const auto active = device->getActiveInputChannels();
    int index = 0;   // コールバックには有効なチャンネルだけが順に来る

    for (int ch = 0; ch < names.size(); ++ch)
    {
        if (! active[ch])
            continue;

        if (index < InputMeter::maxChannels)
        {
            const float peak = inputMeter.peaks[(size_t) index].exchange (0.0f, std::memory_order_relaxed);
            result.push_back ({ names[ch], juce::Decibels::gainToDecibels (peak, -100.0f) });
        }

        ++index;
    }

    return result;
}

juce::StringArray EngineBridge::getAudioInputs() const
{
    juce::StringArray names;
    auto& dm = engine.getDeviceManager();

    for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
        if (auto* w = dm.getWaveInDevice (i); w != nullptr && w->isEnabled())
            names.add (w->getName());

    return names;
}

EngineBridge::TrackInput EngineBridge::getTrackInput (const std::string& trackId) const
{
    auto it = trackInputs.find (trackId);
    return it != trackInputs.end() ? it->second : TrackInput {};
}

void EngineBridge::setTrackInput (const std::string& trackId, const TrackInput& input)
{
    // 1 つの入力は 1 つのトラックにだけ割り当てる
    for (auto& [id, other] : trackInputs)
        if (id != trackId)
            for (auto& name : { input.device, input.deviceRight })
                if (name.isNotEmpty() && (other.device == name || other.deviceRight == name))
                    other = {};

    trackInputs[trackId] = input;
    applyInputs();
}

void EngineBridge::applyInputs()
{
    // 録音中は入力の割り当てを変えない（変えると、その入力の録音が止まる）。録音が終わってから行う
    if (edit->getTransport().isRecording())
    {
        inputsChangedWhileRecording = true;
        return;
    }

    edit->getTransport().ensureContextAllocated();

    for (auto* in : edit->getAllInputDevices())
    {
        // MIDI キーボード: 選択中の MIDI トラックの音源で鳴らし、録音もそのトラックへ
        // （録音のタイミングの確認用の仮想 MIDI 入力も同じに扱う: --record-test）
        if (in->getInputDevice().getDeviceType() == te::InputDevice::physicalMidiDevice
             || (in->getInputDevice().getDeviceType() == te::InputDevice::virtualMidiDevice && in->getInputDevice().getName() == loopbackMidiName))
        {
            te::AudioTrack* target = nullptr;

            if (auto b = bindings.find (midiTargetId); b != bindings.end() && b->second.track != nullptr && ! b->second.renderMode)
            {
                // トラックで入力を選んでいれば、その機器だけ
                const auto choice = getTrackMidiInput (midiTargetId);

                if (choice.isEmpty() || choice == in->getInputDevice().getName())
                    target = b->second.track.get();
            }

            for (auto id : in->getTargets())
                if (target == nullptr || id != target->itemID)
                    [[maybe_unused]] auto r = in->removeTarget (id, nullptr);

            in->getInputDevice().setMonitorMode (te::InputDevice::MonitorMode::on);

            if (target != nullptr)
            {
                if (! in->getTargets().contains (target->itemID))
                    [[maybe_unused]] auto r = in->setTarget (target->itemID, true, nullptr);

                in->setRecordingEnabled (target->itemID, true);
            }

            continue;
        }

        if (in->getInputDevice().getDeviceType() != te::InputDevice::waveDevice)
            continue;

        const auto name = in->getInputDevice().getName();
        te::AudioTrack* target = nullptr;
        TrackInput setting;

        for (auto& [id, ti] : trackInputs)
        {
            auto b = bindings.find (id);

            if ((ti.device == name || ti.deviceRight == name) && (ti.armed || ti.monitor) && b != bindings.end() && b->second.track != nullptr)
            {
                target = b->second.track.get();
                setting = ti;
            }
        }

        for (auto id : in->getTargets())
            if (target == nullptr || id != target->itemID)
                [[maybe_unused]] auto r = in->removeTarget (id, nullptr);

        in->getInputDevice().setMonitorMode (setting.monitor ? te::InputDevice::MonitorMode::on
                                                             : te::InputDevice::MonitorMode::off);

        if (target != nullptr)
        {
            if (! in->getTargets().contains (target->itemID))
                [[maybe_unused]] auto r = in->setTarget (target->itemID, false, nullptr);

            in->setRecordingEnabled (target->itemID, setting.armed);
        }
    }
}

void EngineBridge::setManualLatencySamples (int samples)
{
    manualLatencySamples = samples;
    auto& dm = engine.getDeviceManager();
    const double rate = dm.getSampleRate() > 0 ? dm.getSampleRate() : (double) collab::kSampleRate;

    for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
        if (auto* w = dm.getWaveInDevice (i))
            w->setRecordAdjustmentMs (samples * 1000.0 / rate);
}

juce::Result EngineBridge::startRecording (int countInBars)
{
    auto& transport = edit->getTransport();

    if (transport.isRecording())
        return juce::Result::ok();

    bool anyArmed = false;

    for (auto& [id, ti] : trackInputs)
        anyArmed = anyArmed || (ti.armed && ti.device.isNotEmpty() && bindings.count (id) > 0);

    // MIDI キーボードは選択中の MIDI トラックに録音する
    const bool midiReady = bindings.count (midiTargetId) > 0
                            && std::any_of (midiInputs.begin(), midiInputs.end(), [] (auto& in) { return in->device->isEnabled(); });
    anyArmed = anyArmed || midiReady;

    if (! anyArmed)
        return juce::Result::fail ("録音するトラックがありません。オーディオトラックの録音待機（●）をオンにするか、"_ju
                                   "MIDI キーボードをつないで MIDI トラックを選んでください。"_ju);

    // 録音はいったん一時フォルダに書き、終わったら 48kHz / 32bit float に変換して audio/ に取り込む
    auto dir = engine.getTemporaryFileManager().getTempDirectory().getChildFile ("recordings");
    dir.createDirectory();
    auto& dm = engine.getDeviceManager();

    for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
        if (auto* w = dm.getWaveInDevice (i))
            w->setFilenameMask (dir.getChildFile ("take").getFullPathName());

    setManualLatencySamples (manualLatencySamples);
    applyInputs();

    punchInSeconds = getPositionSeconds();

    if (! transport.isPlaying())
    {
        // カウントイン: Edit は 60BPM（1拍 = 1秒）なので、1小節 = ceil(秒数) 拍の拍子にして
        // Tracktion にプリロールさせ、クリックは CountInPlugin がテンポマップどおりに鳴らす
        const double start = getPositionSeconds();
        const auto& map = document.getTempoMap();
        const double length = collab::countInSeconds (map, start, countInBars);

        if (length > 0.0)
        {
            std::vector<collab::Click> clicks;

            for (auto& c : collab::countInClicks (map, start, countInBars))
                if (c.seconds < 0.0 || ! metronomeEnabled)   // 0 秒以降はメトロノームのトラックが鳴る
                    clicks.push_back (c);

            if (countIn != nullptr)
                countIn->setClicks (std::move (clicks), metronomeVolumeDb);

            edit->tempoSequence.getTimeSig (0)->setStringTimeSig (juce::String ((int) std::ceil (length - 1.0e-6)) + "/4");
            edit->setCountInMode (te::Edit::CountIn::oneBar);
        }
        else
        {
            edit->setCountInMode (te::Edit::CountIn::none);
        }
    }

    startLoudness();
    transport.record (false);

    // Tracktion 自身のカウントインのクリック（Edit の 60BPM で鳴る）は使わない。
    // 最初のクリックはプリロール開始の 0.5 拍以上あとなので、ここで消せば鳴らない
    edit->setClickTrackRange ({});

    if (! transport.isRecording())
    {
        restoreAfterRecording();
        return juce::Result::fail ("録音を開始できませんでした。オーディオ設定で入力デバイスを確認してください。"_ju);
    }

    // オーディオは自前で書く（再生が始まって、再生の仕組みができてから）
    startOwnRecording (dir);

    return juce::Result::ok();
}

bool EngineBridge::isRecording() const
{
    return edit->getTransport().isRecording();
}

void EngineBridge::restoreAfterRecording()
{
    if (std::exchange (inputsChangedWhileRecording, false))
        juce::MessageManager::callAsync ([this, alive = std::weak_ptr<bool> (aliveFlag)]
        {
            if (! alive.expired())
                configureInputs();
        });

    if (countIn != nullptr)
        countIn->setClicks ({}, metronomeVolumeDb);

    edit->setCountInMode (te::Edit::CountIn::none);
    edit->tempoSequence.getTimeSig (0)->setStringTimeSig ("4/4");
}

void EngineBridge::recordingStopped (te::SyncPoint, bool)
{
    juce::MessageManager::callAsync ([this, alive = std::weak_ptr<bool> (aliveFlag)]
    {
        if (alive.expired())
            return;

        restoreAfterRecording();
        deliverRecordings();
    });
}

void EngineBridge::startOwnRecording (const juce::File& dir)
{
    finishOwnRecording();
    auto* device = engine.getDeviceManager().deviceManager.getCurrentAudioDevice();
    auto* context = edit->getCurrentPlaybackContext();

    if (device == nullptr || context == nullptr)
        return;

    auto rec = std::make_unique<InputMeter::Recorder>();
    rec->playHead = context->getNodePlayHead();
    rec->sampleRate = device->getCurrentSampleRate();

    // ドライバが報告する入力・出力の遅れと、エンジン内の遅れ（プラグインの遅れの補正）。手動の補正は正の値で前へ
    rec->latency = device->getInputLatencyInSamples() + device->getOutputLatencyInSamples() + context->getLatencySamples() + manualLatencySamples;
    midiOutputLatencySeconds = device->getOutputLatencyInSamples() / juce::jmax (1.0, rec->sampleRate);

    juce::WavAudioFormat wav;
    int index = 0;

    for (auto& [trackId, input] : trackInputs)
    {
        auto* t = document.getProject().findTrack (trackId);

        if (! input.armed || input.device.isEmpty() || t == nullptr || t->type != collab::TrackType::audio || bindings.count (trackId) == 0)
            continue;

        auto take = std::make_unique<InputMeter::Take>();
        take->trackId = trackId;
        take->channels = { activeInputIndex (input.device), input.deviceRight.isNotEmpty() ? activeInputIndex (input.deviceRight) : -1 };
        take->numChannels = input.deviceRight.isNotEmpty() ? 2 : 1;
        take->file = dir.getChildFile ("rec-" + juce::String (++index) + "-" + juce::Uuid().toString() + ".wav");

        if (auto stream = take->file.createOutputStream())
        {
            if (auto* writer = wav.createWriterFor (stream.get(), rec->sampleRate, (unsigned int) take->numChannels, 32, {}, 0))
            {
                stream.release();
                take->writer = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer, takeWriterThread, 1 << 18);
            }
        }

        if (take->writer != nullptr)
            rec->takes.push_back (std::move (take));
    }

    if (rec->takes.empty())
        return;

    takeWriterThread.startThread();
    recorder = std::move (rec);
    const juce::SpinLock::ScopedLockType sl (inputMeter.recorderLock);
    inputMeter.recorder = recorder.get();
}

void EngineBridge::finishOwnRecording()
{
    {
        const juce::SpinLock::ScopedLockType sl (inputMeter.recorderLock);
        inputMeter.recorder = nullptr;
    }

    if (recorder == nullptr)
        return;

    for (auto& take : recorder->takes)
    {
        take->writer.reset();   // 残りを書き出して閉じる

        if (take->written <= 0 || take->numAnchors == 0)
        {
            take->file.deleteFile();
            continue;
        }

        std::vector<juce::int64> anchors (take->anchors.begin(), take->anchors.begin() + take->numAnchors);
        std::nth_element (anchors.begin(), anchors.begin() + (long) anchors.size() / 2, anchors.end());
        const auto startSample = anchors[anchors.size() / 2] - recorder->latency;

        RecordedTake t;
        t.trackId = take->trackId;
        t.file = take->file;
        t.startSeconds = (double) startSample / recorder->sampleRate;
        t.offsetSeconds = 0.0;
        t.lengthSeconds = (double) take->written / recorder->sampleRate;
        t.punchInSeconds = punchInSeconds;
        pendingTakes.push_back (t);
    }

    recorder.reset();
}

void EngineBridge::deliverRecordings()
{
    // Tracktion は録音の始めにも（クリップなしで）recordingFinished を呼ぶので、録音中はまだ閉じない
    if (! edit->getTransport().isRecording())
        finishOwnRecording();

    if (! pendingMidi.empty())
    {
        auto midi = std::move (pendingMidi);
        pendingMidi.clear();

        if (onMidiRecorded)
            onMidiRecorded (std::move (midi));
    }

    if (pendingTakes.empty())
        return;

    auto takes = std::move (pendingTakes);
    pendingTakes.clear();

    if (onRecordingFinished)
        onRecordingFinished (std::move (takes));
}

void EngineBridge::recordingFinished (te::InputDeviceInstance& input, te::EditItemID targetID,
                                      const juce::ReferenceCountedArray<te::Clip>& recordedClips)
{
    std::string trackId;

    for (auto& [id, b] : bindings)
        if (b.track != nullptr && b.track->itemID == targetID)
            trackId = id;

    for (auto& clip : recordedClips)
    {
        // MIDI: Edit は 60BPM（1 拍 = 1 秒）なので、拍 = 秒としてプロジェクトの tick に直す
        if (auto* midi = dynamic_cast<te::MidiClip*> (clip); midi != nullptr && ! trackId.empty())
        {
            const auto& map = document.getTempoMap();
            const double clipStart = midi->getPosition().getStart().inSeconds();
            const double offset = midi->getPosition().getOffset().inSeconds();
            RecordedMidi rec;
            rec.trackId = trackId;
            rec.punchInTick = (collab::Tick) std::llround (map.secondsToTick (juce::jmax (0.0, punchInSeconds)));

            if (juce::SystemStats::getEnvironmentVariable ("SHAREDAW_LOOPBACK_LOG", {}).isNotEmpty())
                std::cerr << "tracktion midi clip: start " << clipStart << " offset " << offset << " length " << midi->getPosition().getLength().inSeconds()
                          << " notes " << midi->getSequence().getNotes().size() << " punch " << punchInSeconds << std::endl;

            for (auto* n : midi->getSequence().getNotes())
            {
                // 聞こえていた音は出力の遅れの分だけ後なので、その分だけ前に戻す（エンジン内の遅れは Tracktion が戻している）
                const double start = clipStart + n->getStartBeat().inBeats() - offset - midiOutputLatencySeconds;
                const double end = start + n->getLengthBeats().inBeats();

                if (start < punchInSeconds - 0.05)   // カウントイン中の音は捨てる
                    continue;

                collab::Note note;
                note.id = collab::generateUuid();
                note.tick = (collab::Tick) std::llround (map.secondsToTick (juce::jmax (0.0, start)));
                note.lengthTick = std::max<collab::Tick> (10, (collab::Tick) std::llround (map.secondsToTick (end)) - note.tick);
                note.pitch = n->getNoteNumber();
                note.velocity = juce::jlimit (1, 127, n->getVelocity());
                rec.notes.push_back (note);
            }

            if (! rec.notes.empty())
                pendingMidi.push_back (std::move (rec));
        }

        // オーディオは自前で書いたもの（InputMeter::Recorder）を使う。Tracktion が書いたファイルは捨てる
        if (auto* wave = dynamic_cast<te::WaveAudioClip*> (clip))
            wave->getAudioFile().getFile().deleteFile();

        // Edit には JSON から作り直したクリップだけを置く
        clip->removeFromParent();
    }

    // 入力ごとに呼ばれるので、まとめてから知らせる
    juce::MessageManager::callAsync ([this, alive = std::weak_ptr<bool> (aliveFlag)]
    {
        if (! alive.expired())
            deliverRecordings();
    });
}

//==============================================================================
int EngineBridge::activeInputIndex (const juce::String& waveInputName) const
{
    // 入力（Tracktion の WaveInputDevice）→ オーディオ機器のチャンネル → InputMeter の番号（有効なチャンネルだけを数えた順）
    auto& dm = engine.getDeviceManager();
    auto* device = dm.deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
        return -1;

    for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
    {
        auto* w = dm.getWaveInDevice (i);

        if (w == nullptr || w->getName() != waveInputName || w->getChannels().empty())
            continue;

        const int channel = w->getChannels().front().indexInDevice;
        const auto active = device->getActiveInputChannels();

        if (channel < 0 || ! active[channel])
            return -1;

        int index = 0;

        for (int ch = 0; ch < channel; ++ch)
            if (active[ch])
                ++index;

        return index < InputMeter::maxChannels ? index : -1;
    }

    return -1;
}

void EngineBridge::pollRecording()
{
    if (! isRecording())
    {
        liveRecordings.clear();

        for (auto& p : inputMeter.recordingPeaks)
            p.store (0.0f, std::memory_order_relaxed);

        return;
    }

    const double now = getPositionSeconds();
    std::map<int, float> channelPeaks;   // 同じ入力を 2 つのトラックで録っていても、読むのは 1 回

    auto peakOf = [&] (const juce::String& name)
    {
        const int index = name.isNotEmpty() ? activeInputIndex (name) : -1;

        if (index < 0)
            return 0.0f;

        if (auto it = channelPeaks.find (index); it != channelPeaks.end())
            return it->second;

        return channelPeaks[index] = inputMeter.recordingPeaks[(size_t) index].exchange (0.0f, std::memory_order_relaxed);
    };

    for (auto& [trackId, input] : trackInputs)
    {
        if (! input.armed || input.device.isEmpty())
            continue;

        auto* t = document.getProject().findTrack (trackId);

        if (t == nullptr || t->type != collab::TrackType::audio)
            continue;

        const float peak = juce::jmax (peakOf (input.device), peakOf (input.deviceRight));

        // カウントイン中は描かない（録音はまだ始まっていない）
        if (now < punchInSeconds)
            continue;

        auto& live = liveRecordings[trackId];
        live.startSeconds = punchInSeconds;
        live.peaks.emplace_back (now, peak);
    }
}

void EngineBridge::midiKeyStateChanged (te::AudioTrack* track, const juce::Array<int>& notesOn,
                                        const juce::Array<int>& velocities, const juce::Array<int>& notesOff)
{
    // Tracktion がメッセージスレッドで少し遅れて（25〜50 ms）知らせてくる。画面に出すだけなのでこれで足りる
    if (track == nullptr || ! isRecording())
        return;

    std::string trackId;

    for (auto& [id, b] : bindings)
        if (b.track.get() == track)
            trackId = id;

    const double now = getPositionSeconds();

    if (trackId.empty() || now < punchInSeconds)
        return;

    auto& live = liveRecordings[trackId];
    live.startSeconds = punchInSeconds;

    for (int pitch : notesOff)
        for (auto it = live.notes.rbegin(); it != live.notes.rend(); ++it)
            if (it->pitch == pitch && it->end < 0)
            {
                it->end = now;
                break;
            }

    for (int i = 0; i < notesOn.size(); ++i)
        live.notes.push_back ({ now, -1.0, notesOn[i], velocities[i] });
}
