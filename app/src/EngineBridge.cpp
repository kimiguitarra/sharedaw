#include "EngineBridge.h"
#include "EngineBridgeDetail.h"
#include "collab/ClipEditing.h"

#include <sstream>

#include "SfizzPlugin.h"
#include "audio/ChannelStripPlugin.h"
#include "audio/PitchShift.h"
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

namespace
{
    /** トラックのクリップを全部消す。getClips() は消すたびに縮むので、写しを取ってから回す。 */
    void removeAllClips (te::ClipTrack& track)
    {
        const juce::Array<te::Clip*> clips (track.getClips());

        for (auto* c : clips)
            c->removeFromParent();
    }

    std::string makeTempoKey (const collab::Project& p)
    {
        std::ostringstream s;
        s.imbue (std::locale::classic());

        for (auto& e : p.tempoTrack.events)   s << e.tick << ':' << e.bpm << ';';
        s << '|';
        for (auto& e : p.meterTrack.events)   s << e.bar << ':' << e.numerator << '/' << e.denominator << ';';

        return s.str();
    }

    std::string makeClipsKey (const collab::Track& t)
    {
        std::ostringstream s;

        for (auto& c : t.midiClips)
        {
            s << c.id << '@' << c.startTick << '+' << c.lengthTick << '[';

            for (auto& n : c.notes)
                s << n.tick << ',' << n.lengthTick << ',' << n.pitch << ',' << n.velocity << ';';

            s << "]b[";

            for (auto& b : c.pitchBends)
                s << b.tick << ',' << b.value << ';';

            s << ']';
        }

        for (auto& c : t.audioClips)
            s << "A" << c.id << '@' << c.startTick << ':' << c.audioHash << ':' << c.sourceOffsetSamples << ':' << c.lengthSamples
              << ':' << c.gainDb << ':' << c.fadeInSamples << ':' << c.fadeOutSamples << ':' << c.pitchSemitones << ';';

        s << "X";

        if (t.render)
            s << "R" << t.render->audioHash;

        return s.str();
    }
}

EngineBridge::EngineBridge (te::Engine& e, ProjectDocument& doc, const InstrumentLibrary& lib)
    : engine (e), document (doc), library (lib)
{
    edit = std::make_unique<te::Edit> (engine, te::Edit::forEditing);
    midiKeyDispatcher->listeners.add (this);

    // 既定で作られるトラックは使わない
    for (auto t : te::getAudioTracks (*edit))
        edit->deleteTrack (t);

    auto& ts = edit->tempoSequence;
    ts.getTempo (0)->setBpm (60.0);
    ts.getTimeSig (0)->setStringTimeSig ("4/4");
    edit->clickTrackEnabled = false;

    // 入力デバイスの一覧は非同期に作られるので、変わるたびに設定する
    engine.getDeviceManager().addChangeListener (this);
    engine.getDeviceManager().deviceManager.addAudioCallback (&inputMeter);
    configureInputs();

    // ミックスバス: トラック・コード・バスの出力はここへ集まり、リミッターを通ってからマスターへ出る
    mixTrack = createTrack();

    if (mixTrack != nullptr)
    {
        mixTrack->setName ("Mix Bus");
        // ソロ・アイソレートにはしない（Tracktion では出力先のアイソレートが送り元の全トラックに伝わり、ソロが効かなくなる）。
        // ミックスバスは、ソロのトラックが入ってくれば鳴る（AudioTrack::isTrackAudible）

        if (auto plugin = edit->getPluginCache().createNewPlugin (MasterLimiterPlugin::xmlTypeName, {}))
        {
            mixTrack->pluginList.insertPlugin (plugin, 0, nullptr);
            masterLimiter = dynamic_cast<MasterLimiterPlugin*> (plugin.get());
        }
    }

    // カウントインのクリック（マスターの最後）
    if (auto plugin = edit->getPluginCache().createNewPlugin (CountInPlugin::xmlTypeName, {}))
    {
        edit->getMasterPluginList().insertPlugin (plugin, -1, nullptr);
        countIn = dynamic_cast<CountInPlugin*> (plugin.get());
    }

    // マスターのメーター（マスターの最後。マスター音量の前なので、表示するときに音量を足す）
    if (auto plugin = edit->getPluginCache().createNewPlugin (te::LevelMeterPlugin::xmlTypeName, {}))
    {
        edit->getMasterPluginList().insertPlugin (plugin, -1, nullptr);

        if (auto* meterPlugin = dynamic_cast<te::LevelMeterPlugin*> (plugin.get()))
        {
            masterMeter = std::make_unique<Meter>();
            masterMeter->attach (meterPlugin->measurer);
        }
    }

    edit->getTransport().ensureContextAllocated();
    edit->getTransport().addListener (this);

    document.addChangeListener (this);
    sync();
}

EngineBridge::~EngineBridge()
{
    aliveFlag.reset();
    pitchPool.removeAllJobs (true, 60000);
    midiKeyDispatcher->listeners.remove (this);
    finishOwnRecording();
    takeWriterThread.stopThread (2000);

    for (auto& [id, b] : bindings)
        b.meter.reset();

    chordMeter.reset();
    metronomeMeter.reset();
    masterMeter.reset();
    midiInputs.clear();
    engine.getDeviceManager().deviceManager.removeAudioCallback (&inputMeter);
    document.removeChangeListener (this);
    engine.getDeviceManager().removeChangeListener (this);
    edit->getTransport().removeListener (this);
    edit->getTransport().stop (false, true);
}

void EngineBridge::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &engine.getDeviceManager())
        configureInputs();
    else
        sync();
}

void EngineBridge::configureInputs()
{
    // 録音中に入力の設定を変えると、Tracktion はその入力の録音を止めてしまう（MIDI のノートが途中から消える）。
    // 機器の一覧の更新（MIDI 機器の再検出など）は録音中にも届くので、録音が終わってからまとめて行う
    if (edit->getTransport().isRecording())
    {
        inputsChangedWhileRecording = true;
        return;
    }

    auto& dm = engine.getDeviceManager();

    // 入力はモノラルのチャンネルごとに扱う（ギター・マイクを 1 本ずつ録る想定）
    for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
        if (auto* w = dm.getWaveInDevice (i); w != nullptr && w->getChannels().size() == 2)
            return w->setStereoPair (false);   // 一覧が作り直されるので、次の変更通知で続ける

    for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
        if (auto* w = dm.getWaveInDevice (i); w != nullptr && ! w->isEnabled())
            w->setEnabled (true);

    setManualLatencySamples (manualLatencySamples);
    refreshMidiInputs();
    applyInputs();
}

//==============================================================================
void EngineBridge::setMidiRecordOffsetMs (double msEarlier)
{
    midiRecordOffsetMs = msEarlier;

    for (auto& in : midiInputs)
        in->device->setManualAdjustmentMs (-msEarlier);
}

