#include "collab/ProjectJson.h"

#include <nlohmann/json-schema.hpp>

#include "collab/Unicode.h"
#include "ProjectSchemaData.h"   // CMake が /shared/schema/project.schema.json から生成

namespace collab
{

using ojson = nlohmann::ordered_json;
using json = nlohmann::json;

//==============================================================================
ojson canonicalise (const json& j)
{
    if (j.is_object())
    {
        ojson o = ojson::object();

        // nlohmann::json（std::map）はキーがソート済み
        for (auto it = j.begin(); it != j.end(); ++it)
            o[toNfc (it.key())] = canonicalise (it.value());

        return o;
    }

    if (j.is_array())
    {
        ojson a = ojson::array();

        for (auto& e : j)
            a.push_back (canonicalise (e));

        return a;
    }

    if (j.is_string())
        return toNfc (j.get<std::string>());

    return ojson (j);
}

//==============================================================================
namespace
{
    ojson toJson (const InstrumentRef& r)
    {
        return { { "id", r.id }, { "version", r.version } };
    }

    ojson toJson (const ExternalPlugin& p)
    {
        return { { "format", p.format }, { "name", toNfc (p.name) }, { "vendor", toNfc (p.vendor) },
                 { "uid", p.uid }, { "os", p.os } };
    }

    ojson toJson (const Instrument& i)
    {
        ojson o;

        if (i.kind == Instrument::Kind::builtin)
        {
            o["kind"] = "builtin";
            o["id"] = i.id;
            o["version"] = i.version;
            o["params"] = canonicalise (i.params.is_object() ? i.params : json::object());
        }
        else
        {
            o["kind"] = "external";
            o["plugin"] = toJson (i.plugin);

            if (! i.stateRef.empty())
                o["stateRef"] = i.stateRef;
        }

        return o;
    }

    ojson toJson (const Effect& e)
    {
        ojson o;
        o["id"] = e.id;
        o["plugin"] = toJson (e.plugin);

        if (! e.stateRef.empty())
            o["stateRef"] = e.stateRef;

        if (e.bypass)
            o["bypass"] = true;

        return o;
    }

    ojson toJson (const Render& r)
    {
        return { { "audioHash", r.audioHash }, { "renderedAt", r.renderedAt },
                 { "sourceFingerprint", r.sourceFingerprint }, { "tailSeconds", r.tailSeconds } };
    }

    ojson toJson (const Note& n)
    {
        return { { "id", n.id }, { "tick", n.tick }, { "lengthTick", n.lengthTick },
                 { "pitch", n.pitch }, { "velocity", n.velocity } };
    }

    ojson toJson (const MidiClip& c)
    {
        ojson notes = ojson::array();

        for (auto& n : c.notes)
            notes.push_back (toJson (n));

        return { { "id", c.id }, { "startTick", c.startTick }, { "lengthTick", c.lengthTick }, { "notes", notes } };
    }

    ojson toJson (const AudioClip& c)
    {
        return { { "id", c.id }, { "startTick", c.startTick }, { "audioHash", c.audioHash },
                 { "displayName", toNfc (c.displayName) },
                 { "sourceOffsetSamples", c.sourceOffsetSamples }, { "lengthSamples", c.lengthSamples },
                 { "gainDb", c.gainDb }, { "fadeInSamples", c.fadeInSamples }, { "fadeOutSamples", c.fadeOutSamples } };
    }

    ojson toJson (const ChannelStrip& s)
    {
        ojson eq;
        eq["enabled"] = s.eq.enabled;
        eq["lowCutHz"] = s.eq.lowCutHz;
        eq["highCutHz"] = s.eq.highCutHz;
        eq["lowGainDb"] = s.eq.lowGainDb;
        eq["lowFreqHz"] = s.eq.lowFreqHz;
        eq["lowMidGainDb"] = s.eq.lowMidGainDb;
        eq["lowMidFreqHz"] = s.eq.lowMidFreqHz;
        eq["lowMidQ"] = s.eq.lowMidQ;
        eq["midGainDb"] = s.eq.midGainDb;
        eq["midFreqHz"] = s.eq.midFreqHz;
        eq["midQ"] = s.eq.midQ;
        eq["highGainDb"] = s.eq.highGainDb;
        eq["highFreqHz"] = s.eq.highFreqHz;

        ojson comp;
        comp["enabled"] = s.comp.enabled;
        comp["type"] = compTypeName (s.comp.type);
        comp["thresholdDb"] = s.comp.thresholdDb;
        comp["ratio"] = s.comp.ratio;
        comp["attackMs"] = s.comp.attackMs;
        comp["releaseMs"] = s.comp.releaseMs;
        comp["makeupDb"] = s.comp.makeupDb;

        if (s.comp.sidechainHpHz > 0.0)
            comp["sidechainHpHz"] = s.comp.sidechainHpHz;

        ojson o;
        o["eq"] = eq;
        o["comp"] = comp;

        if (s.compFirst)
            o["order"] = "compEq";

        return o;
    }

