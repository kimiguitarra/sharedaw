#include "EngineBridge.h"

#include <sstream>

#include "SfizzPlugin.h"
#include "audio/ChannelStripPlugin.h"
#include "audio/CountInPlugin.h"
#include "collab/Recording.h"
#include "collab/ChordPlayback.h"
#include "collab/Render.h"
#include "audio/AudioFiles.h"
#include "collab/Time.h"
#include "collab/Uuid.h"
#include "plugins/PluginHost.h"

namespace
{
    te::TimePosition secondsToTime (double s)     { return te::TimePosition::fromSeconds (juce::jmax (0.0, s)); }
    te::BeatPosition secondsToBeats (double s)    { return te::BeatPosition::fromBeats (s); }   // 60BPM 固定なので 1拍 = 1秒

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

            s << ']';
        }

        for (auto& c : t.audioClips)
            s << "A" << c.id << '@' << c.startTick << ':' << c.audioHash << ':' << c.sourceOffsetSamples << ':' << c.lengthSamples
              << ':' << c.gainDb << ':' << c.fadeInSamples << ':' << c.fadeOutSamples << ';';

        if (t.render)
            s << "R" << t.render->audioHash;

        return s.str();
    }
}

EngineBridge::EngineBridge (te::Engine& e, ProjectDocument& doc, const InstrumentLibrary& lib)
    : engine (e), document (doc), library (lib)
{
    edit = std::make_unique<te::Edit> (engine, te::Edit::forEditing);

    // 既定で作られるトラックは使わない
    for (auto t : te::getAudioTracks (*edit))
        edit->deleteTrack (t);

    auto& ts = edit->tempoSequence;
    ts.getTempo (0)->setBpm (60.0);
    ts.getTimeSig (0)->setStringTimeSig ("4/4");
    edit->clickTrackEnabled = false;

    // 入力デバイスの一覧は非同期に作られるので、変わるたびに設定する
    engine.getDeviceManager().addChangeListener (this);
    configureInputs();

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
    for (auto& [id, b] : bindings)
        b.meter.reset();

    chordMeter.reset();
    metronomeMeter.reset();
    masterMeter.reset();
    midiInputs.clear();
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

        auto in = std::make_unique<MidiIn>();
        in->device = d;
        in->client = std::make_unique<te::LevelMeasurer::Client>();
        d->levelMeasurer.addClient (*in->client);
        midiInputs.push_back (std::move (in));
    }
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

float EngineBridge::getMidiActivity() const
{
    float a = 0.0f;

    for (auto& in : midiInputs)
        if (in->device->isEnabled())
            a = juce::jmax (a, in->activity);

    return a;
}

