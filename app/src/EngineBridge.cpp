#include "EngineBridge.h"

#include <sstream>

#include "SfizzPlugin.h"
#include "collab/ChordPlayback.h"
#include "collab/Render.h"
#include "audio/AudioFiles.h"
#include "collab/Time.h"
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

    edit->getTransport().ensureContextAllocated();

    document.addChangeListener (this);
    sync();
}

EngineBridge::~EngineBridge()
{
    document.removeChangeListener (this);
    edit->getTransport().stop (false, true);
}

void EngineBridge::changeListenerCallback (juce::ChangeBroadcaster*)
{
    sync();
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
            b.track = createTrack();

        if (b.track == nullptr)
            continue;

        syncTrack (t, b, tempoChanged);
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

        auto* vol = b.track->getVolumePlugin();
        int index = vol != nullptr ? b.track->pluginList.indexOf (vol) : -1;

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
        ok = te::Renderer::renderToFile ("CollabDAW bounce", params).existsAsFile();
    }

    if (vol != nullptr)
    {
        vol->setVolumeDb (oldDb);
        vol->setPan (oldPan);
    }

    track.setMute (oldMute);
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
    return te::Renderer::renderToFile ("CollabDAW render", params).existsAsFile();
}
