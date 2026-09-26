#pragma once

#include <functional>

#include "Common.h"
#include "collab/Project.h"
#include "collab/TempoMap.h"

/**
    編集中のプロジェクト（正は collab::Project）と、そのフォルダ・元に戻す履歴・保存・自動保存を管理する。
    すべての編集は perform() を通して行い、変更は ChangeBroadcaster で UI とエンジンに通知する。

    フォルダ構成は仕様書 §5 のとおり:
        <プロジェクト名>/project.json, audio/, .collab/, autosave/, plugins-state/
*/
class ProjectDocument  : public juce::ChangeBroadcaster,
                         private juce::Timer
{
public:
    ProjectDocument();
    ~ProjectDocument() override;

    const collab::Project& getProject() const noexcept      { return project; }
    const collab::TempoMap& getTempoMap() const noexcept    { return tempoMap; }

    /**
        編集を行う。fn の中で Project を書き換える。
        mergeId が空でなく、直前の編集と同じ mergeId なら元に戻す履歴を1つにまとめる（ドラッグ中など）。
    */
    void perform (const juce::String& description, const std::function<void (collab::Project&)>& fn,
                  const juce::String& mergeId = {});

    /** ドラッグ等のまとまりを終わらせる（次の perform は新しい履歴になる）。 */
    void endMerge()                                          { lastMergeId = {}; }

    bool canUndo() const                                     { return ! undoStack.empty(); }
    bool canRedo() const                                     { return ! redoStack.empty(); }
    juce::String getUndoDescription() const;
    juce::String getRedoDescription() const;
    bool undo();
    bool redo();

    /** 変更の世代番号（エンジン側の差分同期に使う）。 */
    std::uint64_t getRevision() const noexcept               { return revision; }

    //==============================================================================
    bool hasLocation() const                                 { return projectDir != juce::File(); }
    juce::File getProjectDir() const                         { return projectDir; }
    juce::File getProjectFile() const                        { return projectDir.getChildFile ("project.json"); }
    bool isDirty() const noexcept                            { return dirty; }

    /** 新しい空のプロジェクト（場所未定）にする。 */
    void newProject (const juce::String& name);

    /** 指定したフォルダ（project.json を含む）を開く。失敗時は例外（std::exception）。 */
    void load (const juce::File& projectFolder);

    /** 現在の場所に保存する。場所が未定なら false。 */
    juce::Result save();

    /** parentDir の下に「プロジェクト名」のフォルダを作って保存する。 */
    juce::Result saveNew (const juce::File& parentDir);

    /** 自動保存ファイルの場所（プロジェクトフォルダ内、場所未定ならアプリのデータフォルダ）。 */
    juce::File getAutosaveFile() const;

    /** 自動保存を書く（変更がなければ何もしない）。 */
    void writeAutosave (bool force = false);

    /** 自動保存から復旧する（projectFolder が空ならフォルダ未定のプロジェクトとして開く）。 */
    void recoverFromAutosave (const juce::File& autosaveFile, const juce::File& projectFolder);

    /** プロジェクトフォルダの構成（§5）を作る。 */
    static void createFolderStructure (const juce::File& dir);

    /** 自動保存の間隔（既定60秒）。 */
    static constexpr int autosaveIntervalMs = 60 * 1000;

    /** 場所・ファイルが変わったときに呼ばれる（セッション情報・同期情報の更新用）。 */
    std::vector<std::function<void()>> locationListeners;

    /**
        編集を許可するか（同期中のロック確認、§4.2）。false を返すと perform() の変更は取り消される。
    */
    std::function<bool (const collab::Project& before, const collab::Project& after)> editGuard;

    /** 保存・自動保存の直前に呼ばれる（外部プラグインの状態を書き出すため）。 */
    std::function<void()> beforeSave;

    /** pull の結果でプロジェクトを置き換える（元に戻す履歴は消える）。 */
    void replaceFromSync (collab::Project);

private:
    struct UndoEntry
    {
        collab::Project before;
        juce::String description;
    };

    collab::Project project;
    collab::TempoMap tempoMap;
    juce::File projectDir;
    bool dirty = false;
    bool autosaveDirty = false;
    std::uint64_t revision = 0;
    juce::int64 lastChangeTime = 0, firstUnsavedChangeTime = 0;

    std::vector<UndoEntry> undoStack, redoStack;
    juce::String lastMergeId;

    void setProject (collab::Project, bool markDirty);
    void changed();
    void timerCallback() override;

    static juce::Result writeTextAtomically (const juce::File&, const std::string&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProjectDocument)
};
