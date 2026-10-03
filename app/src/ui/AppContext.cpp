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

    collab::Instrument inst;
    inst.kind = collab::Instrument::Kind::builtin;
    inst.id = instrumentId;
    inst.version = manifest != nullptr ? manifest->version : "0.1.0";
    inst.params = manifest != nullptr ? manifest->defaultParams : nlohmann::json::object();
    addMidiTrack (inst, name);
}

void AppContext::addExternalMidiTrack (const juce::PluginDescription& desc)
{
    collab::Instrument inst;
    inst.kind = collab::Instrument::Kind::external;
    inst.plugin = PluginHost::describe (desc);
    inst.stateRef = PluginHost::stateRefFor (collab::generateUuid());
    openInstrumentEditorSoon (addMidiTrack (inst, desc.name));
}

std::string AppContext::addMidiTrack (const collab::Instrument& inst, const juce::String& name)
{
    collab::Track t;
    t.id = collab::generateUuid();
    t.type = collab::TrackType::midi;
    t.name = toStd (name);
    t.color = toStd (Theme::trackColourHex ((int) document.getProject().tracks.size()));
    t.instrument = inst;

    // 選択中のトラックの下に追加する
    const auto afterId = state.selectedTrackId;

    document.perform ("トラックの追加"_ju, [t, afterId] (collab::Project& p)
    {
        const int i = p.indexOfTrack (afterId);
        p.tracks.insert (i >= 0 ? p.tracks.begin() + i + 1 : p.tracks.end(), t);
    });

    state.selectedTrackId = t.id;
    state.selectClip ({});
    state.changed();
    return t.id;
}

void AppContext::openInstrumentEditorSoon (const std::string& trackId)
{
    if (openPluginEditorSoon)
        openPluginEditorSoon (trackId);
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
    state.selectClip ({});
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
    const bool newTrack = track == nullptr || track->type != collab::TrackType::audio;

    if (newTrack)
        trackId = addAudioTrack (imported.front().displayName);

    // 新しく作ったトラックは、読み込んだファイルに合わせてモノ / ステレオにする（あとからインスペクターで変えられる）
    const int channels = imported.front().numChannels;

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

    document.perform ("オーディオの読み込み"_ju, [trackId, clips, newTrack, channels] (collab::Project& p)
    {
        if (auto* t = p.findTrack (trackId))
        {
            if (newTrack)
                t->inputChannels = t->outputChannels = channels;

            for (auto& c : clips)
                t->audioClips.push_back (c);
        }
    });

    state.selectedTrackId = trackId;
    state.selectClip (clips.front().id);
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

    openInstrumentEditorSoon (trackId);
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

void AppContext::addBuiltinEffect (const std::string& trackId, collab::fx::Type type)
{
    auto* t = document.getProject().findTrack (trackId);

    if (t == nullptr)
        return;

    collab::Effect e;
    e.id = collab::generateUuid();
    e.builtin = collab::fx::idOf (type);
    e.params = collab::fx::defaultParams (type, t->type == collab::TrackType::bus);

    document.perform ("エフェクトの追加"_ju, [trackId, e] (collab::Project& p)
    {
        if (auto* tr = p.findTrack (trackId))
            tr->effects.push_back (e);
    });

    // 追加したらすぐ画面を開く
    if (openPluginEditor)
        openPluginEditor (trackId, e.id);
}

juce::String AppContext::effectName (const collab::Effect& e)
{
    if (auto type = collab::fx::typeFromId (e.builtin))
        return juce::String::fromUTF8 (collab::fx::displayName (*type).c_str());

    return toJuce (e.plugin.name);
}

juce::PopupMenu AppContext::addEffectMenu (const std::string& trackId)
{
    // 内蔵エフェクト（誰の PC でも同じに鳴る）を先に、その下に外部プラグイン
    juce::PopupMenu add;
    add.addSectionHeader ("内蔵エフェクト"_ju);

    for (auto type : collab::fx::allTypes())
        add.addItem (juce::String::fromUTF8 (collab::fx::displayName (type).c_str()), [this, trackId, type] { addBuiltinEffect (trackId, type); });

    add.addSectionHeader ("外部プラグイン"_ju);
    const int before = add.getNumItems();

    for (auto& d : PluginHost::list (engine.getEngine(), false))
        add.addItem (d.name + " (" + d.manufacturerName + ")", [this, trackId, d] { addEffect (trackId, d); });

    if (add.getNumItems() == before)
        add.addItem ("プラグインがありません（設定 → プラグイン… でスキャン）"_ju, false, false, nullptr);

    return add;
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
        state.selectClip (newTracks.front().midiClips.front().id);
    }
    else
    {
        state.selectedTrackId = clipsForExisting.front().first;
        state.selectClip (clipsForExisting.front().second.id);
    }

    state.changed();
}