void EngineBridge::refreshMidiInputs()
{
    // 実際の機器だけ（Tracktion の仮想デバイス「All MIDI Ins」などは除く）
    auto devices = engine.getDeviceManager().getMidiInDevices();
    devices.erase (std::remove_if (devices.begin(), devices.end(),
                                   [] (auto& d) { return d == nullptr || d->getDeviceType() != te::InputDevice::physicalMidiDevice; }),
                   devices.end());

    // 一覧が変わっていなければそのまま（メーターの値を保つ）
    bool same = devices.size() == midiInputs.size();

    for (size_t i = 0; same && i < devices.size(); ++i)
        same = devices[i] == midiInputs[i]->device;

    if (same)
        return;

    midiInputs.clear();

    for (auto& d : devices)
    {
        if (d == nullptr)
            continue;

        d->setManualAdjustmentMs (-midiRecordOffsetMs);   // Tracktion は正の値で後ろへずらす
        auto in = std::make_unique<MidiIn>();
        in->device = d;
        in->client = std::make_unique<te::LevelMeasurer::Client>();
        d->levelMeasurer.addClient (*in->client);
        midiInputs.push_back (std::move (in));
    }
}

juce::String EngineBridge::getTrackMidiInput (const std::string& trackId) const
{
    auto it = trackMidiInputs.find (trackId);
    return it != trackMidiInputs.end() ? it->second : juce::String();
}

void EngineBridge::setTrackMidiInput (const std::string& trackId, const juce::String& device)
{
    trackMidiInputs[trackId] = device;
    applyInputs();
}

std::vector<EngineBridge::MidiInputStatus> EngineBridge::getMidiInputs() const
{
    std::vector<MidiInputStatus> result;

    for (auto& in : midiInputs)
        result.push_back ({ in->device->getName(), in->device->isEnabled(), in->activity });

    return result;
}

void EngineBridge::setMidiInputEnabled (const juce::String& name, bool enabled)
{
    // 有効・無効は Tracktion の設定に保存され、次回の起動でも引き継がれる
    for (auto& in : midiInputs)
        if (in->device->getName() == name && in->device->isEnabled() != enabled)
            in->device->setEnabled (enabled);

    applyInputs();
}

void EngineBridge::pollMidiActivity()
{
    for (auto& in : midiInputs)
    {
        const float db = in->client->getAndClearMidiLevel().dB;
        const float level = db > -99.0f ? juce::Decibels::decibelsToGain (db) : 0.0f;
        in->activity = juce::jmax (level, in->activity * 0.85f);
    }
}

void EngineBridge::setMidiTarget (const std::string& trackId)
{
    if (trackId == midiTargetId)
        return;

    midiTargetId = trackId;
    applyInputs();
}

void EngineBridge::routeToMix (te::AudioTrack& track)
{
    if (mixTrack != nullptr && &track != mixTrack.get())
        track.getOutput().setOutputToTrack (mixTrack.get());
    else
        track.getOutput().setOutputToDefaultDevice (false);
}

te::AudioTrack::Ptr EngineBridge::createTrack()
{
    auto tracks = te::getAudioTracks (*edit);
    auto t = edit->insertNewAudioTrack (te::TrackInsertPoint (nullptr, tracks.isEmpty() ? nullptr : tracks.getLast()), nullptr);
    return t;
}

SfizzPlugin* EngineBridge::addSynth (te::AudioTrack& track)
{
    auto plugin = edit->getPluginCache().createNewPlugin (SfizzPlugin::xmlTypeName, {});
    track.pluginList.insertPlugin (plugin, 0, nullptr);
    return dynamic_cast<SfizzPlugin*> (plugin.get());
}

//==============================================================================
void EngineBridge::sync()
{
    const auto& project = document.getProject();
    const auto newTempoKey = makeTempoKey (project);
    const bool tempoChanged = newTempoKey != tempoKey;

    // テンポが変わっても、再生位置は小節上の位置（tick）を保つ
    const double positionTick = tempoChanged && ! tempoKey.empty() ? getPositionTick() : -1.0;
    tempoKey = newTempoKey;

    if (tempoChanged)
        hostTempo->setTempoMap (document.getTempoMap());

    // 削除されたトラック
    for (auto it = bindings.begin(); it != bindings.end();)
    {
        if (project.findTrack (it->first) == nullptr)
        {
            removeInstrument (it->second);
            removeEffects (it->second);
            it->second.meter.reset();   // メーターを外してからトラックを消す

            // このトラック（バス）へ出力しているトラックはマスターへ戻す
            for (auto& [otherId, other] : bindings)
                if (other.track != nullptr && other.track != it->second.track
                    && other.track->getOutput().getDestinationTrack() == it->second.track.get())
                    routeToMix (*other.track);

            edit->deleteTrack (it->second.track.get());
            it = bindings.erase (it);
        }
        else
        {
            ++it;
        }
    }

    // 追加・変更されたトラック

    for (auto& t : project.tracks)
    {
        auto& b = bindings[t.id];

        if (b.track == nullptr)
        {
            b.track = createTrack();

            if (b.track != nullptr)
            {
                b.meter = std::make_unique<Meter>();
                b.meter->attach (*b.track);
            }
        }

        if (b.track == nullptr)
            continue;

        syncTrack (t, b, tempoChanged);
    }

    syncRouting (project);

    // MIDI キーボードは全部の MIDI トラックにつなぐので、MIDI トラック（音源）が増減・変わったらつなぎ直す
    {
        juce::String key;

        for (auto& [id, b] : bindings)
            if (b.track != nullptr && ! b.renderMode && (b.synth != nullptr || b.externalInstrument != nullptr))
                key << juce::String (id) << ":" << juce::String::toHexString ((juce::pointer_sized_int) b.track.get())
                    << ":" << juce::String::toHexString ((juce::pointer_sized_int) (b.synth != nullptr ? (void*) b.synth : (void*) b.externalInstrument.get())) << ";";

        if (key != liveMidiTracksKey)
        {
            liveMidiTracksKey = key;
            applyInputs();
        }
    }

    if (masterLimiter != nullptr)
        masterLimiter->setLimiter (project.master.limiter);

    // マスターのエフェクト（リミッターの前）。この PC にない外部プラグインは飛ばす
    if (mixTrack != nullptr)
    {
        masterEffects.track = mixTrack;
        syncEffects (project.master.effects, masterEffects, masterLimiter);
    }
    syncChordTrack (tempoChanged);
    syncMetronome (tempoChanged);

    if (tempoChanged)
    {
        applyLoop();

        if (positionTick >= 0)
            setPositionTick (positionTick);
    }
}

