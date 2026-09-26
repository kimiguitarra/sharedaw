#include "SessionGuard.h"

#include "AppPaths.h"

SessionGuard::SessionGuard()
    : sessionFile (AppPaths::getAppDataDir().getChildFile ("session.json"))
{
}

std::optional<SessionGuard::PreviousSession> SessionGuard::findCrashedSession() const
{
    if (! sessionFile.existsAsFile())
        return std::nullopt;

    auto v = juce::JSON::parse (sessionFile);

    PreviousSession s;
    s.projectDir = v["projectDir"].toString().isNotEmpty() ? juce::File (v["projectDir"].toString()) : juce::File();
    s.autosaveFile = v["autosave"].toString().isNotEmpty() ? juce::File (v["autosave"].toString()) : juce::File();

    if (! s.autosaveFile.existsAsFile())
        return std::nullopt;

    return s;
}

void SessionGuard::markRunning (const juce::File& projectDir, const juce::File& autosaveFile)
{
    auto obj = std::make_unique<juce::DynamicObject>();
    obj->setProperty ("projectDir", projectDir == juce::File() ? juce::String() : projectDir.getFullPathName());
    obj->setProperty ("autosave", autosaveFile.getFullPathName());
    obj->setProperty ("startedAt", juce::Time::getCurrentTime().toISO8601 (true));
    sessionFile.replaceWithText (juce::JSON::toString (juce::var (obj.release())));
}

void SessionGuard::markCleanExit()
{
    sessionFile.deleteFile();
}