    ojson toJson (const Track& t)
    {
        ojson o;
        o["id"] = t.id;
        o["type"] = trackTypeName (t.type);
        o["name"] = toNfc (t.name);
        o["color"] = t.color;
        o["volumeDb"] = t.volumeDb;
        o["pan"] = t.pan;
        o["mute"] = t.mute;
        o["solo"] = t.solo;

        if (t.type == TrackType::midi && t.instrument)
            o["instrument"] = toJson (*t.instrument);

        if (! t.effects.empty())
        {
            ojson fx = ojson::array();

            for (auto& e : t.effects)
                fx.push_back (toJson (e));

            o["effects"] = fx;
        }

        if (! t.strip.isDefault())
            o["strip"] = toJson (t.strip);

        if (! t.output.empty())
            o["output"] = t.output;

        if (! t.sends.empty())
        {
            ojson sends = ojson::array();

            for (auto& s : t.sends)
                sends.push_back ({ { "busId", s.busId }, { "levelDb", s.levelDb }, { "preFader", s.preFader } });

            o["sends"] = sends;
        }

        if (t.render)
            o["render"] = toJson (*t.render);

        ojson clips = ojson::array();

        if (t.type == TrackType::midi)
            for (auto& c : t.midiClips)
                clips.push_back (toJson (c));
        else
            for (auto& c : t.audioClips)
                clips.push_back (toJson (c));

        o["clips"] = clips;
        return o;
    }

    //==============================================================================
    template <typename T>
    T get (const json& j, const char* key)
    {
        return j.at (key).get<T>();
    }

    template <typename T>
    T getOr (const json& j, const char* key, T fallback)
    {
        auto it = j.find (key);
        return it != j.end() && ! it->is_null() ? it->get<T>() : fallback;
    }

    ChannelStrip stripFromJson (const json& j)
    {
        ChannelStrip s;
        const ChannelStrip d;

        if (auto it = j.find ("eq"); it != j.end())
        {
            auto& e = *it;
            s.eq.enabled = getOr<bool> (e, "enabled", d.eq.enabled);
            s.eq.lowCutHz = getOr<double> (e, "lowCutHz", d.eq.lowCutHz);
            s.eq.highCutHz = getOr<double> (e, "highCutHz", d.eq.highCutHz);
            s.eq.lowMidGainDb = getOr<double> (e, "lowMidGainDb", d.eq.lowMidGainDb);
            s.eq.lowMidFreqHz = getOr<double> (e, "lowMidFreqHz", d.eq.lowMidFreqHz);
            s.eq.lowMidQ = getOr<double> (e, "lowMidQ", d.eq.lowMidQ);
            s.eq.lowGainDb = getOr<double> (e, "lowGainDb", d.eq.lowGainDb);
            s.eq.lowFreqHz = getOr<double> (e, "lowFreqHz", d.eq.lowFreqHz);
            s.eq.midGainDb = getOr<double> (e, "midGainDb", d.eq.midGainDb);
            s.eq.midFreqHz = getOr<double> (e, "midFreqHz", d.eq.midFreqHz);
            s.eq.midQ = getOr<double> (e, "midQ", d.eq.midQ);
            s.eq.highGainDb = getOr<double> (e, "highGainDb", d.eq.highGainDb);
            s.eq.highFreqHz = getOr<double> (e, "highFreqHz", d.eq.highFreqHz);
        }

        if (auto it = j.find ("comp"); it != j.end())
        {
            auto& c = *it;
            s.comp.enabled = getOr<bool> (c, "enabled", d.comp.enabled);
            s.comp.type = getOr<std::string> (c, "type", "fet") == "opto" ? CompType::opto : CompType::fet;
            s.comp.thresholdDb = getOr<double> (c, "thresholdDb", d.comp.thresholdDb);
            s.comp.ratio = getOr<double> (c, "ratio", d.comp.ratio);
            s.comp.attackMs = getOr<double> (c, "attackMs", d.comp.attackMs);
            s.comp.releaseMs = getOr<double> (c, "releaseMs", d.comp.releaseMs);
            s.comp.makeupDb = getOr<double> (c, "makeupDb", d.comp.makeupDb);
            s.comp.sidechainHpHz = getOr<double> (c, "sidechainHpHz", d.comp.sidechainHpHz);
        }

        s.compFirst = getOr<std::string> (j, "order", "eqComp") == "compEq";

        return s;
    }