void EngineBridge::syncAutomation (const collab::Track& t, Binding& b, bool tempoChanged)
{
    auto* vol = b.track->getVolumePlugin();

    if (vol == nullptr)
        return;

    std::string key;

    for (auto& lane : t.automation)
    {
        key += lane.param + ":";

        for (auto& p : lane.points)
            key += std::to_string (p.tick) + "=" + std::to_string (p.value) + ",";
    }

    if (key == b.automationKey && ! tempoChanged)
        return;

    const bool hadAutomation = ! b.automationKey.empty();
    b.automationKey = key;
    const auto& map = document.getTempoMap();

    // 点は曲の秒の位置に置き、点の間は直線（Tracktion の値は、音量はフェーダーの位置 0〜1、パンは -1〜1）
    auto apply = [&] (te::AutomatableParameter& param, const collab::AutomationLane* lane, auto toParam)
    {
        auto& curve = param.getCurve();
        curve.clear();

        if (lane != nullptr)
        {
            // 画面のとおり、点の間はこちらの値（dB など）で直線にする。Tracktion はパラメーターの値（フェーダーの位置）で直線に
            // つなぐので、間に細かく点を足す（50 ms ごと）
            for (size_t i = 0; i < lane->points.size(); ++i)
            {
                const auto& p = lane->points[i];
                const double t0 = juce::jmax (0.0, map.tickToSeconds ((double) p.tick));
                curve.addPoint (te::TimePosition::fromSeconds (t0), toParam (p.value), 0.0f);

                if (i + 1 < lane->points.size())
                {
                    const auto& q = lane->points[i + 1];
                    const double t1 = juce::jmax (0.0, map.tickToSeconds ((double) q.tick));
                    const int steps = juce::jlimit (0, 400, (int) ((t1 - t0) / 0.05));

                    if (std::abs (q.value - p.value) > 1e-9)
                        for (int k = 1; k < steps; ++k)
                        {
                            const double f = (double) k / (double) steps;
                            curve.addPoint (te::TimePosition::fromSeconds (t0 + (t1 - t0) * f), toParam (p.value + (q.value - p.value) * f), 0.0f);
                        }
                }
            }
        }

        // Tracktion は少し後（タイマー）で再生用の値を作り直すので、すぐに作る（書き出しなどがすぐ後に続くことがある）
        param.updateStream();
    };

    apply (*vol->volParam, t.findAutomation ("volume"), [] (double db) { return te::decibelsToVolumeFaderPosition ((float) db); });
    apply (*vol->panParam, t.findAutomation ("pan"), [] (double pan) { return (float) juce::jlimit (-1.0, 1.0, pan); });

    // オートメーションを消したら、ミキサーの値に戻す（最後に鳴っていた値のままにしない）
    if (hadAutomation)
    {
        vol->setVolumeDb ((float) t.volumeDb);
        vol->setPan ((float) t.pan);
    }
}

void EngineBridge::syncTrack (const collab::Track& t, Binding& b, bool tempoChanged)
{
    auto& track = *b.track;

    if (track.getName() != toJuce (t.name))
        track.setName (toJuce (t.name));

    if (auto vol = track.getVolumePlugin())
    {
        if (std::abs (vol->getVolumeDb() - (float) t.volumeDb) > 0.001f)
            vol->setVolumeDb ((float) t.volumeDb);

        if (std::abs (vol->getPan() - (float) t.pan) > 0.001f)
            vol->setPan ((float) t.pan);
    }

    syncAutomation (t, b, tempoChanged);

    // 外部プラグインのトラックをバウンスしたトラックは、持ち主の PC では鳴らさない（元のトラックをそのまま鳴らす）
    if (const bool mute = t.mute || collab::isHiddenBounceTrack (document.getProject(), t); track.isMuted (false) != mute)
        track.setMute (mute);

    if (track.isSolo (false) != t.solo)
        track.setSolo (t.solo);

    // 外部プラグインを使うトラックは、この環境でプラグインを鳴らせなければバウンスした音で再生する（§3.7）
    juce::String liveProblem;
    const bool external = collab::usesExternalPlugin (t);
    const bool renderMode = external && ! canPlayLive (t, liveProblem);
    b.renderMode = renderMode;
    syncStrip (t, b);

    if (renderMode)
    {
        removeInstrument (b);
        removeEffects (b);
        b.problem = t.render ? "プラグインを鳴らせないため、バウンスした音で再生しています（"_ju + liveProblem + "）"_ju
                             : "プラグインを鳴らせず、バウンスもありません（"_ju + liveProblem + "）"_ju;
    }
    else
    {
        b.problem = {};

        if (t.type == collab::TrackType::midi)
            syncInstrument (t, b);

        syncEffects (t, b);
    }

    // クリップ（tick → 秒 → Tracktion の拍）
    auto key = makeClipsKey (t) + (renderMode ? "#render" : "");

    if (key == b.clipsKey && ! tempoChanged)
        return;

    b.clipsKey = key;
    b.clipsRebuiltAt = juce::Time::getMillisecondCounter();

    // getClips() は消すと縮む本物の一覧なので、写しを取ってから消す（そのまま回すと 1 つおきに消し残し、
    // 画面にない古いクリップが鳴っていた）
    removeAllClips (track);

    const auto& map = document.getTempoMap();
    b.missingAudio = 0;

    if (renderMode)
    {
        if (! t.render)
            return;

        auto file = AudioFiles::fileForHash (document.getProjectDir(), t.render->audioHash);

        if (! document.hasLocation() || ! file.existsAsFile())
        {
            b.missingAudio = 1;
            return;
        }

        // バウンスは曲の先頭から書き出しているので、0 秒に置く
        te::AudioFile audioFile (engine, file);
        const te::ClipPosition pos { te::TimeRange (secondsToTime (0), te::TimeDuration::fromSeconds (audioFile.getLength())), {} };

        if (auto clip = track.insertWaveClip ("render", file, pos, false))
        {
            clip->setAutoTempo (false);
            clip->setAutoPitch (false);
        }

        return;
    }

    // オーディオクリップ（非破壊: 実体は audio/<hash>.wav、クリップはオフセット・長さ・音量・フェードの参照）
    // 重なっているときは Pro Tools と同じく新しい（後ろの）クリップだけが鳴る
    for (size_t i = 0; i < t.audioClips.size(); ++i)
    {
        auto& c = t.audioClips[i];
        auto file = AudioFiles::fileForHash (document.getProjectDir(), c.audioHash);

        if (! document.hasLocation() || ! file.existsAsFile())
            ++b.missingAudio;
    }

    // テイクの切り替わり・くっついたつなぎ目は、5 ms 重ねて直線のクロスフェードで入れ替える（調整はしない）
    const auto shape = te::AudioFadeCurve::linear;

    const auto sourceSeconds = [this] (const collab::AudioClip& c)
    {
        const auto file = AudioFiles::fileForHash (document.getProjectDir(), c.audioHash);
        return file.existsAsFile() ? te::AudioFile (engine, file).getLength() : -1.0;
    };

    for (auto seg : collab::audibleSegments (t.audioClips, map, 0.005, sourceSeconds))
    {
        auto& c = t.audioClips[seg.clipIndex];
        auto file = AudioFiles::fileForHash (document.getProjectDir(), c.audioHash);

        if (! document.hasLocation() || ! file.existsAsFile())
            continue;

        // ピッチを変えたクリップは、高さを変えたファイルを鳴らす（できるまでは元の音。できたら作り直す）
        if (c.pitchSemitones != 0.0)
            if (auto pitched = pitchedFile (c, false); pitched.existsAsFile())
                file = pitched;

        // くっついたクリップのクロスフェードで延ばした分が、元ファイルの終わりを超えるときは切る
        if (const double fileSeconds = te::AudioFile (engine, file).getLength(); fileSeconds > 0.0
             && seg.offsetSeconds + seg.lengthSeconds > fileSeconds)
        {
            const double over = seg.offsetSeconds + seg.lengthSeconds - fileSeconds;
            seg.lengthSeconds = juce::jmax (0.001, seg.lengthSeconds - over);
            seg.fadeOutSeconds = juce::jmax (0.0, juce::jmin (seg.fadeOutSeconds - over, seg.lengthSeconds - seg.fadeInSeconds));
        }

        const te::ClipPosition pos { te::TimeRange (secondsToTime (seg.startSeconds), te::TimeDuration::fromSeconds (seg.lengthSeconds)),
                                     te::TimeDuration::fromSeconds (seg.offsetSeconds) };

        if (auto clip = track.insertWaveClip (toJuce (c.displayName), file, pos, false))
        {
            clip->setAutoTempo (false);
            clip->setAutoPitch (false);
            clip->setGainDB ((float) c.gainDb);


            clip->setFadeIn (te::TimeDuration::fromSeconds (seg.fadeInSeconds));
            clip->setFadeOut (te::TimeDuration::fromSeconds (seg.fadeOutSeconds));

            if (seg.crossfadeIn)
                clip->setFadeInType (shape);

            if (seg.crossfadeOut)
                clip->setFadeOutType (shape);
        }
    }

    for (auto& c : t.midiClips)
    {
        const double start = map.tickToSeconds ((double) c.startTick);
        const double end = map.tickToSeconds ((double) c.endTick());

        auto clip = track.insertMIDIClip (toJuce (c.id), te::TimeRange (secondsToTime (start), secondsToTime (end)), nullptr);

        if (clip == nullptr)
            continue;

        auto& seq = clip->getSequence();

        for (auto& n : c.notes)
        {
            const double ns = map.tickToSeconds ((double) (c.startTick + n.tick)) - start;
            const double ne = map.tickToSeconds ((double) (c.startTick + n.tick + n.lengthTick)) - start;

            if (ns < 0.0 || ns >= end - start)
                continue;

            seq.addNote (n.pitch, secondsToBeats (ns), te::BeatDuration::fromBeats (juce::jmax (0.001, ne - ns)),
                         n.velocity, 0, nullptr);
        }

        // ピッチベンド（Tracktion の値は 0〜16383、中央 8192）。クリップの頭と終わりは中央にして、前後の音に残さない
        if (! c.pitchBends.empty())
        {
            auto addBend = [&] (double seconds, int value)
            {
                seq.addControllerEvent (secondsToBeats (juce::jlimit (0.0, end - start, seconds)), te::MidiControllerEvent::pitchWheelType,
                                        juce::jlimit (0, 16383, value + 8192), nullptr);
            };

            if (c.pitchBends.front().tick > 0)
                addBend (0.0, 0);

            // 点の間の直線は、細かいイベントで埋めて鳴らす
            for (auto& b : collab::densePitchBends (c.pitchBends))
                if (b.tick >= 0 && b.tick < c.lengthTick)
                    addBend (map.tickToSeconds ((double) (c.startTick + b.tick)) - start, b.value);

            if (collab::pitchBendAt (c.pitchBends, c.lengthTick - 1) != 0)
                addBend (end - start - 0.0005, 0);
        }
    }
}

