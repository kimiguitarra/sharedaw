#include "EngineBridge.h"

#include <sstream>

#include "SfizzPlugin.h"
#include "collab/ChordPlayback.h"

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

    if (t.type == collab::TrackType::midi)
        syncInstrument (t, b);

    // クリップ（tick → 秒 → Tracktion の拍）
    auto key = makeClipsKey (t);

    if (key == b.clipsKey && ! tempoChanged)
        return;

    b.clipsKey = key;

    for (auto c : track.getClips())
        c->removeFromParent();

    const auto& map = document.getTempoMap();

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

void EngineBridge::syncInstrument (const collab::Track& t, Binding& b)
{
    if (b.synth == nullptr)
        b.synth = addSynth (*b.track);

    if (b.synth == nullptr)
        return;

    if (! t.instrument || t.instrument->kind != collab::Instrument::Kind::builtin)
    {
        // 外部プラグインは M2 で対応する
        b.problem = t.instrument ? "外部プラグインは未対応です（M2）"_ju : "音源が設定されていません"_ju;

        if (b.sfzText.isNotEmpty())
        {
            b.synth->clearSfz();
            b.sfzText = {};
        }

        return;
    }

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
        {
            b.sfzText = text;
            b.problem = {};
        }
        else
        {
            b.problem = "内蔵音源の読み込みに失敗しました"_ju;
        }
    }

    auto resolved = collab::resolveInstrumentParams (*manifest, params);
    b.synth->setGainAndPan ((float) resolved.volumeDb, (float) resolved.pan);
}

juce::String EngineBridge::getInstrumentProblem (const std::string& trackId) const
{
    auto it = bindings.find (trackId);
    return it != bindings.end() ? it->second.problem : juce::String();
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
