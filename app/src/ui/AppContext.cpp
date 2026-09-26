#include "AppContext.h"

#include "Theme.h"
#include "collab/Uuid.h"

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