//==============================================================================
bool EngineBridge::canPlayLive (const collab::Track& t, juce::String& why) const
{
    auto check = [&] (const collab::ExternalPlugin& plugin, const std::string& stateRef)
    {
        if (! PluginHost::find (engine, plugin))
        {
            why = "プラグイン「"_ju + toJuce (plugin.name) + "」が見つかりません"_ju;
            return false;
        }

        // 状態ファイルは持ち主の環境にだけある（§7.2）。他の人の環境ではバウンスした音で再生する。
        // バウンスがまだなければ、追加したばかり（状態ファイルは保存時に書かれる）なので鳴らす。
        if (! stateRef.empty() && t.render && ! PluginHost::stateFile (document.getProjectDir(), stateRef).existsAsFile())
        {
            why = "他の人のプラグイン設定です"_ju;
            return false;
        }

        return true;
    };

    if (t.instrument && t.instrument->kind == collab::Instrument::Kind::external)
        if (! check (t.instrument->plugin, t.instrument->stateRef))
            return false;

    for (auto& e : t.effects)
        if (! e.bypass && ! e.isBuiltin() && ! check (e.plugin, e.stateRef))
            return false;

    return true;
}

te::Plugin::Ptr EngineBridge::createExternal (const collab::ExternalPlugin& plugin, const std::string& stateRef)
{
    auto desc = PluginHost::find (engine, plugin);

    if (! desc)
        return {};

    auto p = edit->getPluginCache().createNewPlugin (HostSyncedExternalPlugin::create (engine, *desc));

    if (auto* synced = dynamic_cast<HostSyncedExternalPlugin*> (p.get()))
        synced->setHostTempo (hostTempo);

    if (auto* ext = dynamic_cast<te::ExternalPlugin*> (p.get()))
    {
        juce::MemoryBlock state;

        if (auto* instance = ext->getAudioPluginInstance();
            instance != nullptr && ! stateRef.empty()
             && PluginHost::stateFile (document.getProjectDir(), stateRef).loadFileAsData (state) && state.getSize() > 0)
            instance->setStateInformation (state.getData(), (int) state.getSize());
    }

    return p;
}

void EngineBridge::removeInstrument (Binding& b)
{
    if (b.synth != nullptr)
    {
        b.synth->deleteFromParent();
        b.synth = nullptr;
        b.sfzText = {};
    }

    if (b.externalInstrument != nullptr)
    {
        if (onPluginRemoved) onPluginRemoved (b.externalInstrument.get());
        b.externalInstrument->deleteFromParent();
        b.externalInstrument = nullptr;
        b.instrumentKey = {};
    }
}

void EngineBridge::removeEffects (Binding& b)
{
    for (auto& e : b.effects)
    {
        if (onPluginRemoved) onPluginRemoved (e.plugin.get());
        e.plugin->deleteFromParent();
    }

    b.effects.clear();
    b.effectsKey = {};
}

void EngineBridge::syncEffects (const collab::Track& t, Binding& b)
{
    // チャンネルストリップ（なければ音量・パン）の直前に並べる
    syncEffects (t.effects, b, b.strip != nullptr ? static_cast<te::Plugin*> (b.strip) : b.track->getVolumePlugin());
}

void EngineBridge::syncEffects (const std::vector<collab::Effect>& effects, Binding& b, te::Plugin* before)
{
    std::string key;

    for (auto& e : effects)
        key += e.id + "|" + e.builtin + "|" + e.plugin.uid + "|" + e.stateRef + ";";

    if (key != b.effectsKey)
    {
        removeEffects (b);
        b.effectsKey = key;

        int index = before != nullptr ? b.track->pluginList.indexOf (before) : -1;

        for (auto& e : effects)
        {
            te::Plugin::Ptr p;

            if (e.isBuiltin())
            {
                if (collab::fx::typeFromId (e.builtin))
                    p = edit->getPluginCache().createNewPlugin (BuiltinEffectPlugin::xmlTypeName, {});
            }
            else
            {
                p = createExternal (e.plugin, e.stateRef);
            }

            if (p != nullptr)
            {
                b.track->pluginList.insertPlugin (p, index < 0 ? -1 : index++, nullptr);
                b.effects.push_back ({ e.id, e.stateRef, p });
            }
        }
    }

    // バイパス（エフェクトを通さない）と、内蔵エフェクトの値
    for (auto& e : effects)
        for (auto& be : b.effects)
            if (be.id == e.id)
            {
                if (be.plugin->isEnabled() == e.bypass)
                    be.plugin->setEnabled (! e.bypass);

                if (auto* fx = dynamic_cast<BuiltinEffectPlugin*> (be.plugin.get()))
                    if (auto type = collab::fx::typeFromId (e.builtin))
                        fx->setEffect (*type, e.params);
            }
}

