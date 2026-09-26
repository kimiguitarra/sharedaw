#pragma once

#include <map>
#include <optional>

#include "Common.h"
#include "collab/BuiltinInstruments.h"

/** 同梱の内蔵音源マニフェストを読み込んで保持する。 */
class InstrumentLibrary
{
public:
    explicit InstrumentLibrary (juce::File assetsDir);

    const collab::BuiltinInstrumentManifest* find (const std::string& id, const std::string& version) const;

    /** 同じ ID の中で最新のバージョン（新規トラック作成時に使う）。 */
    const collab::BuiltinInstrumentManifest* findLatest (const std::string& id) const;

    juce::File getInstrumentDir (const collab::BuiltinInstrumentManifest&) const;

    /** sfizz の loadSfzString() に渡す仮想パス（#include の基準）。 */
    juce::File getVirtualSfzPath (const collab::BuiltinInstrumentManifest&) const;

    juce::File getMetronomeSfz() const              { return assetsDir.getChildFile ("metronome/click.sfz"); }

    std::vector<const collab::BuiltinInstrumentManifest*> getAll() const;

    const juce::StringArray& getLoadErrors() const  { return loadErrors; }

private:
    juce::File assetsDir;
    std::map<std::pair<std::string, std::string>, collab::BuiltinInstrumentManifest> manifests;
    juce::StringArray loadErrors;
};