    InstrumentRef instrumentRefFromJson (const json& j)
    {
        return { get<std::string> (j, "id"), get<std::string> (j, "version") };
    }

    ExternalPlugin pluginFromJson (const json& j)
    {
        return { get<std::string> (j, "format"), toNfc (get<std::string> (j, "name")),
                 toNfc (get<std::string> (j, "vendor")), get<std::string> (j, "uid"), get<std::string> (j, "os") };
    }

    Instrument instrumentFromJson (const json& j)
    {
        Instrument i;

        if (get<std::string> (j, "kind") == "builtin")
        {
            i.kind = Instrument::Kind::builtin;
            i.id = get<std::string> (j, "id");
            i.version = get<std::string> (j, "version");
            i.params = j.at ("params");
        }
        else
        {
            i.kind = Instrument::Kind::external;
            i.plugin = pluginFromJson (j.at ("plugin"));
            i.stateRef = getOr<std::string> (j, "stateRef", {});
        }

        return i;
    }
}

//==============================================================================
ojson projectToJson (const Project& source)
{
    Project p = source;
    p.sortCanonical();

    ojson o;
    o["schemaVersion"] = p.schemaVersion;
    o["projectId"] = p.projectId;
    o["name"] = toNfc (p.name);
    o["sampleRate"] = p.sampleRate;
    o["ppq"] = p.ppq;

    {
        ojson events = ojson::array();

        for (auto& e : p.tempoTrack.events)
            events.push_back ({ { "id", e.id }, { "tick", e.tick }, { "bpm", e.bpm } });

        o["tempoTrack"] = { { "id", p.tempoTrack.id }, { "events", events } };
    }

    {
        ojson events = ojson::array();

        for (auto& e : p.meterTrack.events)
            events.push_back ({ { "id", e.id }, { "bar", e.bar }, { "numerator", e.numerator }, { "denominator", e.denominator } });

        o["meterTrack"] = { { "id", p.meterTrack.id }, { "events", events } };
    }

    {
        ojson events = ojson::array();

        for (auto& e : p.chordTrack.events)
        {
            ojson ev;
            ev["id"] = e.id;
            ev["tick"] = e.tick;

            if (e.noChord)
            {
                ev["noChord"] = true;
            }
            else if (e.chord)
            {
                ojson tensions = ojson::array();

                for (auto& t : e.chord->tensions)
                    tensions.push_back (t);

                ev["chord"] = { { "root", e.chord->root }, { "quality", e.chord->quality }, { "tensions", tensions },
                                { "bass", e.chord->bass ? ojson (*e.chord->bass) : ojson (nullptr) } };
            }

            if (! e.text.empty())
                ev["text"] = toNfc (e.text);

            events.push_back (ev);
        }

        auto& pb = p.chordTrack.playback;
        o["chordTrack"] = { { "id", p.chordTrack.id },
                            { "playback", { { "enabled", pb.enabled }, { "volumeDb", pb.volumeDb }, { "instrument", toJson (pb.instrument) } } },
                            { "events", events } };
    }

    {
        ojson events = ojson::array();

        for (auto& e : p.markerTrack.events)
            events.push_back ({ { "id", e.id }, { "tick", e.tick }, { "name", toNfc (e.name) } });

        o["markerTrack"] = { { "id", p.markerTrack.id.empty() ? markerTrackIdFor (p.projectId) : p.markerTrack.id }, { "events", events } };
    }

    // キートラック（空のときは省略する。古いアプリ・サーバーでも読めるように）
    if (! p.keyTrack.events.empty())
    {
        ojson events = ojson::array();

        for (auto& e : p.keyTrack.events)
            events.push_back ({ { "id", e.id }, { "bar", e.bar }, { "tonic", e.tonic }, { "mode", e.minor ? "minor" : "major" } });

        o["keyTrack"] = { { "id", p.keyTrack.id.empty() ? keyTrackIdFor (p.projectId) : p.keyTrack.id }, { "events", events } };
    }

    // マスター（既定値のときは省略する。古いアプリ・サーバーでも読めるように）
    if (! p.master.isDefault())
    {
        const auto& l = p.master.limiter;
        o["master"] = { { "id", p.master.id.empty() ? masterBusIdFor (p.projectId) : p.master.id },
                        { "limiter", { { "enabled", l.enabled }, { "thresholdDb", l.thresholdDb }, { "ceilingDb", l.ceilingDb },
                                       { "character", l.character }, { "mode", limiterModeName (l.mode) } } } };
    }

    ojson tracks = ojson::array();

    for (auto& t : p.tracks)
        tracks.push_back (toJson (t));

    o["tracks"] = tracks;
    return o;
}

//==============================================================================
const json& projectSchema()
{
    static const json schema = json::parse (std::string (collab::detail::projectSchemaText()));
    return schema;
}

std::string validateProjectJson (const json& j)
{
    struct ErrorCollector : nlohmann::json_schema::basic_error_handler
    {
        std::string message;

        void error (const json::json_pointer& ptr, const json& /*instance*/, const std::string& msg) override
        {
            basic_error_handler::error (ptr, {}, msg);

            if (message.empty())
                message = (ptr.to_string().empty() ? std::string ("/") : ptr.to_string()) + ": " + msg;
        }
    };

    static const auto validator = []
    {
        auto v = std::make_unique<nlohmann::json_schema::json_validator>();
        v->set_root_schema (projectSchema());
        return v;
    }();

    ErrorCollector errors;
    validator->validate (j, errors);
    return errors.message;
}

Project projectFromJson (const json& j)
{
    if (auto err = validateProjectJson (j); ! err.empty())
        throw ProjectFormatError ("プロジェクトJSONがスキーマに適合しません: " + err);

    try
    {
        Project p;
        p.schemaVersion = get<int> (j, "schemaVersion");
        p.projectId = get<std::string> (j, "projectId");
        p.name = toNfc (get<std::string> (j, "name"));
        p.sampleRate = get<int> (j, "sampleRate");
        p.ppq = get<int> (j, "ppq");

        auto& tt = j.at ("tempoTrack");
        p.tempoTrack.id = get<std::string> (tt, "id");

        for (auto& e : tt.at ("events"))
            p.tempoTrack.events.push_back ({ get<std::string> (e, "id"), get<Tick> (e, "tick"), get<double> (e, "bpm") });

        auto& mt = j.at ("meterTrack");
        p.meterTrack.id = get<std::string> (mt, "id");

        for (auto& e : mt.at ("events"))
            p.meterTrack.events.push_back ({ get<std::string> (e, "id"), get<int> (e, "bar"),
                                             get<int> (e, "numerator"), get<int> (e, "denominator") });

        auto& ct = j.at ("chordTrack");
        p.chordTrack.id = get<std::string> (ct, "id");

        {
            auto& pb = ct.at ("playback");
            p.chordTrack.playback = { get<bool> (pb, "enabled"), get<double> (pb, "volumeDb"),
                                      instrumentRefFromJson (pb.at ("instrument")) };
        }

        for (auto& e : ct.at ("events"))
        {
            ChordEvent ev;
            ev.id = get<std::string> (e, "id");
            ev.tick = get<Tick> (e, "tick");
            ev.noChord = getOr<bool> (e, "noChord", false);
            ev.text = toNfc (getOr<std::string> (e, "text", {}));

            if (auto it = e.find ("chord"); it != e.end() && ! ev.noChord)
            {
                ChordSymbol cs;
                cs.root = get<std::string> (*it, "root");
                cs.quality = get<std::string> (*it, "quality");
                cs.tensions = get<std::vector<std::string>> (*it, "tensions");

                if (! it->at ("bass").is_null())
                    cs.bass = get<std::string> (*it, "bass");

                ev.chord = cs;
            }

            p.chordTrack.events.push_back (ev);
        }

        // マーカートラック（古いプロジェクトにはない）
        p.markerTrack.id = markerTrackIdFor (p.projectId);

        if (auto it = j.find ("markerTrack"); it != j.end())
        {
            p.markerTrack.id = get<std::string> (*it, "id");

            for (auto& e : it->at ("events"))
                p.markerTrack.events.push_back ({ get<std::string> (e, "id"), get<Tick> (e, "tick"),
                                                  toNfc (getOr<std::string> (e, "name", {})) });
        }

        // キートラック（ない場合はキー未設定）
        p.keyTrack.id = keyTrackIdFor (p.projectId);

        if (auto it = j.find ("keyTrack"); it != j.end())
        {
            p.keyTrack.id = getOr<std::string> (*it, "id", p.keyTrack.id);

            for (auto& e : it->at ("events"))
                p.keyTrack.events.push_back ({ get<std::string> (e, "id"), get<int> (e, "bar"), get<int> (e, "tonic"),
                                               getOr<std::string> (e, "mode", "major") == "minor" });
        }

        // マスター（ない場合は既定値）
        p.master.id = masterBusIdFor (p.projectId);

        if (auto it = j.find ("master"); it != j.end())
        {
            p.master.id = getOr<std::string> (*it, "id", p.master.id);

            if (auto l = it->find ("limiter"); l != it->end())
            {
                auto& m = p.master.limiter;
                m.enabled = getOr<bool> (*l, "enabled", m.enabled);
                m.thresholdDb = getOr<double> (*l, "thresholdDb", m.thresholdDb);
                m.ceilingDb = getOr<double> (*l, "ceilingDb", m.ceilingDb);
                m.character = getOr<double> (*l, "character", m.character);
                m.mode = limiterModeFromName (getOr<std::string> (*l, "mode", "analog"));
            }
        }

        for (auto& tj : j.at ("tracks"))
        {
            Track t;
            t.id = get<std::string> (tj, "id");
            const auto type = get<std::string> (tj, "type");
            t.type = type == "midi" ? TrackType::midi : type == "bus" ? TrackType::bus : TrackType::audio;
            t.name = toNfc (get<std::string> (tj, "name"));
            t.color = get<std::string> (tj, "color");
            t.volumeDb = get<double> (tj, "volumeDb");
            t.pan = get<double> (tj, "pan");
            t.mute = get<bool> (tj, "mute");
            t.solo = get<bool> (tj, "solo");

            if (auto it = tj.find ("instrument"); it != tj.end())
                t.instrument = instrumentFromJson (*it);

            if (auto it = tj.find ("effects"); it != tj.end())
                for (auto& ej : *it)
                    t.effects.push_back ({ get<std::string> (ej, "id"), pluginFromJson (ej.at ("plugin")),
                                           getOr<std::string> (ej, "stateRef", {}), getOr<bool> (ej, "bypass", false) });

            if (auto it = tj.find ("strip"); it != tj.end())
                t.strip = stripFromJson (*it);

            t.output = getOr<std::string> (tj, "output", {});

            if (auto it = tj.find ("sends"); it != tj.end())
                for (auto& sj : *it)
                    t.sends.push_back ({ get<std::string> (sj, "busId"), getOr<double> (sj, "levelDb", 0.0), getOr<bool> (sj, "preFader", false) });

            if (auto it = tj.find ("render"); it != tj.end())
                t.render = Render { get<std::string> (*it, "audioHash"), get<std::string> (*it, "renderedAt"),
                                    get<std::string> (*it, "sourceFingerprint"), get<double> (*it, "tailSeconds") };

            for (auto& cj : tj.at ("clips"))
            {
                if (t.type == TrackType::midi)
                {
                    MidiClip c;
                    c.id = get<std::string> (cj, "id");
                    c.startTick = get<Tick> (cj, "startTick");
                    c.lengthTick = get<Tick> (cj, "lengthTick");

                    for (auto& nj : cj.at ("notes"))
                        c.notes.push_back ({ get<std::string> (nj, "id"), get<Tick> (nj, "tick"), get<Tick> (nj, "lengthTick"),
                                             get<int> (nj, "pitch"), get<int> (nj, "velocity") });

                    t.midiClips.push_back (std::move (c));
                }
                else
                {
                    AudioClip c;
                    c.id = get<std::string> (cj, "id");
                    c.startTick = get<Tick> (cj, "startTick");
                    c.audioHash = get<std::string> (cj, "audioHash");
                    c.displayName = toNfc (get<std::string> (cj, "displayName"));
                    c.sourceOffsetSamples = get<SampleCount> (cj, "sourceOffsetSamples");
                    c.lengthSamples = get<SampleCount> (cj, "lengthSamples");
                    c.gainDb = get<double> (cj, "gainDb");
                    c.fadeInSamples = get<SampleCount> (cj, "fadeInSamples");
                    c.fadeOutSamples = get<SampleCount> (cj, "fadeOutSamples");
                    t.audioClips.push_back (std::move (c));
                }
            }

            p.tracks.push_back (std::move (t));
        }

        p.sortCanonical();
        return p;
    }
    catch (const json::exception& e)
    {
        throw ProjectFormatError (std::string ("プロジェクトJSONの読み込みに失敗しました: ") + e.what());
    }
}

std::string serialiseProject (const Project& p)
{
    return projectToJson (p).dump (2, ' ', false) + "\n";
}

Project parseProject (const std::string& text)
{
    json j;

    try
    {
        j = json::parse (text);
    }
    catch (const json::parse_error& e)
    {
        throw ProjectFormatError (std::string ("JSONの構文エラー: ") + e.what());
    }

    return projectFromJson (j);
}

} // namespace collab