void EngineBridge::syncRouting (const collab::Project& project)
{
    // バスの番号（Tracktion の Aux Send / Return の番号）= プロジェクトのバストラックの順番
    std::map<std::string, int> busNumbers;

    for (auto& t : project.tracks)
        if (t.type == collab::TrackType::bus && (int) busNumbers.size() < 32)
            busNumbers.emplace (t.id, (int) busNumbers.size());

    // 出力先をたどってループにならないか（なるならマスターへ）
    auto resolvesWithoutLoop = [&] (const collab::Track& start)
    {
        std::set<std::string> seen { start.id };
        const collab::Track* t = &start;

        while (t != nullptr && ! t->output.empty())
        {
            if (! seen.insert (t->output).second)
                return false;

            t = project.findTrack (t->output);
        }

        return true;
    };

    for (auto& t : project.tracks)
    {
        auto it = bindings.find (t.id);

        if (it == bindings.end() || it->second.track == nullptr)
            continue;

        auto& b = it->second;

        // 出力先
        te::AudioTrack* dest = nullptr;

        if (auto d = bindings.find (t.output); ! t.output.empty() && d != bindings.end() && busNumbers.count (t.output) > 0
                                               && resolvesWithoutLoop (t))
            dest = d->second.track.get();

        if (dest == nullptr)
            dest = mixTrack.get();

        if (b.track->getOutput().getDestinationTrack() != dest)
        {
            if (dest != nullptr)
                b.track->getOutput().setOutputToTrack (dest);
            else
                b.track->getOutput().setOutputToDefaultDevice (false);
        }

        // バスはセンドを受ける Aux Return を先頭に持つ
        if (auto n = busNumbers.find (t.id); n != busNumbers.end())
        {
            if (b.auxReturn == nullptr)
            {
                b.auxReturn = edit->getPluginCache().createNewPlugin (te::AuxReturnPlugin::xmlTypeName, {});
                b.track->pluginList.insertPlugin (b.auxReturn, 0, nullptr);
            }

            if (auto* ret = dynamic_cast<te::AuxReturnPlugin*> (b.auxReturn.get()); ret != nullptr && ret->busNumber != n->second)
                ret->busNumber = n->second;
        }

        // センド（プリフェーダーは音量の前、ポストフェーダーは音量の後）
        std::string key;

        for (auto& s : t.sends)
            if (busNumbers.count (s.busId) > 0 && s.busId != t.id)
                key += s.busId + (s.preFader ? "|pre;" : "|post;") + std::to_string (busNumbers[s.busId]) + ";";

        if (key != b.sendsKey)
        {
            for (auto& p : b.sends)
                p->deleteFromParent();

            b.sends.clear();
            b.sendsKey = key;

            for (auto& s : t.sends)
            {
                if (busNumbers.count (s.busId) == 0 || s.busId == t.id)
                    continue;

                auto plugin = edit->getPluginCache().createNewPlugin (te::AuxSendPlugin::xmlTypeName, {});

                if (auto* send = dynamic_cast<te::AuxSendPlugin*> (plugin.get()))
                    send->busNumber = busNumbers[s.busId];

                auto* vol = b.track->getVolumePlugin();
                const int volIndex = vol != nullptr ? b.track->pluginList.indexOf (vol) : -1;
                b.track->pluginList.insertPlugin (plugin, volIndex < 0 ? -1 : (s.preFader ? volIndex : volIndex + 1), nullptr);
                b.sends.push_back (plugin);
            }
        }

        // 送る量
        size_t i = 0;

        for (auto& s : t.sends)
        {
            if (busNumbers.count (s.busId) == 0 || s.busId == t.id)
                continue;

            if (i < b.sends.size())
                if (auto* send = dynamic_cast<te::AuxSendPlugin*> (b.sends[i].get()))
                    if (std::abs (send->getGainDb() - (float) s.levelDb) > 0.01f)
                        send->setGainDb ((float) s.levelDb);

            ++i;
        }
    }
}

void EngineBridge::syncStrip (const collab::Track& t, Binding& b)
{
    // 2 つ置く: インサートの前と後（インサートはこの間に並べる）。どちらにかけるかは並べた順番で決まる
    if (b.strip == nullptr)
    {
        auto plugin = edit->getPluginCache().createNewPlugin (ChannelStripPlugin::xmlTypeName, {});
        auto* vol = b.track->getVolumePlugin();
        const int index = vol != nullptr ? b.track->pluginList.indexOf (vol) : -1;
        b.track->pluginList.insertPlugin (plugin, index, nullptr);
        b.strip = dynamic_cast<ChannelStripPlugin*> (plugin.get());
    }

    if (b.stripPre == nullptr && b.strip != nullptr && b.effects.empty())
    {
        auto plugin = edit->getPluginCache().createNewPlugin (ChannelStripPlugin::xmlTypeName, {});
        b.track->pluginList.insertPlugin (plugin, b.track->pluginList.indexOf (b.strip), nullptr);
        b.stripPre = dynamic_cast<ChannelStripPlugin*> (plugin.get());
    }

    // バウンスした音で鳴らすときは、インサートの前の分はもうバウンスに入っている
    auto before = t.strip.beforeInserts();

    if (b.renderMode)
        before.eq.enabled = before.comp.enabled = false;

    b.stripEqPre = b.stripPre != nullptr && before.eq.enabled;
    b.stripCompPre = b.stripPre != nullptr && before.comp.enabled;

    if (b.stripPre != nullptr)
        b.stripPre->setStrip (before);

    if (b.strip != nullptr)
    {
        b.strip->setStrip (b.stripPre != nullptr ? t.strip.afterInserts() : t.strip);
        b.strip->setMonoOutput (t.outputChannels == 1);
    }

    if (spectrumTrackId == t.id)
        setSpectrumTrack (t.id);
}

void EngineBridge::setSpectrumTrack (const std::string& trackId)
{
    for (auto& [id, b] : bindings)
        for (auto* p : { b.strip, b.stripPre })
            if (p != nullptr)
                p->setSpectrumEnabled (id == trackId && p == b.stripWithEq());

    spectrumTrackId = trackId;
}

bool EngineBridge::getSpectrumSamples (float* dest, int numSamples, double& sampleRate) const
{
    auto it = bindings.find (spectrumTrackId);

    if (it == bindings.end() || it->second.stripWithEq() == nullptr)
        return false;

    sampleRate = it->second.stripWithEq()->getSampleRate();
    return it->second.stripWithEq()->getLatestSamples (dest, numSamples);
}

EngineBridge::MasterStatus EngineBridge::pollMaster()
{
    if (masterLimiter == nullptr)
        return {};

    // ミキサーとマスターの画面の両方から呼ばれるので、短い間隔の呼び出しには前回の値を返す
    const auto now = juce::Time::getMillisecondCounter();

    if (now - lastMasterPoll < 25)
        return lastMasterStatus;

    lastMasterPoll = now;

    // 再生を始めたときの測り直しは play() / record() で行う（ほかの経路で始まった場合はここで）
    const bool playing = isPlaying();

    if (playing && ! loudnessWasPlaying)
        startLoudness();

    loudnessWasPlaying = playing;

    loudnessScratch.clear();
    masterLimiter->takeLoudnessBlocks (loudnessScratch);

    // 止まっている間の無音は数えない
    if (playing)
        for (double b : loudnessScratch)
            loudnessStats.addBlock (b);

    MasterStatus s;
    s.gainReductionDb = masterLimiter->getGainReductionDb();
    s.inputPeakDb = masterLimiter->takeInputPeakDb();
    s.outputPeakDb = masterLimiter->takeOutputPeakDb();
    s.momentaryLufs = playing ? loudnessStats.momentaryLufs() : -100.0;
    s.shortTermLufs = playing ? loudnessStats.shortTermLufs() : -100.0;
    s.integratedLufs = loudnessStats.integratedLufs();
    s.seconds = loudnessStats.seconds();
    lastMasterStatus = s;
    return s;
}