void EngineBridge::setMidiTarget (const std::string& trackId)
{
    if (trackId == midiTargetId)
        return;

    midiTargetId = trackId;
    applyInputs();
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
                    other.track->getOutput().setOutputToDefaultDevice (false);

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
    syncChordTrack (tempoChanged);
    syncMetronome (tempoChanged);

    if (tempoChanged)
    {
        applyLoop();

        if (positionTick >= 0)
            setPositionTick (positionTick);
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

    if (track.isMuted (false) != t.mute)
        track.setMute (t.mute);

    if (track.isSolo (false) != t.solo)
        track.setSolo (t.solo);

    syncStrip (t, b);

    // 外部プラグインを使うトラックは、この環境でプラグインを鳴らせなければバウンスした音で再生する（§3.7）
    juce::String liveProblem;
    const bool external = collab::usesExternalPlugin (t);
    const bool renderMode = external && ! canPlayLive (t, liveProblem);
    b.renderMode = renderMode;

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

    for (auto c : track.getClips())
        c->removeFromParent();

    const auto& map = document.getTempoMap();
    b.missingAudio = 0;

    if (renderMode)
    {
        if (! t.render)
            return;

        auto file = document.getProjectDir().getChildFile ("audio").getChildFile (toJuce (t.render->audioHash) + ".wav");

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
    for (auto& c : t.audioClips)
    {
        auto file = document.getProjectDir().getChildFile ("audio").getChildFile (toJuce (c.audioHash) + ".wav");

        if (! document.hasLocation() || ! file.existsAsFile())
        {
            ++b.missingAudio;
            continue;
        }

        const double start = map.tickToSeconds ((double) c.startTick);
        const double length = (double) c.lengthSamples / collab::kSampleRate;
        const te::ClipPosition pos { te::TimeRange (secondsToTime (start), te::TimeDuration::fromSeconds (length)),
                                     te::TimeDuration::fromSeconds ((double) c.sourceOffsetSamples / collab::kSampleRate) };

        if (auto clip = track.insertWaveClip (toJuce (c.displayName), file, pos, false))
        {
            clip->setAutoTempo (false);
            clip->setAutoPitch (false);
            clip->setGainDB ((float) c.gainDb);
            clip->setFadeIn (te::TimeDuration::fromSeconds ((double) c.fadeInSamples / collab::kSampleRate));
            clip->setFadeOut (te::TimeDuration::fromSeconds ((double) c.fadeOutSamples / collab::kSampleRate));
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
        if (! e.bypass && ! check (e.plugin, e.stateRef))
            return false;

    return true;
}

te::Plugin::Ptr EngineBridge::createExternal (const collab::ExternalPlugin& plugin, const std::string& stateRef)
{
    auto desc = PluginHost::find (engine, plugin);

    if (! desc)
        return {};

    auto p = edit->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, *desc);

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
    std::string key;

    for (auto& e : t.effects)
        key += e.id + "|" + e.plugin.uid + "|" + e.stateRef + ";";

    if (key != b.effectsKey)
    {
        removeEffects (b);
        b.effectsKey = key;

        // チャンネルストリップ（なければ音量・パン）の直前に並べる
        te::Plugin* before = b.strip != nullptr ? static_cast<te::Plugin*> (b.strip) : b.track->getVolumePlugin();
        int index = before != nullptr ? b.track->pluginList.indexOf (before) : -1;

        for (auto& e : t.effects)
        {
            if (auto p = createExternal (e.plugin, e.stateRef))
            {
                b.track->pluginList.insertPlugin (p, index < 0 ? -1 : index++, nullptr);
                b.effects.push_back ({ e.id, e.stateRef, p });
            }
        }
    }

    // バイパス（エフェクトを通さない）
    for (auto& e : t.effects)
        for (auto& be : b.effects)
            if (be.id == e.id && be.plugin->isEnabled() == e.bypass)
                be.plugin->setEnabled (! e.bypass);
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
    if (b.strip == nullptr)
    {
        auto plugin = edit->getPluginCache().createNewPlugin (ChannelStripPlugin::xmlTypeName, {});
        auto* vol = b.track->getVolumePlugin();
        const int index = vol != nullptr ? b.track->pluginList.indexOf (vol) : -1;
        b.track->pluginList.insertPlugin (plugin, index, nullptr);
        b.strip = dynamic_cast<ChannelStripPlugin*> (plugin.get());
    }

    if (b.strip != nullptr)
        b.strip->setStrip (t.strip);
}

void EngineBridge::setSpectrumTrack (const std::string& trackId)
{
    for (auto& [id, b] : bindings)
        if (b.strip != nullptr)
            b.strip->setSpectrumEnabled (id == trackId);

    spectrumTrackId = trackId;
}

bool EngineBridge::getSpectrumSamples (float* dest, int numSamples, double& sampleRate) const
{
    auto it = bindings.find (spectrumTrackId);

    if (it == bindings.end() || it->second.strip == nullptr)
        return false;

    sampleRate = it->second.strip->getSampleRate();
    return it->second.strip->getLatestSamples (dest, numSamples);
}

float EngineBridge::getTrackGainReductionDb (const std::string& trackId) const
{
    auto it = bindings.find (trackId);
    return it != bindings.end() && it->second.strip != nullptr ? it->second.strip->getGainReductionDb() : 0.0f;
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
            write (e.plugin.get(), e.stateRef);
    }

    return changed;
}

te::Plugin* EngineBridge::getExternalPlugin (const std::string& trackId, const std::string& effectId) const
{
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

bool EngineBridge::isPlayingRender (const std::string& trackId) const
{
    auto it = bindings.find (trackId);
    return it != bindings.end() && it->second.renderMode;
}

juce::Result EngineBridge::renderTrack (const std::string& trackId, const juce::File& output, double tailSeconds)
{
    sync();
    stop();

    auto it = bindings.find (trackId);

    if (it == bindings.end())
        return juce::Result::fail ("トラックが見つかりません"_ju);

    if (it->second.renderMode)
        return juce::Result::fail ("この環境ではプラグインを鳴らせないため、バウンスできません"_ju);

    auto& track = *it->second.track;
    auto allTracks = te::getAllTracks (*edit);
    juce::BigInteger tracksToDo;

    for (int i = 0; i < allTracks.size(); ++i)
        if (allTracks[i] == &track)
            tracksToDo.setBit (i);

    // バウンスはトラックの音量・パン・ミュートの前の音（受け取った側でも同じ設定がかかるため）
    auto* vol = track.getVolumePlugin();
    const float oldDb = vol != nullptr ? vol->getVolumeDb() : 0.0f;
    const float oldPan = vol != nullptr ? vol->getPan() : 0.0f;
    const bool oldMute = track.isMuted (false);

    if (vol != nullptr)
    {
        vol->setVolumeDb (0.0f);
        vol->setPan (0.0f);
    }

    track.setMute (false);

    // バスへの出力・センドもバウンスには含めない（トラックそのものの音を書き出す）
    auto* oldDest = track.getOutput().getDestinationTrack();

    if (oldDest != nullptr)
        track.getOutput().setOutputToDefaultDevice (false);

    for (auto& s : it->second.sends)
        s->setEnabled (false);

    // EQ・コンプもバウンスに含めない（同上）
    auto* strip = it->second.strip;
    const bool stripWasEnabled = strip != nullptr && strip->isEnabled();

    if (strip != nullptr)
        strip->setEnabled (false);

    const auto& project = document.getProject();
    const double end = document.getTempoMap().tickToSeconds ((double) collab::chordTrackEndTick (project, document.getTempoMap())) + tailSeconds;

    juce::WavAudioFormat wav;
    te::Renderer::Parameters params (*edit);
    params.destFile = output;
    params.audioFormat = &wav;
    params.bitDepth = 32;
    params.sampleRateForAudio = (double) collab::kSampleRate;
    params.time = te::TimeRange (secondsToTime (0), secondsToTime (end));
    params.tracksToDo = tracksToDo;
    params.canRenderInMono = false;
    params.usePlugins = true;
    params.checkNodesForAudio = false;

    bool ok = false;
    {
        const te::Edit::ScopedRenderStatus renderStatus (*edit, true);
        output.deleteFile();
        ok = te::Renderer::renderToFile ("ShareDAW bounce", params).existsAsFile();
    }

    if (vol != nullptr)
    {
        vol->setVolumeDb (oldDb);
        vol->setPan (oldPan);
    }

    track.setMute (oldMute);

    if (strip != nullptr)
        strip->setEnabled (stripWasEnabled);

    if (oldDest != nullptr)
        track.getOutput().setOutputToTrack (oldDest);

    for (auto& s : it->second.sends)
        s->setEnabled (true);

    return ok ? juce::Result::ok() : juce::Result::fail ("書き出しに失敗しました"_ju);
}

std::string EngineBridge::trackFingerprint (const collab::Track& t) const
{
    const auto dir = document.getProjectDir();
    return collab::trackSourceFingerprint (t, [dir] (const std::string& ref) { return PluginHost::stateHash (dir, ref); });
}

juce::Result EngineBridge::bounceTrack (const std::string& trackId, collab::Render& result)
{
    if (! document.hasLocation())
        return juce::Result::fail ("プロジェクトがまだ保存されていません"_ju);

    const auto* track = document.getProject().findTrack (trackId);

    if (track == nullptr)
        return juce::Result::fail ("トラックが見つかりません"_ju);

    const auto fingerprint = trackFingerprint (*track);
    const auto audioDir = document.getProjectDir().getChildFile ("audio");
    audioDir.createDirectory();

    juce::TemporaryFile temp (audioDir.getChildFile ("bounce.wav"));
    constexpr double tailSeconds = 2.0;

    if (auto r = renderTrack (trackId, temp.getFile(), tailSeconds); r.failed())
        return r;

    const auto hash = AudioFiles::hashFile (temp.getFile());

    if (hash.empty())
        return juce::Result::fail ("ハッシュを計算できません"_ju);

    auto target = AudioFiles::fileForHash (document.getProjectDir(), hash);

    if (! target.existsAsFile() && ! temp.getFile().moveFileTo (target))
        return juce::Result::fail ("保存できません: "_ju + target.getFullPathName());

    result.audioHash = hash;
    result.renderedAt = collab::nowUtcIso8601();
    result.sourceFingerprint = fingerprint;
    result.tailSeconds = tailSeconds;
    return juce::Result::ok();
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

    for (auto c : chordTrack->getClips())
        c->removeFromParent();

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

    for (auto c : metronomeTrack->getClips())
        c->removeFromParent();

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
        transport.play (false);
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
    return document.getTempoMap().secondsToTick (getPositionSeconds());
}

void EngineBridge::setPositionTick (double tick)
{
    edit->getTransport().setPosition (secondsToTime (document.getTempoMap().tickToSeconds (juce::jmax (0.0, tick))));
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
        transport.setLoopRange (te::TimeRange (secondsToTime (map.tickToSeconds ((double) loopStart)),
                                               secondsToTime (map.tickToSeconds ((double) loopEnd))));

    transport.looping = loopEnabled;
}

bool EngineBridge::isLooping() const
{
    return edit->getTransport().looping;
}

//==============================================================================
bool EngineBridge::renderToFile (const juce::File& output, collab::Tick endTick, double tailSeconds)
{
    sync();
    stop();

    juce::BigInteger tracksToDo;
    auto allTracks = te::getAllTracks (*edit);

    for (int i = 0; i < allTracks.size(); ++i)
        if (dynamic_cast<te::AudioTrack*> (allTracks[i]) != nullptr && allTracks[i] != metronomeTrack.get()
             && (allTracks[i] != chordTrack.get() || document.getProject().chordTrack.playback.enabled))
            tracksToDo.setBit (i);

    const double end = document.getTempoMap().tickToSeconds ((double) endTick) + tailSeconds;
    output.deleteFile();

    // プロジェクトのフォーマット（48kHz / 32bit float WAV、§7.1）で書き出す
    juce::WavAudioFormat wav;
    te::Renderer::Parameters params (*edit);
    params.destFile = output;
    params.audioFormat = &wav;
    params.bitDepth = 32;
    params.sampleRateForAudio = (double) collab::kSampleRate;
    params.blockSizeForAudio = 512;
    params.time = te::TimeRange (secondsToTime (0), secondsToTime (end));
    params.tracksToDo = tracksToDo;
    params.canRenderInMono = false;
    params.usePlugins = true;
    params.useMasterPlugins = false;

    // レンダリング中はオーディオデバイスから切り離す（終了後に再接続される）
    const te::Edit::ScopedRenderStatus renderStatus (*edit, true);
    return te::Renderer::renderToFile ("ShareDAW render", params).existsAsFile();
}

//==============================================================================
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
    if (input.device.isNotEmpty())
        for (auto& [id, other] : trackInputs)
            if (id != trackId && other.device == input.device)
                other = {};

    trackInputs[trackId] = input;
    applyInputs();
}

void EngineBridge::applyInputs()
{
    edit->getTransport().ensureContextAllocated();

    for (auto* in : edit->getAllInputDevices())
    {
        // MIDI キーボード: 選択中の MIDI トラックの音源で鳴らし、録音もそのトラックへ
        if (in->getInputDevice().getDeviceType() == te::InputDevice::physicalMidiDevice)
        {
            te::AudioTrack* target = nullptr;

            if (auto b = bindings.find (midiTargetId); b != bindings.end() && b->second.track != nullptr && ! b->second.renderMode)
                target = b->second.track.get();

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

            if (ti.device == name && (ti.armed || ti.monitor) && b != bindings.end() && b->second.track != nullptr)
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

    transport.record (false);

    // Tracktion 自身のカウントインのクリック（Edit の 60BPM で鳴る）は使わない。
    // 最初のクリックはプリロール開始の 0.5 拍以上あとなので、ここで消せば鳴らない
    edit->setClickTrackRange ({});

    if (! transport.isRecording())
    {
        restoreAfterRecording();
        return juce::Result::fail ("録音を開始できませんでした。オーディオ設定で入力デバイスを確認してください。"_ju);
    }

    return juce::Result::ok();
}

bool EngineBridge::isRecording() const
{
    return edit->getTransport().isRecording();
}

void EngineBridge::restoreAfterRecording()
{
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
    });
}

void EngineBridge::recordingFinished (te::InputDeviceInstance&, te::EditItemID targetID,
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

            for (auto* n : midi->getSequence().getNotes())
            {
                const double start = clipStart + n->getStartBeat().inBeats() - offset;
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

        if (auto* wave = dynamic_cast<te::WaveAudioClip*> (clip))
        {
            const auto pos = wave->getPosition();
            RecordedTake take;
            take.trackId = trackId;
            take.file = wave->getAudioFile().getFile();
            take.startSeconds = pos.getStart().inSeconds();
            take.offsetSeconds = pos.getOffset().inSeconds();
            take.lengthSeconds = pos.getLength().inSeconds();
            take.punchInSeconds = punchInSeconds;

            if (! trackId.empty() && take.file.existsAsFile())
                pendingTakes.push_back (take);
        }

        // Edit には JSON から作り直したクリップだけを置く
        clip->removeFromParent();
    }

    // 入力ごとに呼ばれるので、まとめてから知らせる
    juce::MessageManager::callAsync ([this, alive = std::weak_ptr<bool> (aliveFlag)]
    {
        if (alive.expired())
            return;

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
    });
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

float EngineBridge::getTrackPeakDb (const std::string& trackId)
{
    Meter* meter = nullptr;

    if (trackId.empty())
        meter = chordMeter.get();
    else if (auto it = bindings.find (trackId); it != bindings.end())
        meter = it->second.meter.get();

    if (meter == nullptr || meter->measurer == nullptr)
        return -100.0f;

    return juce::jmax (meter->client.getAndClearAudioLevel (0).dB, meter->client.getAndClearAudioLevel (1).dB);
}

static float peakOf (te::LevelMeasurer::Client& c)
{
    return juce::jmax (c.getAndClearAudioLevel (0).dB, c.getAndClearAudioLevel (1).dB);
}

float EngineBridge::getMetronomePeakDb()
{
    return metronomeMeter != nullptr && metronomeMeter->measurer != nullptr ? peakOf (metronomeMeter->client) : -100.0f;
}

float EngineBridge::getMasterPeakDb()
{
    return masterMeter != nullptr && masterMeter->measurer != nullptr ? peakOf (masterMeter->client) + masterVolumeDb : -100.0f;
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
