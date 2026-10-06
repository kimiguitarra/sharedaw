#include "collab/BuiltinInstruments.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace collab
{

using json = nlohmann::json;

const DrumPiece* BuiltinInstrumentManifest::findPiece (const std::string& key) const
{
    for (auto& p : pieces)
        if (p.key == key)
            return &p;

    return nullptr;
}

const DrumPiece* BuiltinInstrumentManifest::findPieceForNote (int note, std::string* name) const
{
    for (auto& p : pieces)
    {
        if (p.note == note)
        {
            if (name != nullptr)
                *name = p.displayName;

            return &p;
        }

        for (auto& a : p.aliases)
            if (a.note == note)
            {
                if (name != nullptr)
                    *name = a.displayName;

                return &p;
            }
    }

    return nullptr;
}

BuiltinInstrumentManifest BuiltinInstrumentManifest::fromJson (const json& j)
{
    try
    {
        BuiltinInstrumentManifest m;
        m.id = j.at ("id").get<std::string>();
        m.version = j.at ("version").get<std::string>();
        m.samplesFrom = j.value ("samplesFrom", std::string());
        m.displayName = j.value ("displayName", m.id);
        m.type = j.at ("type").get<std::string>();
        m.mainSfz = j.value ("main", std::string());
        m.defaultParams = j.value ("defaultParams", json::object());

        if (j.contains ("credits"))
            m.credits = j.at ("credits").get<std::vector<std::string>>();

        if (m.type == "drums")
        {
            for (auto& p : j.at ("pieces"))
            {
                DrumPiece piece { p.at ("key").get<std::string>(), p.value ("name", p.at ("key").get<std::string>()),
                                  p.at ("note").get<int>(), p.value ("sfzExtra", std::string()), {}, {}, p.value ("optionsFrom", std::string()) };

                for (auto& a : p.value ("aliases", json::array()))
                    piece.aliases.push_back ({ a.at ("note").get<int>(), a.value ("name", piece.displayName), a.value ("sfz", std::string()) });

                for (auto& o : p.value ("options", json::array()))
                {
                    DrumPieceOption option { o.at ("key").get<std::string>(), o.value ("name", o.at ("key").get<std::string>()),
                                             o.value ("default", std::string()), {} };

                    for (auto& c : o.at ("choices"))
                        option.choices.push_back ({ c.at ("key").get<std::string>(), c.value ("name", c.at ("key").get<std::string>()),
                                                    c.value ("sfz", std::string()) });

                    if (option.defaultChoice.empty() && ! option.choices.empty())
                        option.defaultChoice = option.choices.front().key;

                    piece.options.push_back (std::move (option));
                }

                m.pieces.push_back (std::move (piece));
            }

            for (auto& [kit, map] : j.at ("kits").items())
                for (auto& [piece, sample] : map.items())
                    m.kits[kit][piece] = sample.get<std::string>();

            for (auto& k : j.value ("kitOrder", json::array()))
                if (m.kits.count (k.get<std::string>()) != 0)
                    m.kitOrder.push_back (k.get<std::string>());

            for (auto& [kit, _] : m.kits)
                if (std::find (m.kitOrder.begin(), m.kitOrder.end(), kit) == m.kitOrder.end())
                    m.kitOrder.push_back (kit);

            if (j.contains ("kitAliases"))
                for (auto it = j.at ("kitAliases").begin(); it != j.at ("kitAliases").end(); ++it)
                    m.kitAliases[it.key()] = it.value().get<std::string>();

            m.samples = j.at ("samples").get<std::vector<std::string>>();
        }
        else if (m.type == "melodic")
        {
            if (j.contains ("presets"))
                for (auto& p : j.at ("presets"))
                    m.presets.push_back ({ p.at ("key").get<std::string>(), p.value ("name", p.at ("key").get<std::string>()),
                                           p.at ("sfz").get<std::string>(), p.value ("volumeDb", 0.0) });

            if (m.mainSfz.empty() && m.presets.empty())
                throw std::runtime_error ("melodic instrument needs 'main' or 'presets'");
        }
        else
        {
            throw std::runtime_error ("unknown instrument type: " + m.type);
        }

        return m;
    }
    catch (const json::exception& e)
    {
        throw std::runtime_error (std::string ("invalid instrument manifest: ") + e.what());
    }
}

namespace
{
    double num (const json& j, const char* key, double fallback)
    {
        auto it = j.find (key);
        return it != j.end() && it->is_number() ? it->get<double>() : fallback;
    }

    std::string str (const json& j, const char* key, const std::string& fallback)
    {
        auto it = j.find (key);
        return it != j.end() && it->is_string() ? it->get<std::string>() : fallback;
    }

    std::string fmt (double v)
    {
        std::ostringstream s;
        s.imbue (std::locale::classic());
        s.precision (6);
        s << v;
        return s.str();
    }

    bool isSafeRelativePath (const std::string& s)
    {
        if (s.empty() || s.find ("..") != std::string::npos || s.front() == '/')
            return false;

        return std::all_of (s.begin(), s.end(), [] (char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                     || c == '_' || c == '-' || c == '/' || c == '.';
        });
    }
}

ResolvedInstrumentParams resolveInstrumentParams (const BuiltinInstrumentManifest& m, const json& paramsIn)
{
    const json params = paramsIn.is_object() ? paramsIn : json::object();
    const json& defaults = m.defaultParams;

    ResolvedInstrumentParams r;
    r.volumeDb = num (params, "volumeDb", num (defaults, "volumeDb", 0.0));
    r.pan = std::clamp (num (params, "pan", num (defaults, "pan", 0.0)), -1.0, 1.0);
    r.toneDb = std::clamp (num (params, "tone", num (defaults, "tone", 0.0)), -12.0, 12.0);

    if (m.type == "melodic" && ! m.presets.empty())
    {
        r.preset = str (params, "preset", str (defaults, "preset", m.presets.front().key));

        if (std::none_of (m.presets.begin(), m.presets.end(), [&] (auto& p) { return p.key == r.preset; }))
            r.preset = m.presets.front().key;
    }

    if (m.type == "drums")
    {
        r.kit = str (params, "kit", str (defaults, "kit", m.kitOrder.empty() ? std::string() : m.kitOrder.front()));

        // 前の版で使っていたキット名（名前を変えたもの）は、この版の名前に読み替える
        if (auto a = m.kitAliases.find (r.kit); m.kits.find (r.kit) == m.kits.end() && a != m.kitAliases.end())
            r.kit = a->second;

        if (m.kits.find (r.kit) == m.kits.end() && ! m.kitOrder.empty())
            r.kit = str (defaults, "kit", m.kitOrder.front());

        if (m.kits.find (r.kit) == m.kits.end() && ! m.kitOrder.empty())
            r.kit = m.kitOrder.front();

        const json pieces = params.contains ("pieces") && params["pieces"].is_object() ? params["pieces"] : json::object();

        for (auto& piece : m.pieces)
        {
            ResolvedInstrumentParams::Piece rp;

            if (auto kit = m.kits.find (r.kit); kit != m.kits.end())
                if (auto s = kit->second.find (piece.key); s != kit->second.end())
                    rp.sample = s->second;

            for (auto& option : piece.options)
                rp.options[option.key] = option.defaultChoice;

            if (auto it = pieces.find (piece.key); it != pieces.end() && it->is_object())
            {
                // 選び方（マニフェストにある choice だけ）
                for (auto& option : piece.options)
                {
                    const auto chosen = str (*it, option.key.c_str(), option.defaultChoice);

                    if (std::any_of (option.choices.begin(), option.choices.end(), [&] (auto& c) { return c.key == chosen; }))
                        rp.options[option.key] = chosen;
                }

                const auto sample = str (*it, "sample", rp.sample);

                // マニフェストに無いサンプルは無視して既定のまま（別バージョンの音源で作られた場合など）
                if (std::find (m.samples.begin(), m.samples.end(), sample) != m.samples.end())
                    rp.sample = sample;

                rp.volumeDb = num (*it, "volumeDb", 0.0);
                rp.pan = std::clamp (num (*it, "pan", 0.0), -1.0, 1.0);
                rp.tuneSemitones = std::clamp (num (*it, "tune", 0.0), -24.0, 24.0);
            }

            r.pieces[piece.key] = rp;
        }
    }

    return r;
}

std::string generateSfz (const BuiltinInstrumentManifest& m, const json& params)
{
    const auto r = resolveInstrumentParams (m, params);

    std::ostringstream s;
    s.imbue (std::locale::classic());
    s << "// generated by ShareDAW for " << m.id << " " << m.version << "\n";

    std::string toneOpcodes;

    if (std::abs (r.toneDb) > 0.01)
        toneOpcodes = " eq1_type=hshelf eq1_freq=3000 eq1_bw=1 eq1_gain=" + fmt (r.toneDb);

    if (m.type == "melodic")
    {
        auto sfz = m.mainSfz;
        double presetVolumeDb = 0.0;

        for (auto& p : m.presets)
            if (p.key == r.preset)
            {
                sfz = p.sfz;
                presetVolumeDb = p.volumeDb;
            }

        s << "<master>" << toneOpcodes;

        if (std::abs (presetVolumeDb) > 0.001)
            s << " volume=" << fmt (presetVolumeDb);

        s << "\n";

        if (isSafeRelativePath (sfz))
            s << "#include \"" << sfz << "\"\n";

        return s.str();
    }

    for (auto& piece : m.pieces)
    {
        auto it = r.pieces.find (piece.key);

        if (it == r.pieces.end() || ! isSafeRelativePath (it->second.sample))
            continue;

        auto& p = it->second;

        // 音の選び方（スネアの胴の深さ・シェル）。optionsFrom ならそのパーツの選び方
        std::string optionSfz;
        {
            const auto* source = piece.optionsFrom.empty() ? &piece : m.findPiece (piece.optionsFrom);
            const auto resolved = source != nullptr ? r.pieces.find (source->key) : r.pieces.end();

            if (source != nullptr && resolved != r.pieces.end())
                for (auto& option : source->options)
                    if (auto chosen = resolved->second.options.find (option.key); chosen != resolved->second.options.end())
                        for (auto& c : option.choices)
                            if (c.key == chosen->second && ! c.sfz.empty())
                                optionSfz += " " + c.sfz;
        }

        std::vector<std::pair<int, std::string>> notes { { piece.note, {} } };

        for (auto& a : piece.aliases)
            notes.push_back ({ a.note, a.sfzExtra });

        // 別名のノートも同じ設定・同じサンプルで鳴らす（チョークのグループも同じ）。別名ごとの音の違い（開き具合など）を足す
        for (auto& [note, aliasSfz] : notes)
        {
            s << "<master> key=" << note
              << " volume=" << fmt (p.volumeDb)
              << " pan=" << fmt (p.pan * 100.0)
              << " tune=" << fmt (std::round (p.tuneSemitones * 100.0))
              << toneOpcodes;

            if (! piece.sfzExtra.empty())
                s << " " << piece.sfzExtra;

            s << optionSfz;

            if (! aliasSfz.empty())
                s << " " << aliasSfz;

            s << "\n#include \"samples/" << p.sample << ".sfz\"\n";
        }
    }

    return s.str();
}

} // namespace collab