float EngineBridge::getTrackGainReductionDb (const std::string& trackId) const
{
    auto it = bindings.find (trackId);
    return it != bindings.end() && it->second.stripWithComp() != nullptr ? it->second.stripWithComp()->getGainReductionDb() : 0.0f;
}

juce::File EngineBridge::pitchedFile (const collab::AudioClip& c, bool waitUntilReady)
{
    const auto projectDir = document.getProjectDir();
    const auto source = AudioFiles::fileForHash (projectDir, c.audioHash);
    auto dest = PitchShift::cachedFile (projectDir, c.audioHash, c.pitchSemitones);

    if (dest.existsAsFile() || ! source.existsAsFile())
        return dest;

    if (waitUntilReady)
    {
        // 書き出し: 作り終わるのを待つ（裏で作っている途中なら、それも待つ）
        while (pitchJobs.count (dest.getFullPathName()) > 0 && ! dest.existsAsFile())
            juce::Thread::sleep (20);

        if (! dest.existsAsFile())
            PitchShift::render (source, dest, c.pitchSemitones);

        return dest;
    }

    if (pitchJobs.insert (dest.getFullPathName()).second)
    {
        const double semitones = c.pitchSemitones;
        pitchPool.addJob ([this, source, dest, semitones, alive = std::weak_ptr<bool> (aliveFlag)]
        {
            PitchShift::render (source, dest, semitones);

            juce::MessageManager::callAsync ([this, dest, alive]
            {
                if (alive.expired())
                    return;

                pitchJobs.erase (dest.getFullPathName());

                // 鳴らすファイルが変わったので、オーディオのクリップを作り直す
                for (auto& [id, b] : bindings)
                    b.clipsKey.clear();

                sync();
            });
        });
    }

    return dest;
}

void EngineBridge::preparePitchedAudio()
{
    bool made = false;

    for (auto& t : document.getProject().tracks)
        for (auto& c : t.audioClips)
            if (c.pitchSemitones != 0.0 && ! PitchShift::cachedFile (document.getProjectDir(), c.audioHash, c.pitchSemitones).existsAsFile())
            {
                pitchedFile (c, true);
                made = true;
            }

    if (made)
        for (auto& [id, b] : bindings)
            b.clipsKey.clear();   // 次の sync() で、できたファイルに差し替える
}

juce::String EngineBridge::describeChannel (const std::string& trackId) const
{
    auto it = bindings.find (trackId);

    if (it == bindings.end())
        return {};

    const auto& b = it->second;
    juce::StringArray parts;

    for (auto* p : b.track->pluginList)
    {
        if (auto* strip = dynamic_cast<ChannelStripPlugin*> (p))
        {
            const auto& s = strip->getStrip();
            juce::StringArray stages;

            for (auto block : s.order())
            {
                if (block == collab::StripBlock::eq && s.eq.enabled)     stages.add ("eq");
                if (block == collab::StripBlock::comp && s.comp.enabled) stages.add ("comp");
            }

            if (! stages.isEmpty())
                parts.add (stages.joinIntoString (" "));
        }
        else
        {
            for (auto& e : b.effects)
                if (e.plugin.get() == p)
                    parts.add ("fx");
        }
    }

    return parts.joinIntoString (" | ");
}

void EngineBridge::syncInstrument (const collab::Track& t, Binding& b)
{
    if (! t.instrument)
    {
        removeInstrument (b);
        b.problem = "音源が設定されていません"_ju;
        return;
    }

    if (t.instrument->kind == collab::Instrument::Kind::external)
    {
        if (b.synth != nullptr)
        {
            b.synth->deleteFromParent();
            b.synth = nullptr;
            b.sfzText = {};
        }

        const auto key = t.instrument->plugin.uid + "|" + t.instrument->stateRef;

        if (key != b.instrumentKey || b.externalInstrument == nullptr)
        {
            if (b.externalInstrument != nullptr)
            {
                if (onPluginRemoved) onPluginRemoved (b.externalInstrument.get());
                b.externalInstrument->deleteFromParent();
            }

            b.externalInstrument = createExternal (t.instrument->plugin, t.instrument->stateRef);
            b.instrumentKey = key;
            b.instrumentStateRef = t.instrument->stateRef;

            if (b.externalInstrument != nullptr)
                b.track->pluginList.insertPlugin (b.externalInstrument, 0, nullptr);
            else
                b.problem = "プラグインを読み込めません"_ju;
        }

        return;
    }

    // 内蔵音源
    if (b.externalInstrument != nullptr)
    {
        if (onPluginRemoved) onPluginRemoved (b.externalInstrument.get());
        b.externalInstrument->deleteFromParent();
        b.externalInstrument = nullptr;
        b.instrumentKey = {};
    }

    if (b.synth == nullptr)
        b.synth = addSynth (*b.track);

    if (b.synth == nullptr)
        return;

    auto* manifest = library.find (t.instrument->id, t.instrument->version);

    if (manifest == nullptr)
    {
        b.problem = "内蔵音源が見つかりません: "_ju + toJuce (t.instrument->id) + " " + toJuce (t.instrument->version);

        if (b.sfzText.isNotEmpty())
        {
            b.synth->clearSfz();
            b.sfzText = {};
        }

        return;
    }

    const auto& params = t.instrument->params;
    auto text = toJuce (collab::generateSfz (*manifest, params));

    if (text != b.sfzText)
    {
        if (b.synth->setSfz (library.getVirtualSfzPath (*manifest).getFullPathName(), text))
            b.sfzText = text;
        else
            b.problem = "内蔵音源の読み込みに失敗しました"_ju;
    }

    auto resolved = collab::resolveInstrumentParams (*manifest, params);
    b.synth->setGainAndPan ((float) resolved.volumeDb, (float) resolved.pan);
}

//==============================================================================
bool EngineBridge::flushPluginStates()
{
    if (! document.hasLocation())
        return false;

    bool changed = false;

    auto write = [&] (te::Plugin* plugin, const std::string& stateRef)
    {
        auto* ext = dynamic_cast<te::ExternalPlugin*> (plugin);

        if (ext == nullptr || ext->getAudioPluginInstance() == nullptr || stateRef.empty())
            return;

        juce::MemoryBlock state;
        ext->getAudioPluginInstance()->getStateInformation (state);

        auto file = PluginHost::stateFile (document.getProjectDir(), stateRef);
        juce::MemoryBlock existing;

        if (file == juce::File())
            return;   // 曲のフォルダの外を指す（書かない）

        if (file.loadFileAsData (existing) && existing == state)
            return;

        file.getParentDirectory().createDirectory();
        file.replaceWithData (state.getData(), state.getSize());
        changed = true;
    };

    for (auto& [id, b] : bindings)
    {
        if (b.renderMode)
            continue;

        if (b.externalInstrument != nullptr)
            write (b.externalInstrument.get(), b.instrumentStateRef);

        for (auto& e : b.effects)
            if (dynamic_cast<BuiltinEffectPlugin*> (e.plugin.get()) == nullptr)
                write (e.plugin.get(), e.stateRef);
    }

    for (auto& e : masterEffects.effects)
        if (dynamic_cast<BuiltinEffectPlugin*> (e.plugin.get()) == nullptr)
            write (e.plugin.get(), e.stateRef);

    return changed;
}

