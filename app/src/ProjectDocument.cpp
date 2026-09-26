#include "ProjectDocument.h"

#include "AppPaths.h"
#include "collab/ProjectJson.h"
#include "collab/Unicode.h"

namespace
{
    constexpr size_t maxUndoSteps = 300;

    // 主要な編集の後、少し待ってから自動保存する（連続した編集をまとめるため）
    constexpr int autosaveDebounceMs = 3000;
}

ProjectDocument::ProjectDocument()
{
    project = collab::Project::createEmpty ("無題");   // utf8-std
    tempoMap = collab::TempoMap (project);
    startTimer (1000);
}

ProjectDocument::~ProjectDocument()
{
    stopTimer();
}

void ProjectDocument::perform (const juce::String& description, const std::function<void (collab::Project&)>& fn,
                               const juce::String& mergeId)
{
    collab::Project before = project;
    fn (project);

    if (project == before)
        return;

    if (mergeId.isEmpty() || mergeId != lastMergeId || undoStack.empty())
    {
        undoStack.push_back ({ std::move (before), description });

        if (undoStack.size() > maxUndoSteps)
            undoStack.erase (undoStack.begin());
    }

    lastMergeId = mergeId;
    redoStack.clear();
    dirty = true;
    changed();
}

juce::String ProjectDocument::getUndoDescription() const
{
    return undoStack.empty() ? juce::String() : undoStack.back().description;
}

juce::String ProjectDocument::getRedoDescription() const
{
    return redoStack.empty() ? juce::String() : redoStack.back().description;
}

bool ProjectDocument::undo()
{
    if (undoStack.empty())
        return false;

    auto entry = std::move (undoStack.back());
    undoStack.pop_back();
    redoStack.push_back ({ project, entry.description });
    project = std::move (entry.before);
    lastMergeId = {};
    dirty = true;
    changed();
    return true;
}

bool ProjectDocument::redo()
{
    if (redoStack.empty())
        return false;

    auto entry = std::move (redoStack.back());
    redoStack.pop_back();
    undoStack.push_back ({ project, entry.description });
    project = std::move (entry.before);
    lastMergeId = {};
    dirty = true;
    changed();
    return true;
}

void ProjectDocument::changed()
{
    tempoMap = collab::TempoMap (project);
    ++revision;
    const auto now = juce::Time::currentTimeMillis();

    if (! autosaveDirty)
        firstUnsavedChangeTime = now;

    autosaveDirty = true;
    lastChangeTime = now;
    sendChangeMessage();
}

void ProjectDocument::setProject (collab::Project p, bool markDirty)
{
    project = std::move (p);
    undoStack.clear();
    redoStack.clear();
    lastMergeId = {};
    dirty = markDirty;
    changed();
    autosaveDirty = markDirty;
}

//==============================================================================
void ProjectDocument::newProject (const juce::String& name)
{
    projectDir = juce::File();
    setProject (collab::Project::createEmpty (toStd (name)), false);

    if (onLocationChanged)
        onLocationChanged();
}

void ProjectDocument::load (const juce::File& folder)
{
    auto file = folder.getChildFile ("project.json");

    if (! file.existsAsFile())
        throw std::runtime_error (toStd ("project.json が見つかりません: "_ju + folder.getFullPathName()));

    auto p = collab::parseProject (file.loadFileAsString().toStdString());

    projectDir = folder;
    createFolderStructure (projectDir);
    setProject (std::move (p), false);

    if (onLocationChanged)
        onLocationChanged();
}

void ProjectDocument::createFolderStructure (const juce::File& dir)
{
    dir.createDirectory();

    for (auto sub : { "audio", ".collab", "autosave", "plugins-state" })
        dir.getChildFile (sub).createDirectory();
}

juce::Result ProjectDocument::writeTextAtomically (const juce::File& target, const std::string& text)
{
    target.getParentDirectory().createDirectory();
    juce::TemporaryFile temp (target);

    if (! temp.getFile().replaceWithData (text.data(), text.size()))
        return juce::Result::fail ("書き込みに失敗しました: "_ju + temp.getFile().getFullPathName());

    if (! temp.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("ファイルを置き換えられませんでした: "_ju + target.getFullPathName());

    return juce::Result::ok();
}

juce::Result ProjectDocument::save()
{
    if (! hasLocation())
        return juce::Result::fail ("保存先が決まっていません"_ju);

    createFolderStructure (projectDir);
    auto r = writeTextAtomically (getProjectFile(), collab::serialiseProject (project));

    if (r.wasOk())
    {
        dirty = false;
        writeAutosave (true);   // 自動保存も最新にしておく
        sendChangeMessage();
    }

    return r;
}

juce::Result ProjectDocument::saveNew (const juce::File& parentDir)
{
    auto folderName = toJuce (collab::sanitiseFileName (project.name));
    auto dir = parentDir.getChildFile (folderName);

    if (dir.getChildFile ("project.json").exists())
        return juce::Result::fail ("同じ名前のプロジェクトが既にあります: "_ju + dir.getFullPathName());

    auto oldAutosave = getAutosaveFile();
    projectDir = dir;
    auto r = save();

    if (r.failed())
    {
        projectDir = juce::File();
        return r;
    }

    oldAutosave.deleteFile();

    if (onLocationChanged)
        onLocationChanged();

    return r;
}

//==============================================================================
juce::File ProjectDocument::getAutosaveFile() const
{
    if (hasLocation())
        return projectDir.getChildFile ("autosave/project.autosave.json");

    return AppPaths::getAppDataDir().getChildFile ("unsaved/project.autosave.json");
}

void ProjectDocument::writeAutosave (bool force)
{
    if (! force && ! autosaveDirty)
        return;

    auto r = writeTextAtomically (getAutosaveFile(), collab::serialiseProject (project));

    if (r.wasOk())
    {
        autosaveDirty = false;
    }
    else
    {
        DBG ("autosave failed: " << r.getErrorMessage());
    }
}

void ProjectDocument::recoverFromAutosave (const juce::File& autosaveFile, const juce::File& folder)
{
    auto p = collab::parseProject (autosaveFile.loadFileAsString().toStdString());
    projectDir = folder;

    if (hasLocation())
        createFolderStructure (projectDir);

    setProject (std::move (p), true);

    if (onLocationChanged)
        onLocationChanged();
}

void ProjectDocument::timerCallback()
{
    if (! autosaveDirty)
        return;

    // 編集が一段落したら（debounce）、または編集が続いていても1分ごとに保存する
    const auto now = juce::Time::currentTimeMillis();

    if (now - lastChangeTime >= autosaveDebounceMs || now - firstUnsavedChangeTime >= autosaveIntervalMs)
        writeAutosave();
}
