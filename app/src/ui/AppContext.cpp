#include "AppContext.h"

#include "Theme.h"
#include "collab/Uuid.h"
#include "collab/ClipEditing.h"
#include "Dialogs.h"
#include "SyncUI.h"
#include "audio/AudioFiles.h"

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
