#include "AppContext.h"

#include "Theme.h"
#include "collab/Uuid.h"
#include "collab/ClipEditing.h"
#include "Dialogs.h"
#include "SyncUI.h"
#include "audio/AudioFiles.h"
#include "audio/MidiImport.h"
#include "collab/Render.h"
#include "collab/Time.h"
#include "plugins/PluginHost.h"

void AppContext::addBuiltinMidiTrack (const std::string& instrumentId, const juce::String& name)
{
    auto* manifest = library.findLatest (instrumentId);

    collab::Track t;
    t.id = collab::generateUuid();
    t.type = collab::TrackType::midi;
    t.name = toStd (name);
    t.color = toStd (Theme::trackColourHex ((int) document.getProject().tracks.size()));

    collab::Instrument inst;
    inst.kind = collab::Instrument::Kind::builtin;
    inst.id = instrumentId;
    inst.version = manifest != nullptr ? manifest->version : "0.1.0";
    inst.params = manifest != nullptr ? manifest->defaultParams : nlohmann::json::object();
    t.instrument = inst;

    // 選択中のトラックの下に追加する
    const auto afterId = state.selectedTrackId;

    document.perform ("トラックの追加"_ju, [t, afterId] (collab::Project& p)
    {
        const int i = p.indexOfTrack (afterId);
        p.tracks.insert (i >= 0 ? p.tracks.begin() + i + 1 : p.tracks.end(), t);
    });

    state.selectedTrackId = t.id;
    state.selectedClipId = {};
    state.changed();
}

//==============================================================================
std::string AppContext::addAudioTrack (const juce::String& name)
{
    collab::Track t;
    t.id = collab::generateUuid();
    t.type = collab::TrackType::audio;
    t.name = toStd (name);
    t.color = toStd (Theme::trackColourHex ((int) document.getProject().tracks.size()));

    const auto afterId = state.selectedTrackId;

    document.perform ("オーディオトラックの追加"_ju, [t, afterId] (collab::Project& p)
    {
        const int i = p.indexOfTrack (afterId);
        p.tracks.insert (i >= 0 ? p.tracks.begin() + i + 1 : p.tracks.end(), t);
    });

    state.selectedTrackId = t.id;
    state.selectedClipId = {};
    state.changed();
    return t.id;
}

void AppContext::importAudioFiles (const juce::Array<juce::File>& files, std::string trackId, collab::Tick atTick)
{
    if (files.isEmpty())
        return;

    if (! document.hasLocation())
    {
        Dialogs::showInfo ("オーディオの読み込み"_ju, "オーディオはプロジェクトのフォルダに保存するので、先にプロジェクトを保存してください。"_ju);
        return;
    }

    std::vector<AudioFiles::Imported> imported;
    juce::StringArray errors;
    const auto audioDir = document.getProjectDir().getChildFile ("audio");

    SyncUI::runWithProgress ("オーディオを読み込んでいます"_ju, [&]
    {
        for (auto& f : files)
        {
            AudioFiles::Imported r;

            if (auto res = AudioFiles::importFile (f, audioDir, r); res.failed())
                errors.add (res.getErrorMessage());
            else
                imported.push_back (r);
        }

        return juce::Result::ok();
    });

    if (! errors.isEmpty())
        Dialogs::showError ("読み込めないファイルがありました"_ju, errors.joinIntoString ("\n"));

    if (imported.empty())
        return;

    auto* track = document.getProject().findTrack (trackId);

    if (track == nullptr || track->type != collab::TrackType::audio)
        trackId = addAudioTrack (imported.front().displayName);

    // 順に並べる（テンポに追従しないので、秒で次の位置を求める）
    std::vector<collab::AudioClip> clips;
    const auto& map = document.getTempoMap();
    collab::Tick tick = atTick;

    for (auto& im : imported)
    {
        collab::AudioClip c;
        c.id = collab::generateUuid();
        c.startTick = tick;
        c.audioHash = im.hash;
        c.displayName = toStd (im.displayName);
        c.lengthSamples = juce::jmax<collab::SampleCount> (1, im.lengthSamples);
        clips.push_back (c);
        tick = collab::audioClipEndTick (c, map);
    }

    document.perform ("オーディオの読み込み"_ju, [trackId, clips] (collab::Project& p)
    {
        if (auto* t = p.findTrack (trackId))
            for (auto& c : clips)
                t->audioClips.push_back (c);
    });

    state.selectedTrackId = trackId;
    state.selectedClipId = clips.front().id;
    state.changed();
}