//==============================================================================
std::optional<AppContext::ClipRef> AppContext::findClip (const std::string& clipId) const
{
    for (auto& t : document.getProject().tracks)
    {
        for (auto& c : t.midiClips)
            if (c.id == clipId)
                return ClipRef { t.id, false };

        for (auto& c : t.audioClips)
            if (c.id == clipId)
                return ClipRef { t.id, true };
    }

    return std::nullopt;
}

std::pair<collab::Tick, collab::Tick> AppContext::clipRange (const std::string& clipId) const
{
    const auto& map = document.getTempoMap();

    for (auto& t : document.getProject().tracks)
    {
        for (auto& c : t.midiClips)
            if (c.id == clipId)
                return { c.startTick, c.endTick() };

        for (auto& c : t.audioClips)
            if (c.id == clipId)
                return { c.startTick, collab::audioClipEndTick (c, map) };
    }

    return { 0, 0 };
}

void AppContext::splitClipAt (const std::string& clipId, collab::Tick at)
{
    auto ref = findClip (clipId);

    if (! ref)
        return;

    const auto [start, end] = clipRange (clipId);

    if (at <= start || at >= end)
        return;

    const auto newId = collab::generateUuid();
    const auto trackId = ref->trackId;
    const auto& map = document.getTempoMap();

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

void AppContext::deleteClips (const std::set<std::string>& ids)
{
    if (ids.empty())
        return;

    document.perform (ids.size() > 1 ? "クリップの削除（"_ju + juce::String ((int) ids.size()) + " 個）"_ju : "クリップの削除"_ju,
                      [ids] (collab::Project& p)
    {
        for (auto& t : p.tracks)
        {
            std::erase_if (t.midiClips, [&] (auto& c) { return ids.count (c.id) > 0; });
            std::erase_if (t.audioClips, [&] (auto& c) { return ids.count (c.id) > 0; });
        }
    });

    state.selectClip ({});
    state.changed();
}

void AppContext::copyClips (const std::set<std::string>& ids)
{
    if (ids.empty())
        return;

    ClipClipboard cb;
    cb.origin = std::numeric_limits<collab::Tick>::max();

    for (auto& t : document.getProject().tracks)
    {
        for (auto& c : t.midiClips)
            if (ids.count (c.id) > 0)
            {
                cb.midi.push_back ({ t.id, c });
                cb.origin = std::min (cb.origin, c.startTick);
            }

        for (auto& c : t.audioClips)
            if (ids.count (c.id) > 0)
            {
                cb.audio.push_back ({ t.id, c });
                cb.origin = std::min (cb.origin, c.startTick);
            }
    }

    if (! cb.midi.empty() || ! cb.audio.empty())
        clipboard = std::move (cb);
}

void AppContext::pasteClips (collab::Tick at)
{
    if (! hasClipsInClipboard())
        return;

    const auto& project = document.getProject();
    const auto* selected = selectedTrack();
    auto cb = clipboard;
    const auto offset = at - cb.origin;
    std::set<std::string> newIds;

    // 貼り付け先: 元のトラックがあればそこ。1 つのトラックだけからのコピーなら、選択中の同じ種類のトラックへ
    std::set<std::string> sourceTracks;
    for (auto& [id, c] : cb.midi)  sourceTracks.insert (id);
    for (auto& [id, c] : cb.audio) sourceTracks.insert (id);

    auto targetFor = [&] (const std::string& from, collab::TrackType type) -> std::string
    {
        if (sourceTracks.size() == 1 && selected != nullptr && selected->type == type)
            return selected->id;

        auto* t = project.findTrack (from);
        return t != nullptr ? from : std::string();
    };

    for (auto& [trackId, c] : cb.midi)
    {
        trackId = targetFor (trackId, collab::TrackType::midi);
        c.id = collab::generateUuid();
        c.startTick = std::max<collab::Tick> (0, c.startTick + offset);

        for (auto& n : c.notes)
            n.id = collab::generateUuid();

        newIds.insert (c.id);
    }

    for (auto& [trackId, c] : cb.audio)
    {
        trackId = targetFor (trackId, collab::TrackType::audio);
        c.id = collab::generateUuid();
        c.startTick = std::max<collab::Tick> (0, c.startTick + offset);
        newIds.insert (c.id);
    }

    document.perform ("貼り付け"_ju, [cb] (collab::Project& p)
    {
        for (auto& [trackId, c] : cb.midi)
            if (auto* t = p.findTrack (trackId))
                t->midiClips.push_back (c);

        for (auto& [trackId, c] : cb.audio)
            if (auto* t = p.findTrack (trackId))
                t->audioClips.push_back (c);
    });

    state.selectedClipIds = newIds;
    state.selectedClipId = newIds.empty() ? std::string() : *newIds.begin();
    state.changed();
}

void AppContext::duplicateClips (const std::set<std::string>& ids)
{
    if (ids.empty())
        return;

    // 選んだクリップ全体の終わりの位置に、同じ並びで置く（Cubase の「複製」）
    collab::Tick end = 0;

    for (auto& id : ids)
        end = std::max (end, clipRange (id).second);

    auto saved = clipboard;
    copyClips (ids);
    auto* selected = selectedTrack();
    auto keepTrack = state.selectedTrackId;

    // 複製は元のトラックに置く（選択中のトラックへは移さない）
    state.selectedTrackId = {};
    pasteClips (end);
    state.selectedTrackId = keepTrack;
    clipboard = saved;
    juce::ignoreUnused (selected);
}

void AppContext::nudgeClips (const std::set<std::string>& ids, collab::Tick delta)
{
    if (ids.empty())
        return;

    document.perform ("クリップの移動"_ju, [ids, delta] (collab::Project& p)
    {
        for (auto& t : p.tracks)
        {
            for (auto& c : t.midiClips)  if (ids.count (c.id) > 0) c.startTick = std::max<collab::Tick> (0, c.startTick + delta);
            for (auto& c : t.audioClips) if (ids.count (c.id) > 0) c.startTick = std::max<collab::Tick> (0, c.startTick + delta);
        }
    });
}

//==============================================================================
std::string AppContext::addBusTrack (const juce::String& name)
{
    collab::Track t;
    t.id = collab::generateUuid();
    t.type = collab::TrackType::bus;
    t.name = toStd (name);
    t.color = "#9575CD";

    const auto afterId = state.selectedTrackId;

    document.perform ("バストラックの追加"_ju, [t, afterId] (collab::Project& p)
    {
        const int i = p.indexOfTrack (afterId);
        p.tracks.insert (i >= 0 ? p.tracks.begin() + i + 1 : p.tracks.end(), t);
    });

    state.selectedTrackId = t.id;
    state.selectClip ({});
    state.changed();
    return t.id;
}

void AppContext::editTrack (const std::string& trackId, const juce::String& description, std::function<void (collab::Track&)> fn,
                            const juce::String& mergeId)
{
    document.perform (description, [trackId, fn = std::move (fn)] (collab::Project& p)
    {
        if (auto* t = p.findTrack (trackId))
            fn (*t);
    }, mergeId);
}

void AppContext::setTrackOutput (const std::string& trackId, const std::string& busId)
{
    document.perform ("出力先の変更"_ju, [trackId, busId] (collab::Project& p)
    {
        if (auto* t = p.findTrack (trackId))
            t->output = busId == trackId ? std::string() : busId;
    });
}

void AppContext::setSend (const std::string& trackId, const std::string& busId, std::optional<double> levelDb,
                          std::optional<bool> preFader, const juce::String& mergeId)
{
    document.perform ("センド"_ju, [trackId, busId, levelDb, preFader] (collab::Project& p)
    {
        auto* t = p.findTrack (trackId);

        if (t == nullptr || busId == trackId)
            return;

        auto it = std::find_if (t->sends.begin(), t->sends.end(), [&] (auto& s) { return s.busId == busId; });

        if (it == t->sends.end())
        {
            t->sends.push_back ({ busId, -10.0, false });
            it = std::prev (t->sends.end());
        }

        if (levelDb)  it->levelDb = juce::jlimit (-100.0, 12.0, *levelDb);
        if (preFader) it->preFader = *preFader;
    }, mergeId);
}

void AppContext::removeSend (const std::string& trackId, const std::string& busId)
{
    document.perform ("センドの削除"_ju, [trackId, busId] (collab::Project& p)
    {
        if (auto* t = p.findTrack (trackId))
            std::erase_if (t->sends, [&] (auto& s) { return s.busId == busId; });
    });
}

juce::String AppContext::outputName (const collab::Track& t) const
{
    if (auto* bus = document.getProject().findTrack (t.output); bus != nullptr && bus->type == collab::TrackType::bus)
        return toJuce (bus->name);

    return "マスター"_ju;
}

juce::PopupMenu AppContext::outputMenu (const std::string& trackId)
{
    juce::PopupMenu output;
    const auto& project = document.getProject();
    auto* track = project.findTrack (trackId);

    if (track == nullptr)
        return output;

    // 出力先: マスター、または他のバス（自分自身と、自分へ出力しているバスは除く）
    output.addItem ("マスター"_ju, true, track->output.empty(), [this, trackId] { setTrackOutput (trackId, {}); });

    for (auto& b : project.tracks)
    {
        if (b.type != collab::TrackType::bus || b.id == trackId || b.output == trackId)
            continue;

        output.addItem (toJuce (b.name), true, track->output == b.id, [this, trackId, id = b.id] { setTrackOutput (trackId, id); });
    }

    output.addSeparator();
    output.addItem ("新しいバスを作って出力…"_ju, [this, trackId, name = track->name]
    {
        const auto busId = addBusTrack (toJuce (name) + " Bus");
        setTrackOutput (trackId, busId);
    });

    return output;
}

juce::PopupMenu AppContext::sendMenu (const std::string& trackId)
{
    juce::PopupMenu sends;
    const auto& project = document.getProject();
    auto* track = project.findTrack (trackId);

    if (track == nullptr)
        return sends;

    for (auto& b : project.tracks)
    {
        if (b.type != collab::TrackType::bus || b.id == trackId)
            continue;

        auto it = std::find_if (track->sends.begin(), track->sends.end(), [&] (auto& s) { return s.busId == b.id; });

        if (it == track->sends.end())
        {
            sends.addItem (toJuce (b.name) + " へ送る"_ju, [this, trackId, id = b.id] { setSend (trackId, id, -10.0, false); });
        }
        else
        {
            juce::PopupMenu one;
            one.addItem ("プリフェーダー（音量の前から送る）"_ju, true, it->preFader,
                         [this, trackId, id = b.id, pre = it->preFader] { setSend (trackId, id, std::nullopt, ! pre); });
            one.addItem ("センドを外す"_ju, [this, trackId, id = b.id] { removeSend (trackId, id); });
            sends.addSubMenu (toJuce (b.name) + "（"_ju + juce::String (it->levelDb, 1) + " dB）"_ju, one);
        }
    }

    if (sends.getNumItems() == 0)
        sends.addItem ("バストラックがありません"_ju, false, false, nullptr);

    sends.addSeparator();
    sends.addItem ("新しいバスを作って送る…"_ju, [this, trackId]
    {
        const auto busId = addBusTrack ("FX"_ju);
        setSend (trackId, busId, -10.0, false);
    });

    return sends;
}

juce::PopupMenu AppContext::routingMenu (const std::string& trackId)
{
    juce::PopupMenu m;
    auto* track = document.getProject().findTrack (trackId);

    if (track == nullptr)
        return m;

    m.addSubMenu ("出力先: "_ju + outputName (*track), outputMenu (trackId));
    m.addSubMenu ("センド"_ju, sendMenu (trackId));
    return m;
}

//==============================================================================
std::vector<AppContext::InputChoice> AppContext::inputChoices (const std::string& trackId) const
{
    std::vector<InputChoice> result;
    const auto inputs = engine.getAudioInputs();
    auto* t = document.getProject().findTrack (trackId);
    const bool stereo = t != nullptr && t->inputChannels == 2;

    if (! stereo)
    {
        for (auto& name : inputs)
            result.push_back ({ name, name, {} });

        return result;
    }

    // ステレオ: 1+2、3+4 … のように隣り合う 2 つ（奇数個なら最後は使わない）
    for (int i = 0; i + 1 < inputs.size(); i += 2)
        result.push_back ({ inputs[i] + " + " + inputs[i + 1], inputs[i], inputs[i + 1] });

    return result;
}

void AppContext::toggleRecordArm (const std::string& trackId)
{
    auto* t = document.getProject().findTrack (trackId);

    if (t == nullptr)
        return;

    // MIDI トラック: MIDI キーボードで弾いたものを録音する（録音待機にできる MIDI トラックは 1 つ）
    if (t->type == collab::TrackType::midi)
    {
        const bool arm = state.midiArmedTrackId != trackId;
        state.midiArmedTrackId = arm ? trackId : std::string();
        state.changed();

        const auto inputs = engine.getMidiInputs();

        if (arm && std::none_of (inputs.begin(), inputs.end(), [] (auto& m) { return m.enabled; }))
            Dialogs::showInfo ("録音待機"_ju, "MIDI キーボードが見つかりません。つないでから、設定 → オーディオ・MIDI の設定で有効にしてください。"_ju);

        return;
    }

    if (t->type != collab::TrackType::audio)
        return;

    auto in = engine.getTrackInput (trackId);
    const bool stereo = t->inputChannels == 2;

    // 入力がない・トラックのモノ / ステレオと合わないときは、最初の選択肢を割り当てる
    if (in.device.isEmpty() || stereo != in.deviceRight.isNotEmpty())
    {
        const auto choices = inputChoices (trackId);

        if (choices.empty())
            return Dialogs::showInfo ("録音待機"_ju, stereo ? "ステレオで録音できる入力（2 つ）がありません。設定 → オーディオ・MIDI の設定で入力を有効にするか、インスペクターで入力をモノにしてください。"_ju
                                                             : "録音できる入力がありません。設定 → オーディオ・MIDI の設定で入力を有効にしてください。"_ju);

        in.device = choices.front().left;
        in.deviceRight = choices.front().right;
    }

    in.armed = ! in.armed;
    engine.setTrackInput (trackId, in);
    state.changed();   // 他のトラックの表示も更新（入力は 1 つのトラックにだけ割り当てる）
}
