#include "InstrumentLibrary.h"

namespace
{
    // "1.10.0" > "1.9.0" となるように数値で比較する
    std::vector<int> versionParts (const std::string& v)
    {
        std::vector<int> parts;

        for (auto& p : juce::StringArray::fromTokens (toJuce (v), ".", {}))
            parts.push_back (p.getIntValue());

        return parts;
    }
}

InstrumentLibrary::InstrumentLibrary (juce::File dir)
    : assetsDir (std::move (dir))
{
    auto root = assetsDir.getChildFile ("instruments");

    for (auto& manifestFile : root.findChildFiles (juce::File::findFiles, true, "manifest.json"))
    {
        try
        {
            auto json = nlohmann::json::parse (manifestFile.loadFileAsString().toStdString());
            auto m = collab::BuiltinInstrumentManifest::fromJson (json);
            manifests[{ m.id, m.version }] = std::move (m);
        }
        catch (const std::exception& e)
        {
            loadErrors.add (manifestFile.getFullPathName() + ": " + e.what());
        }
    }
}

const collab::BuiltinInstrumentManifest* InstrumentLibrary::find (const std::string& id, const std::string& version) const
{
    auto it = manifests.find ({ id, version });
    return it != manifests.end() ? &it->second : nullptr;
}

const collab::BuiltinInstrumentManifest* InstrumentLibrary::findLatest (const std::string& id) const
{
    const collab::BuiltinInstrumentManifest* best = nullptr;

    for (auto& [key, m] : manifests)
        if (key.first == id && (best == nullptr || versionParts (m.version) > versionParts (best->version)))
            best = &m;

    return best;
}

juce::File InstrumentLibrary::getInstrumentDir (const collab::BuiltinInstrumentManifest& m) const
{
    return assetsDir.getChildFile ("instruments").getChildFile (toJuce (m.id)).getChildFile (toJuce (m.version));
}

juce::File InstrumentLibrary::getVirtualSfzPath (const collab::BuiltinInstrumentManifest& m) const
{
    return getInstrumentDir (m).getChildFile ("_sharedaw_generated.sfz");
}

std::vector<const collab::BuiltinInstrumentManifest*> InstrumentLibrary::getAll() const
{
    std::vector<const collab::BuiltinInstrumentManifest*> all;

    for (auto& [key, m] : manifests)
        all.push_back (&m);

    return all;
}