void AppContext::splitAtPlayhead()
{
    auto* track = selectedTrack();

    if (track == nullptr)
        return;

    const auto at = (collab::Tick) std::llround (state.playheadTick);
    const auto& map = document.getTempoMap();
    const auto trackId = track->id;

    // 選択中のクリップ、なければ再生位置にあるクリップ
    std::string clipId = state.selectedClipId;

    auto contains = [&] (const std::string& id)
    {
        for (auto& c : track->audioClips)
            if (c.id == id) return at > c.startTick && at < collab::audioClipEndTick (c, map);
        for (auto& c : track->midiClips)
            if (c.id == id) return at > c.startTick && at < c.endTick();
        return false;
    };

    if (clipId.empty() || ! contains (clipId))
    {
        clipId.clear();

        for (auto& c : track->audioClips)
            if (contains (c.id)) clipId = c.id;

        for (auto& c : track->midiClips)
            if (contains (c.id)) clipId = c.id;
    }

    if (clipId.empty())
        return;

    const auto newId = collab::generateUuid();

    document.perform ("クリップの分割"_ju, [trackId, clipId, at, newId, map] (collab::Project& p)
    {
        auto* t = p.findTrack (trackId);

        if (t == nullptr)
            return;

        for (size_t i = 0; i < t->audioClips.size(); ++i)
            if (t->audioClips[i].id == clipId)
                if (auto r = collab::splitAudioClip (t->audioClips[i], at, map, newId))
                {
                    t->audioClips[i] = r->first;
                    t->audioClips.push_back (r->second);
                    return;
                }

        for (size_t i = 0; i < t->midiClips.size(); ++i)
            if (t->midiClips[i].id == clipId)
                if (auto r = collab::splitMidiClip (t->midiClips[i], at, newId, [] { return collab::generateUuid(); }))
                {
                    t->midiClips[i] = r->first;
                    t->midiClips.push_back (r->second);
                    return;
                }
    });
}

//==============================================================================
void AppContext::setBuiltinInstrument (const std::string& trackId, const std::string& instrumentId)
{
    auto* manifest = library.findLatest (instrumentId);

    collab::Instrument inst;
    inst.kind = collab::Instrument::Kind::builtin;
    inst.id = instrumentId;
    inst.version = manifest != nullptr ? manifest->version : "0.1.0";
    inst.params = manifest != nullptr ? manifest->defaultParams : nlohmann::json::object();

    document.perform ("音源の変更"_ju, [trackId, inst] (collab::Project& p)
    {
        if (auto* t = p.findTrack (trackId))
            t->instrument = inst;
    });
}

void AppContext::setExternalInstrument (const std::string& trackId, const juce::PluginDescription& desc)
{
    collab::Instrument inst;
    inst.kind = collab::Instrument::Kind::external;
    inst.plugin = PluginHost::describe (desc);
    inst.stateRef = PluginHost::stateRefFor (collab::generateUuid());

    document.perform ("音源の変更"_ju, [trackId, inst] (collab::Project& p)
    {
        if (auto* t = p.findTrack (trackId))
            t->instrument = inst;
    });
}

void AppContext::addEffect (const std::string& trackId, const juce::PluginDescription& desc)
{
    collab::Effect e;
    e.id = collab::generateUuid();
    e.plugin = PluginHost::describe (desc);
    e.stateRef = PluginHost::stateRefFor (e.id);

    document.perform ("エフェクトの追加"_ju, [trackId, e] (collab::Project& p)
    {
        if (auto* t = p.findTrack (trackId))
            t->effects.push_back (e);
    });
}

void AppContext::removeEffect (const std::string& trackId, const std::string& effectId)
{
    document.perform ("エフェクトの削除"_ju, [trackId, effectId] (collab::Project& p)
    {
        if (auto* t = p.findTrack (trackId))
            std::erase_if (t->effects, [&] (auto& e) { return e.id == effectId; });
    });
}

void AppContext::toggleEffectBypass (const std::string& trackId, const std::string& effectId)
{
    document.perform ("エフェクトのバイパス"_ju, [trackId, effectId] (collab::Project& p)
    {
        if (auto* t = p.findTrack (trackId))
            for (auto& e : t->effects)
                if (e.id == effectId)
                    e.bypass = ! e.bypass;
    });
}

std::string AppContext::fingerprint (const collab::Track& t) const
{
    return engine.trackFingerprint (t);
}