te::Plugin* EngineBridge::getExternalPlugin (const std::string& trackId, const std::string& effectId) const
{
    if (! trackId.empty() && trackId == document.getProject().master.id)
    {
        for (auto& e : masterEffects.effects)
            if (e.id == effectId)
                return e.plugin.get();

        return nullptr;
    }

    auto it = bindings.find (trackId);

    if (it == bindings.end())
        return nullptr;

    if (effectId.empty())
        return it->second.externalInstrument.get();

    for (auto& e : it->second.effects)
        if (e.id == effectId)
            return e.plugin.get();

    return nullptr;
}

float EngineBridge::getEffectGainReductionDb (const std::string& trackId, const std::string& effectId) const
{
    if (auto* fx = dynamic_cast<BuiltinEffectPlugin*> (getExternalPlugin (trackId, effectId)))
        return fx->getGainReductionDb();

    return 0.0f;
}

EngineBridge::EffectMeter EngineBridge::takeEffectMeter (const std::string& trackId, const std::string& effectId) const
{
    if (auto* fx = dynamic_cast<BuiltinEffectPlugin*> (getExternalPlugin (trackId, effectId)))
    {
        const auto m = fx->takeMeter();
        return { m.input, m.output, m.gainReductionDb };
    }

    return {};
}

bool EngineBridge::isPlayingRender (const std::string& trackId) const
{
    auto it = bindings.find (trackId);
    return it != bindings.end() && it->second.renderMode;
}

juce::String EngineBridge::getInstrumentProblem (const std::string& trackId) const
{
    auto it = bindings.find (trackId);

    if (it == bindings.end())
        return {};

    if (it->second.missingAudio > 0)
        return "オーディオが "_ju + juce::String (it->second.missingAudio) + " 個見つかりません（取り込みが必要です）"_ju;

    return it->second.problem;
}

//==============================================================================
void EngineBridge::syncChordTrack (bool tempoChanged)
{
    const auto& project = document.getProject();
    const auto& playback = project.chordTrack.playback;

    if (chordTrack == nullptr)
    {
        chordTrack = createTrack();

        if (chordTrack == nullptr)
            return;

        chordTrack->setName ("Chord Track");
        routeToMix (*chordTrack);
        chordSynth = addSynth (*chordTrack);
        chordMeter = std::make_unique<Meter>();
        chordMeter->attach (*chordTrack);
    }

    // 発音先（内蔵ピアノ）
    if (chordSynth != nullptr)
    {
        auto* manifest = library.find (playback.instrument.id, playback.instrument.version);

        if (manifest == nullptr)
            manifest = library.findLatest (playback.instrument.id);

        if (manifest != nullptr)
        {
            auto text = toJuce (collab::generateSfz (*manifest, manifest->defaultParams));

            if (text != chordSfzText && chordSynth->setSfz (library.getVirtualSfzPath (*manifest).getFullPathName(), text))
                chordSfzText = text;
        }
    }

    chordTrack->setMute (! playback.enabled);

    if (auto vol = chordTrack->getVolumePlugin())
        if (std::abs (vol->getVolumeDb() - (float) playback.volumeDb) > 0.001f)
            vol->setVolumeDb ((float) playback.volumeDb);

    // コードイベントから MIDI を生成（§3.8: 全音符ベタ、小節の頭で弾き直し）
    const auto& map = document.getTempoMap();
    const auto notes = collab::renderChordTrack (project, map);

    std::ostringstream keyStream;
    for (auto& n : notes)
        keyStream << n.tick << ',' << n.lengthTick << ',' << n.pitch << ';';

    auto key = keyStream.str();

    if (key == chordKey && ! tempoChanged)
        return;

    chordKey = key;

    removeAllClips (*chordTrack);

    if (notes.empty())
        return;

    const double end = map.tickToSeconds ((double) collab::chordTrackEndTick (project, map));
    auto clip = chordTrack->insertMIDIClip ("chords", te::TimeRange (secondsToTime (0), secondsToTime (end)), nullptr);

    if (clip == nullptr)
        return;

    auto& seq = clip->getSequence();

    for (auto& n : notes)
    {
        const double s = map.tickToSeconds ((double) n.tick);
        const double e = map.tickToSeconds ((double) (n.tick + n.lengthTick));
        seq.addNote (n.pitch, secondsToBeats (s), te::BeatDuration::fromBeats (juce::jmax (0.001, e - s)), n.velocity, 0, nullptr);
    }
}

//==============================================================================
void EngineBridge::syncMetronome (bool tempoChanged)
{
    const auto& project = document.getProject();
    const auto& map = document.getTempoMap();

    if (metronomeTrack == nullptr)
    {
        metronomeTrack = createTrack();

        if (metronomeTrack == nullptr)
            return;

        metronomeTrack->setName ("Metronome");
        metronomeTrack->setSoloIsolate (true);   // 他のトラックのソロで消えないように
        metronomeMeter = std::make_unique<Meter>();
        metronomeMeter->attach (*metronomeTrack);

        if (auto synth = addSynth (*metronomeTrack))
        {
            auto sfz = library.getMetronomeSfz();
            synth->setSfz (sfz.getFullPathName(), sfz.loadFileAsString());
        }
    }

    metronomeTrack->setMute (! metronomeEnabled);

    if (auto vol = metronomeTrack->getVolumePlugin())
        vol->setVolumeDb (metronomeVolumeDb);

    // 曲の長さ + 余裕を持たせた範囲までクリックを並べる
    const int lastBar = juce::jmax (64, map.tickToBar (project.contentEndTick()) + 16);
    auto key = tempoKey + "#" + std::to_string (lastBar);

    if (key == metronomeKey && ! tempoChanged)
        return;

    metronomeKey = key;

    removeAllClips (*metronomeTrack);

    const double end = map.tickToSeconds ((double) map.barToTick (lastBar + 1));
    auto clip = metronomeTrack->insertMIDIClip ("click", te::TimeRange (secondsToTime (0), secondsToTime (end)), nullptr);

    if (clip == nullptr)
        return;

    auto& seq = clip->getSequence();

    for (int bar = 1; bar <= lastBar; ++bar)
    {
        const auto barTick = map.barToTick (bar);
        const auto sig = map.timeSignatureAtBar (bar);

        for (int beat = 0; beat < sig.numerator; ++beat)
        {
            const double s = map.tickToSeconds ((double) (barTick + beat * sig.ticksPerBeat()));
            seq.addNote (beat == 0 ? 76 : 77, secondsToBeats (s), te::BeatDuration::fromBeats (0.05), 100, 0, nullptr);
        }
    }
}

void EngineBridge::setMetronome (bool enabled, float volumeDb)
{
    metronomeEnabled = enabled;
    metronomeVolumeDb = volumeDb;
    syncMetronome (false);
}

//==============================================================================
void EngineBridge::play()
{
    auto& transport = edit->getTransport();

    if (! transport.isPlaying())
    {
        startLoudness();
        transport.play (false);
    }
}

