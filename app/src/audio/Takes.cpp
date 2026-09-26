#include "Takes.h"

#include "AudioFiles.h"
#include "collab/Recording.h"
#include "collab/Uuid.h"

namespace Takes
{

juce::Result import (const std::vector<EngineBridge::RecordedTake>& takes, const juce::File& projectDir,
                     const collab::TempoMap& map, std::vector<Clip>& result)
{
    juce::StringArray errors;
    const auto name = "テイク "_ju + juce::Time::getCurrentTime().formatted ("%H:%M:%S");

    for (auto& take : takes)
    {
        AudioFiles::Imported imported;

        if (auto r = AudioFiles::importFile (take.file, projectDir.getChildFile ("audio"), imported); r.failed())
        {
            errors.add (r.getErrorMessage());
            continue;
        }

        take.file.deleteFile();

        auto clip = collab::makeRecordedClip (map, collab::generateUuid(), imported.hash, toStd (name),
                                              take.startSeconds, take.offsetSeconds, take.lengthSeconds, imported.lengthSamples,
                                              take.punchInSeconds);

        if (clip.lengthSamples > 0)
            result.push_back ({ take.trackId, clip });
    }

    return errors.isEmpty() ? juce::Result::ok() : juce::Result::fail (errors.joinIntoString ("\n"));
}

void addToProject (ProjectDocument& document, const std::vector<Clip>& clips)
{
    if (clips.empty())
        return;

    document.perform ("録音"_ju, [clips] (collab::Project& p)
    {
        for (auto& c : clips)
            if (auto* t = p.findTrack (c.trackId); t != nullptr && t->type == collab::TrackType::audio)
                t->audioClips.push_back (c.clip);
    });
}

}
