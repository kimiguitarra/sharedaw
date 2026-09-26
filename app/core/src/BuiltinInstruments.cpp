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

BuiltinInstrumentManifest BuiltinInstrumentManifest::fromJson (const json& j)
{
    try
    {
        BuiltinInstrumentManifest m;
        m.id = j.at ("id").get<std::string>();
        m.version = j.at ("version").get<std::string>();
        m.displayName = j.value ("displayName", m.id);
        m.type = j.at ("type").get<std::string>();
        m.mainSfz = j.value ("main", std::string());
        m.defaultParams = j.value ("defaultParams", json::object());

        if (j.contains ("credits"))
            m.credits = j.at ("credits").get<std::vector<std::string>>();

        if (m.type == "drums")
        {
            for (auto& p : j.at ("pieces"))
                m.pieces.push_back ({ p.at ("key").get<std::string>(), p.value ("name", p.at ("key").get<std::string>()),
                                      p.at ("note").get<int>(), p.value ("sfzExtra", std::string()) });

            for (auto& [kit, map] : j.at ("kits").items())
                for (auto& [piece, sample] : map.items())
                    m.kits[kit][piece] = sample.get<std::string>();

            m.samples = j.at ("samples").get<std::vector<std::string>>();
        }
        else if (m.type == "melodic")
        {
            if (m.mainSfz.empty())
                throw std::runtime_error ("melodic instrument needs 'main'");
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

    if (m.type == "drums")
    {
        r.kit = str (params, "kit", str (defaults, "kit", m.kits.empty() ? std::string() : m.kits.begin()->first));

        if (m.kits.find (r.kit) == m.kits.end() && ! m.kits.empty())
            r.kit = m.kits.begin()->first;

        const json pieces = params.contains ("pieces") && params["pieces"].is_object() ? params["pieces"] : json::object();

        for (auto& piece : m.pieces)
        {
            ResolvedInstrumentParams::Piece rp;

            if (auto kit = m.kits.find (r.kit); kit != m.kits.end())
                if (auto s = kit->second.find (piece.key); s != kit->second.end())
                    rp.sample = s->second;

            if (auto it = pieces.find (piece.key); it != pieces.end() && it->is_object())
            {
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
    s << "// generated by CollabDAW for " << m.id << " " << m.version << "\n";

    std::string toneOpcodes;

    if (std::abs (r.toneDb) > 0.01)
        toneOpcodes = " eq1_type=hshelf eq1_freq=3000 eq1_bw=1 eq1_gain=" + fmt (r.toneDb);

    if (m.type == "melodic")
    {
        s << "<master>" << toneOpcodes << "\n";
        s << "#include \"" << m.mainSfz << "\"\n";
        return s.str();
    }

    for (auto& piece : m.pieces)
    {
        auto it = r.pieces.find (piece.key);

        if (it == r.pieces.end() || ! isSafeRelativePath (it->second.sample))
            continue;

        auto& p = it->second;
        s << "<master> key=" << piece.note
          << " volume=" << fmt (p.volumeDb)
          << " pan=" << fmt (p.pan * 100.0)
          << " tune=" << fmt (std::round (p.tuneSemitones * 100.0))
          << toneOpcodes;

        if (! piece.sfzExtra.empty())
            s << " " << piece.sfzExtra;

        s << "\n#include \"samples/" << p.sample << ".sfz\"\n";
    }

    return s.str();
}

} // namespace collab