void EngineBridge::startLoudness()
{
    // 止まっている間に貯まった分（無音）は捨てて、ここから測り直す
    loudnessScratch.clear();

    if (masterLimiter != nullptr)
        masterLimiter->takeLoudnessBlocks (loudnessScratch);

    loudnessScratch.clear();
    loudnessStats.reset();
    loudnessWasPlaying = true;
}

void EngineBridge::stop()
{
    edit->getTransport().stop (false, false);
}

void EngineBridge::togglePlay()
{
    if (isPlaying())
        stop();
    else
        play();
}

bool EngineBridge::isPlaying() const
{
    return edit->getTransport().isPlaying();
}

void EngineBridge::returnToStart()
{
    setPositionTick (0);
}

double EngineBridge::getPositionSeconds() const
{
    return edit->getTransport().getPosition().inSeconds();
}

double EngineBridge::getPositionTick() const
{
    const double seconds = getPositionSeconds();

    // 止まっていて置いた位置のままなら、置いた tick をそのまま返す（秒との往復で 1 tick 手前にならないように。
    // 3 拍子の小節の頭が「前の小節の 3.959」と出ていた）
    if (std::abs (seconds - lastSetSeconds) < 1.0e-6)
        return lastSetTick;

    // 再生中も、ごく近い整数の tick には合わせる（サンプル単位の丸めの誤差）
    const double tick = document.getTempoMap().secondsToTick (seconds);
    const double nearest = std::round (tick);
    return std::abs (tick - nearest) < 0.05 ? nearest : tick;
}

void EngineBridge::setPositionTick (double tick)
{
    tick = juce::jmax (0.0, tick);
    edit->getTransport().setPosition (secondsToTime (document.getTempoMap().tickToSeconds (tick)));
    lastSetTick = tick;
    lastSetSeconds = getPositionSeconds();
}

void EngineBridge::setLoop (bool enabled, collab::Tick start, collab::Tick end)
{
    loopEnabled = enabled && end > start;
    loopStart = start;
    loopEnd = end;
    applyLoop();
}

void EngineBridge::applyLoop()
{
    auto& transport = edit->getTransport();
    const auto& map = document.getTempoMap();

    if (loopEnd > loopStart)
    {
        const double start = map.tickToSeconds ((double) loopStart), end = map.tickToSeconds ((double) loopEnd);
        transport.setLoopRange (te::TimeRange (secondsToTime (start), secondsToTime (end)));
        hostTempo->setLoopRange (start, end);
    }

    transport.looping = loopEnabled;
}

//==============================================================================
void EngineBridge::Meter::attach (te::AudioTrack& track)
{
    detach();

    if (auto* plugin = track.getLevelMeterPlugin())
        attach (plugin->measurer);
}

void EngineBridge::Meter::attach (te::LevelMeasurer& m)
{
    detach();
    measurer = &m;
    measurer->addClient (client);
}

void EngineBridge::Meter::detach()
{
    if (measurer != nullptr)
        measurer->removeClient (client);

    measurer = nullptr;
}

static EngineBridge::StereoPeak peakOf (te::LevelMeasurer::Client& c, float offsetDb = 0.0f)
{
    return { c.getAndClearAudioLevel (0).dB + offsetDb, c.getAndClearAudioLevel (1).dB + offsetDb };
}

EngineBridge::StereoPeak EngineBridge::getTrackPeakDb (const std::string& trackId)
{
    Meter* meter = nullptr;

    if (trackId.empty())
        meter = chordMeter.get();
    else if (auto it = bindings.find (trackId); it != bindings.end())
        meter = it->second.meter.get();

    if (meter == nullptr || meter->measurer == nullptr)
        return {};

    return peakOf (meter->client);
}

EngineBridge::StereoPeak EngineBridge::getMetronomePeakDb()
{
    return metronomeMeter != nullptr && metronomeMeter->measurer != nullptr ? peakOf (metronomeMeter->client) : StereoPeak();
}

EngineBridge::StereoPeak EngineBridge::getMasterPeakDb()
{
    return masterMeter != nullptr && masterMeter->measurer != nullptr ? peakOf (masterMeter->client, masterVolumeDb) : StereoPeak();
}

void EngineBridge::setMasterVolumeDb (float db)
{
    masterVolumeDb = db;

    if (auto vol = edit->getMasterVolumePlugin())
        vol->setVolumeDb (db);
}

void EngineBridge::previewNote (const std::string& trackId, int pitch, int velocity)
{
    auto it = bindings.find (trackId);

    if (it == bindings.end() || it->second.track == nullptr || it->second.renderMode || isPlaying())
        return;

    // 内蔵音源は音源に直接渡す（経路の組み直しに左右されない）
    if (auto* synth = it->second.synth)
    {
        synth->queuePreview (juce::MidiMessage::noteOn (1, pitch, (juce::uint8) juce::jlimit (1, 127, velocity)));

        juce::Timer::callAfterDelay (300, [this, alive = std::weak_ptr<bool> (aliveFlag), trackId, pitch]
        {
            if (! alive.expired())
                if (auto b = bindings.find (trackId); b != bindings.end() && b->second.synth != nullptr)
                    b->second.synth->queuePreview (juce::MidiMessage::noteOff (1, pitch));
        });

        return;
    }

    // 外部プラグイン: ノートを置いた直後はクリップを作り直していて、音の経路が組み直される間に送った MIDI は消えてしまう。
    // 作り直してから少し待ってから鳴らす（ピアノロールで続けて打ち込んだとき、2 音目から鳴らなかった）
    constexpr juce::uint32 settleMs = 120;
    const auto elapsed = juce::Time::getMillisecondCounter() - it->second.clipsRebuiltAt;

    if (elapsed < settleMs)
    {
        juce::Timer::callAfterDelay ((int) (settleMs - elapsed), [this, alive = std::weak_ptr<bool> (aliveFlag), trackId, pitch, velocity]
        {
            if (! alive.expired())
                if (auto b = bindings.find (trackId); b != bindings.end())
                {
                    b->second.clipsRebuiltAt = 0;   // もう待たない
                    previewNote (trackId, pitch, velocity);
                }
        });
        return;
    }

    it->second.track->injectLiveMidiMessage (juce::MidiMessage::noteOn (1, pitch, (juce::uint8) juce::jlimit (1, 127, velocity)),
                                             te::MPESourceID());

    // 少し後で止める（その間にトラックが消えたり、アプリが終了していたら何もしない）
    juce::Timer::callAfterDelay (300, [this, alive = std::weak_ptr<bool> (aliveFlag), trackId, pitch]
    {
        if (alive.expired())
            return;

        if (auto b = bindings.find (trackId); b != bindings.end() && b->second.track != nullptr)
            b->second.track->injectLiveMidiMessage (juce::MidiMessage::noteOff (1, pitch), te::MPESourceID());
    });
}

int EngineBridge::countEngineNotes (const std::string& trackId) const
{
    auto it = bindings.find (trackId);

    if (it == bindings.end() || it->second.track == nullptr)
        return 0;

    int count = 0;

    for (auto* clip : it->second.track->getClips())
        if (auto* midi = dynamic_cast<te::MidiClip*> (clip))
            count += midi->getSequence().getNotes().size();

    return count;
}

int EngineBridge::countEngineClips (const std::string& trackId) const
{
    auto it = bindings.find (trackId);
    return it == bindings.end() || it->second.track == nullptr ? 0 : it->second.track->getClips().size();
}