void AppContext::bounceTrack (const std::string& trackId)
{
    if (! document.hasLocation())
        return Dialogs::showInfo ("バウンス"_ju, "バウンスした音はプロジェクトのフォルダに保存するので、先にプロジェクトを保存してください。"_ju);

    engine.flushPluginStates();

    collab::Render render;
    auto r = SyncUI::runWithProgress ("バウンスしています"_ju, [&] { return engine.bounceTrack (trackId, render); });

    if (r.failed())
        return Dialogs::showError ("バウンスできませんでした"_ju, r.getErrorMessage());

    document.perform ("バウンス"_ju, [trackId, render] (collab::Project& p)
    {
        if (auto* t = p.findTrack (trackId))
            t->render = render;
    });
}

//==============================================================================
void AppContext::importMidiFiles (const juce::Array<juce::File>& files, std::string trackId, collab::Tick atTick)
{
    const auto& project = document.getProject();
    const auto* target = project.findTrack (trackId);

    if (target != nullptr && target->type != collab::TrackType::midi)
        target = nullptr;

    const bool projectEmpty = std::all_of (project.tracks.begin(), project.tracks.end(),
                                           [] (auto& t) { return t.midiClips.empty() && t.audioClips.empty(); });

    std::vector<collab::Track> newTracks;
    std::vector<std::pair<std::string, collab::MidiClip>> clipsForExisting;
    std::optional<double> bpm;
    std::optional<std::pair<int, int>> meter;
    juce::StringArray errors;
    int colourIndex = (int) project.tracks.size();
    const auto& map = document.getTempoMap();

    for (auto& file : files)
    {
        auto r = MidiImport::read (file);

        if (! r.ok())
        {
            errors.add (file.getFileName() + ": " + r.error);
            continue;
        }

        if (! bpm) bpm = r.bpm;
        if (! meter) meter = r.meter;

        // すべてのパートを同じ長さ（小節単位）のクリップにして、位置関係を保つ
        const auto endBar = map.tickToBar (atTick + std::max<collab::Tick> (1, r.endTick()) - 1) + 1;
        const auto length = std::max<collab::Tick> (collab::kPpq, map.barToTick (endBar) - atTick);

        for (auto& part : r.parts)
        {
            collab::MidiClip clip;
            clip.id = collab::generateUuid();
            clip.startTick = atTick;
            clip.lengthTick = length;
            clip.notes = part.notes;

            if (target != nullptr && r.parts.size() == 1)
            {
                clipsForExisting.push_back ({ target->id, clip });
                continue;
            }

            collab::Track t;
            t.id = collab::generateUuid();
            t.type = collab::TrackType::midi;
            t.name = part.name;
            t.color = toStd (Theme::trackColourHex (colourIndex++));

            const auto instrumentId = part.builtinInstrument();
            auto* manifest = library.findLatest (instrumentId);
            collab::Instrument inst;
            inst.kind = collab::Instrument::Kind::builtin;
            inst.id = instrumentId;
            inst.version = manifest != nullptr ? manifest->version : "1.0.0";
            inst.params = manifest != nullptr ? manifest->defaultParams : nlohmann::json::object();
            t.instrument = inst;
            t.midiClips.push_back (clip);
            newTracks.push_back (std::move (t));
        }
    }

    if (! errors.isEmpty())
        Dialogs::showError ("MIDI ファイルの読み込み"_ju, errors.joinIntoString ("\n"));

    if (newTracks.empty() && clipsForExisting.empty())
        return;

    const bool takeTempo = projectEmpty && atTick == 0;
    const auto afterId = state.selectedTrackId;

    document.perform ("MIDI ファイルの読み込み"_ju, [=] (collab::Project& p)
    {
        for (auto& [id, clip] : clipsForExisting)
            if (auto* t = p.findTrack (id))
                t->midiClips.push_back (clip);

        const int i = p.indexOfTrack (afterId);
        p.tracks.insert (i >= 0 ? p.tracks.begin() + i + 1 : p.tracks.end(), newTracks.begin(), newTracks.end());

        // 空のプロジェクトに読み込むときは、ファイルのテンポと拍子に合わせる
        if (takeTempo)
        {
            if (bpm && ! p.tempoTrack.events.empty())
                p.tempoTrack.events.front().bpm = std::round (*bpm * 100.0) / 100.0;

            if (meter && ! p.meterTrack.events.empty())
                std::tie (p.meterTrack.events.front().numerator, p.meterTrack.events.front().denominator) = *meter;
        }
    });

    if (! newTracks.empty())
    {
        state.selectedTrackId = newTracks.front().id;
        state.selectedClipId = newTracks.front().midiClips.front().id;
    }
    else
    {
        state.selectedTrackId = clipsForExisting.front().first;
        state.selectedClipId = clipsForExisting.front().second.id;
    }

    state.changed();
}
