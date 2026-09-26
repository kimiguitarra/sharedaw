#include "SyncManager.h"

#include "CredentialStore.h"
#include "collab/ProjectJson.h"
#include "collab/Sha256.h"
#include "collab/Render.h"
#include "plugins/PluginHost.h"

namespace
{
    juce::File collabDir (const juce::File& projectDir)   { return projectDir.getChildFile (".collab"); }

    std::vector<std::string> referencedAudio (const collab::Project& p)
    {
        std::vector<std::string> hashes;

        auto add = [&] (const std::string& h)
        {
            if (! h.empty() && std::find (hashes.begin(), hashes.end(), h) == hashes.end())
                hashes.push_back (h);
        };

        for (auto& t : p.tracks)
        {
            if (t.render)
                add (t.render->audioHash);

            for (auto& c : t.audioClips)
                add (c.audioHash);
        }

        return hashes;
    }

    juce::Result writeAtomically (const juce::File& target, const void* data, size_t size)
    {
        target.getParentDirectory().createDirectory();
        juce::TemporaryFile temp (target);

        if (! temp.getFile().replaceWithData (data, size) || ! temp.overwriteTargetFileWithTemporary())
            return juce::Result::fail ("書き込めません: "_ju + target.getFullPathName());

        return juce::Result::ok();
    }
}

SyncManager::SyncManager (ProjectDocument& doc, juce::PropertiesFile& props)
    : document (doc), settings (props)
{
    document.editGuard = [this] (const collab::Project& before, const collab::Project& after) { return guardEdit (before, after); };
    reloadForDocument();
    startTimer (60 * 1000);
}

SyncManager::~SyncManager()
{
    stopTimer();
    *alive = false;
    document.editGuard = nullptr;

    // バックグラウンドの更新が終わるまで待つ
    while (refreshing)
        juce::Thread::sleep (10);
}

void SyncManager::refreshLocksInBackground()
{
    if (! linked || refreshing.exchange (true))
        return;

    juce::Thread::launch ([this, alive = alive]
    {
        if (*alive)
            fetchLocks();

        refreshing = false;
    });
}

//==============================================================================
juce::String SyncManager::getServerUrl() const
{
    return settings.getValue ("syncServerUrl");
}

bool SyncManager::hasCredentials() const
{
    return getServerUrl().isNotEmpty() && CredentialStore::loadToken (getServerUrl()).isNotEmpty();
}

void SyncManager::setCredentials (const juce::String& serverUrl, const juce::String& token)
{
    const auto url = serverUrl.trim().trimCharactersAtEnd ("/");
    settings.setValue ("syncServerUrl", url);
    settings.saveIfNeeded();

    if (token.isNotEmpty())
        CredentialStore::saveToken (url, token.trim());
}

SyncClient SyncManager::makeClient() const
{
    const auto url = linked && meta.serverUrl.isNotEmpty() ? meta.serverUrl : getServerUrl();
    return SyncClient (url, CredentialStore::loadToken (url));
}

ApiResponse SyncManager::fetchMe()
{
    auto r = makeClient().get ("/me");

    if (r.ok())
    {
        settings.setValue ("syncUserId", toJuce (r.body.value ("id", std::string())));
        settings.setValue ("syncUserName", toJuce (r.body.value ("displayName", std::string())));
    }

    return r;
}

//==============================================================================
void SyncManager::reloadForDocument()
{
    linked = false;
    meta = {};
    base.reset();

    {
        const juce::ScopedLock sl (lockMapLock);
        locks.clear();
    }

    if (document.hasLocation())
    {
        const auto dir = collabDir (document.getProjectDir());
        auto metaFile = dir.getChildFile ("meta.json");

        if (metaFile.existsAsFile())
        {
            try
            {
                auto j = nlohmann::json::parse (metaFile.loadFileAsString().toStdString());
                meta.serverUrl = toJuce (j.value ("serverUrl", std::string()));
                meta.projectId = j.value ("projectId", std::string());
                meta.baseRevision = j.value ("baseRevision", 0);
                meta.userId = j.value ("userId", std::string());
                meta.userName = toJuce (j.value ("userName", std::string()));
                linked = meta.projectId == document.getProject().projectId && meta.serverUrl.isNotEmpty();
            }
            catch (const std::exception&)
            {
                linked = false;
            }
        }

        if (linked)
            if (auto baseFile = dir.getChildFile ("base.json"); baseFile.existsAsFile())
            {
                try
                {
                    base = collab::parseProject (baseFile.loadFileAsString().toStdString());
                }
                catch (const std::exception&)
                {
                    base.reset();
                }
            }
    }

    sendChangeMessage();
    refreshLocksInBackground();
}

void SyncManager::saveMeta (const juce::File& projectDir) const
{
    nlohmann::ordered_json j;
    j["serverUrl"] = toStd (meta.serverUrl);
    j["projectId"] = meta.projectId;
    j["baseRevision"] = meta.baseRevision;
    j["userId"] = meta.userId;
    j["userName"] = toStd (meta.userName);
    const auto text = j.dump (2) + "\n";
    writeAtomically (collabDir (projectDir).getChildFile ("meta.json"), text.data(), text.size());
}

void SyncManager::saveBase (const juce::File& projectDir, const collab::Project& p)
{
    const auto text = collab::serialiseProject (p);
    writeAtomically (collabDir (projectDir).getChildFile ("base.json"), text.data(), text.size());
}

bool SyncManager::hasLocalChanges (const std::string& scopeId) const
{
    if (! linked || ! base)
        return false;

    return ! collab::scopeEquals (*base, document.getProject(), scopeId);
}

juce::String SyncManager::scopeName (const std::string& scopeId) const
{
    const auto& p = document.getProject();

    if (scopeId == p.tempoTrack.id)  return "テンポ"_ju;
    if (scopeId == p.meterTrack.id)  return "拍子"_ju;
    if (scopeId == p.chordTrack.id)  return "コード"_ju;

    if (auto* t = p.findTrack (scopeId))
        return toJuce (t->name);

    if (base)
        if (auto* t = base->findTrack (scopeId))
            return toJuce (t->name);

    return toJuce (scopeId);
}

//==============================================================================
bool SyncManager::isLockedByMe (const std::string& scopeId) const
{
    const juce::ScopedLock sl (lockMapLock);
    auto it = locks.find (scopeId);
    return it != locks.end() && it->second.userId == meta.userId;
}

std::optional<SyncManager::LockInfo> SyncManager::getLock (const std::string& scopeId) const
{
    const juce::ScopedLock sl (lockMapLock);
    auto it = locks.find (scopeId);
    return it != locks.end() ? std::optional (it->second) : std::nullopt;
}

bool SyncManager::canEdit (const std::string& scopeId) const
{
    if (! linked || ! base)
        return true;

    // 新規作成したスコープ（ベースにない）は、作成者がロックを持っている扱い（§4.2）
    const bool inBase = scopeId == base->tempoTrack.id || scopeId == base->meterTrack.id || scopeId == base->chordTrack.id
                          || base->findTrack (scopeId) != nullptr;

    return ! inBase || isLockedByMe (scopeId);
}

bool SyncManager::guardEdit (const collab::Project& before, const collab::Project& after)
{
    if (! linked || ! base)
        return true;

    std::vector<std::string> ids = collab::allScopeIds (after);

    for (auto& t : before.tracks)
        if (after.findTrack (t.id) == nullptr)
            ids.push_back (t.id);

    std::vector<std::string> denied;

    for (auto& id : ids)
        if (! collab::scopeEquals (before, after, id) && ! canEdit (id))
            denied.push_back (id);

    if (denied.empty())
        return true;

    if (onLockRequired)
        juce::MessageManager::callAsync ([cb = onLockRequired, denied] { cb (denied); });

    return false;
}

juce::Result SyncManager::fetchLocks()
{
    if (! linked)
        return juce::Result::ok();

    auto r = makeClient().get ("/projects/" + toJuce (meta.projectId) + "/locks");

    if (! r.ok())
        return juce::Result::fail (r.message());

    std::map<std::string, LockInfo> fresh;

    for (auto& l : r.body)
        fresh[l.value ("trackId", std::string())] = { l.value ("userId", std::string()),
                                                      toJuce (l.contains ("displayName") && l["displayName"].is_string()
                                                                ? l["displayName"].get<std::string>() : std::string()) };

    {
        const juce::ScopedLock sl (lockMapLock);
        locks = std::move (fresh);
    }

    juce::MessageManager::callAsync ([this, alive = alive] { if (*alive) sendChangeMessage(); });
    return juce::Result::ok();
}

juce::Result SyncManager::runAcquireLock (const std::string& scopeId)
{
    auto r = makeClient().post ("/projects/" + toJuce (meta.projectId) + "/locks", { { "trackId", scopeId } });

    if (! r.ok())
    {
        fetchLocks();
        return juce::Result::fail (r.message());
    }

    return fetchLocks();
}

juce::Result SyncManager::runReleaseLock (const std::string& scopeId, bool force)
{
    auto r = makeClient().del ("/projects/" + toJuce (meta.projectId) + "/locks/" + toJuce (scopeId) + (force ? "?force=true" : ""));

    if (! r.ok())
        return juce::Result::fail (r.message());

    return fetchLocks();
}

//==============================================================================
juce::Result SyncManager::uploadMissingBlobs (const SyncClient& client, const std::vector<std::string>& hashes,
                                              const juce::File& projectDir, const std::map<std::string, std::string>& inlineContent)
{
    if (hashes.empty())
        return juce::Result::ok();

    nlohmann::json list = nlohmann::json::array();
    for (auto& h : hashes)
        list.push_back (h);

    auto check = client.post ("/blobs/check", { { "hashes", list } });

    if (! check.ok())
        return juce::Result::fail (check.message());

    for (auto& m : check.body.value ("missing", nlohmann::json::array()))
    {
        auto t = TransferUrl::fromJson (m);
        juce::MemoryBlock data;

        if (auto it = inlineContent.find (t.hash); it != inlineContent.end())
        {
            data.append (it->second.data(), it->second.size());
        }
        else
        {
            auto file = projectDir.getChildFile ("audio").getChildFile (toJuce (t.hash) + ".wav");

            if (! file.loadFileAsData (data))
                return juce::Result::fail ("オーディオが見つかりません: "_ju + file.getFullPathName());
        }

        if (auto r = client.uploadBlob (t, data); r.failed())
            return r;
    }

    return juce::Result::ok();
}

juce::Result SyncManager::runRegister (const collab::Project& snapshot, const juce::File& projectDir)
{
    auto client = makeClient();
    auto me = client.get ("/me");

    if (! me.ok())
        return juce::Result::fail (me.message());

    meta.serverUrl = client.getServerUrl();
    meta.userId = me.body.value ("id", std::string());
    meta.userName = toJuce (me.body.value ("displayName", std::string()));

    // 友人も参加させる（少人数なので、登録済みの全ユーザーをメンバーにする）
    nlohmann::json members = nlohmann::json::array();
    if (auto users = client.get ("/users"); users.ok())
        for (auto& u : users.body)
            members.push_back (u.value ("id", std::string()));

    auto created = client.post ("/projects", { { "id", snapshot.projectId }, { "name", snapshot.name }, { "memberIds", members } });

    if (! created.ok() && created.errorCode() != "project_exists")
        return juce::Result::fail (created.message());

    const auto text = collab::serialiseProject (snapshot);
    const auto hash = collab::Sha256::hashHex (text);
    auto hashes = referencedAudio (snapshot);
    hashes.push_back (hash);

    if (auto r = uploadMissingBlobs (client, hashes, projectDir, { { hash, text } }); r.failed())
        return r;

    auto pushed = client.post ("/projects/" + toJuce (snapshot.projectId) + "/revisions",
                               { { "parentNumber", 0 }, { "message", "最初のリビジョン" }, { "projectJsonHash", hash } });   // utf8-std

    if (! pushed.ok())
        return juce::Result::fail (pushed.message());

    meta.projectId = snapshot.projectId;
    meta.baseRevision = pushed.body.value ("number", 1);
    return juce::Result::ok();
}

void SyncManager::applyRegistered (const collab::Project& snapshot, int revision)
{
    meta.baseRevision = revision;
    saveMeta (document.getProjectDir());
    saveBase (document.getProjectDir(), snapshot);
    reloadForDocument();
}

//==============================================================================
juce::Result SyncManager::fetchPullPreview (PullPreview& preview)
{
    auto client = makeClient();
    auto info = client.get ("/projects/" + toJuce (meta.projectId));

    if (! info.ok())
        return juce::Result::fail (info.message());

    preview.head = info.body.value ("headRevision", 0);

    if (preview.head == meta.baseRevision || preview.head == 0)
    {
        preview.headProject = base ? *base : document.getProject();
        return juce::Result::ok();
    }

    auto rev = client.get ("/projects/" + toJuce (meta.projectId) + "/revisions/" + juce::String (preview.head));

    if (! rev.ok())
        return juce::Result::fail (rev.message());

    juce::MemoryBlock data;

    if (auto r = client.download (TransferUrl::fromJson (rev.body["download"]), data); r.failed())
        return r;

    const auto text = data.toString().toStdString();

    if (collab::Sha256::hashHex (text) != rev.body.value ("projectJsonHash", std::string()))
        return juce::Result::fail ("ダウンロードしたプロジェクトのハッシュが一致しません"_ju);

    try
    {
        preview.headProject = collab::parseProject (text);
    }
    catch (const std::exception& e)
    {
        return juce::Result::fail (juce::String::fromUTF8 (e.what()));
    }

    preview.diff = collab::diffProjects (base ? *base : preview.headProject, preview.headProject);
    return fetchLocks();
}

juce::Result SyncManager::runDownloadAudio (const collab::Project& p, const juce::File& projectDir)
{
    auto client = makeClient();
    auto audioDir = projectDir.getChildFile ("audio");
    audioDir.createDirectory();

    for (auto& hash : referencedAudio (p))
    {
        auto target = audioDir.getChildFile (toJuce (hash) + ".wav");

        if (target.existsAsFile())
            continue;

        auto info = client.get ("/blobs/" + toJuce (hash));

        if (! info.ok())
            return juce::Result::fail (info.message());

        juce::MemoryBlock data;

        if (auto r = client.download (TransferUrl::fromJson (info.body), data); r.failed())
            return r;

        collab::Sha256 sha;
        sha.update (data.getData(), data.getSize());

        if (sha.finishHex() != hash)
            return juce::Result::fail ("ダウンロードしたオーディオのハッシュが一致しません"_ju);

        // 一時ファイルに書いてから置き換える（§9: 途中で失敗してもローカルを壊さない）
        if (auto r = writeAtomically (target, data.getData(), data.getSize()); r.failed())
            return r;
    }

    return juce::Result::ok();
}

SyncManager::PullReport SyncManager::applyPull (const PullPreview& preview)
{
    PullReport report;
    const auto& local = document.getProject();
    const auto baseProject = base ? *base : preview.headProject;

    std::set<std::string> mine;
    for (auto& id : collab::allScopeIds (local))
        if (isLockedByMe (id))
            mine.insert (id);

    auto result = collab::mergeForPull (baseProject, local, preview.headProject, mine);
    report.keptLocal = result.keptLocalScopes;
    report.conflicts = result.conflictScopes;

    // 不整合があれば、取り込む前のローカルを丸ごと退避しておく（データを失わない）
    if (! result.conflictScopes.empty())
    {
        report.conflictBackup = collabDir (document.getProjectDir()).getChildFile ("conflicts")
                                  .getChildFile (juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + ".project.json");
        const auto text = collab::serialiseProject (local);
        writeAtomically (report.conflictBackup, text.data(), text.size());
    }

    document.replaceFromSync (result.merged);

    meta.baseRevision = preview.head;
    saveMeta (document.getProjectDir());
    saveBase (document.getProjectDir(), preview.headProject);
    base = preview.headProject;
    document.save();
    sendChangeMessage();
    return report;
}

//==============================================================================
static bool hasMissingPluginState (const collab::Track& t, const juce::File& dir)
{
    auto missing = [&] (const std::string& ref) { return ! ref.empty() && ! PluginHost::stateFile (dir, ref).existsAsFile(); };

    if (t.instrument && t.instrument->kind == collab::Instrument::Kind::external && missing (t.instrument->stateRef))
        return true;

    for (auto& e : t.effects)
        if (missing (e.stateRef))
            return true;

    return false;
}

juce::Result SyncManager::fetchPushPlan (const collab::Project& snapshot, PushPlan& plan)
{
    auto client = makeClient();
    auto info = client.get ("/projects/" + toJuce (meta.projectId));

    if (! info.ok())
        return juce::Result::fail (info.message());

    if (auto r = fetchLocks(); r.failed())
        return r;

    plan.head = info.body.value ("headRevision", 0);
    plan.needsPull = plan.head != meta.baseRevision;
    plan.snapshot = snapshot;
    plan.diff = collab::diffProjects (base ? *base : snapshot, snapshot);

    for (auto& id : plan.diff.changedScopeIds)
        if (! canEdit (id))
            plan.notLocked.push_back (id);

    // 外部プラグインのトラックはバウンスが必須。バウンス後に内容が変わっていたら push できない（§3.7）
    const auto dir = document.getProjectDir();

    for (auto& t : snapshot.tracks)
    {
        if (! plan.diff.touches (t.id))
            continue;

        auto stateHash = [dir] (const std::string& ref) { return PluginHost::stateHash (dir, ref); };
        const auto fp = collab::trackSourceFingerprint (t, stateHash);
        auto status = collab::renderStatus (t, fp);

        // 他の人のプラグイン（状態ファイルがこの環境にない）は正しいフィンガープリントを計算できない。
        // 音の元がベースから変わっていなければ、ベースのバウンスがそのまま使える（音量などの変更は push できる）
        if (status == collab::RenderStatus::stale && base && hasMissingPluginState (t, dir))
            if (auto* before = base->findTrack (t.id); before != nullptr && before->render == t.render
                                                        && collab::trackSourceFingerprint (*before, stateHash) == fp)
                status = collab::RenderStatus::upToDate;

        if (status == collab::RenderStatus::missing || status == collab::RenderStatus::stale)
            plan.staleRenders.push_back (t.id);
    }

    return juce::Result::ok();
}

juce::Result SyncManager::runPush (const PushPlan& plan, const juce::String& message, bool releaseLocks,
                                   const juce::File& projectDir, int& newRevision)
{
    auto client = makeClient();
    const auto text = collab::serialiseProject (plan.snapshot);
    const auto hash = collab::Sha256::hashHex (text);

    auto hashes = referencedAudio (plan.snapshot);
    hashes.push_back (hash);

    if (auto r = uploadMissingBlobs (client, hashes, projectDir, { { hash, text } }); r.failed())
        return r;

    nlohmann::json changed = nlohmann::json::array();
    for (auto& id : plan.diff.changedScopeIds)
        changed.push_back (id);

    auto r = client.post ("/projects/" + toJuce (meta.projectId) + "/revisions",
                          { { "parentNumber", meta.baseRevision }, { "message", toStd (message) }, { "projectJsonHash", hash },
                            { "changedTrackIds", changed }, { "releaseLocks", releaseLocks } });

    if (! r.ok())
        return juce::Result::fail (r.message());

    newRevision = r.body.value ("number", meta.baseRevision + 1);
    return fetchLocks();
}

void SyncManager::applyPushed (const PushPlan& plan, int newRevision)
{
    meta.baseRevision = newRevision;
    saveMeta (document.getProjectDir());
    saveBase (document.getProjectDir(), plan.snapshot);
    base = plan.snapshot;
    sendChangeMessage();
}

//==============================================================================
juce::Result SyncManager::fetchProjects (nlohmann::json& list)
{
    auto r = makeClient().get ("/projects");

    if (! r.ok())
        return juce::Result::fail (r.message());

    list = r.body;
    return juce::Result::ok();
}

juce::Result SyncManager::fetchRevisions (nlohmann::json& list)
{
    auto r = makeClient().get ("/projects/" + toJuce (meta.projectId) + "/revisions");

    if (! r.ok())
        return juce::Result::fail (r.message());

    list = r.body;
    return juce::Result::ok();
}

juce::Result SyncManager::runOpenFromServer (const std::string& projectId, const juce::File& parentDir, juce::File& createdFolder)
{
    auto client = makeClient();
    auto me = client.get ("/me");

    if (! me.ok())
        return juce::Result::fail (me.message());

    auto info = client.get ("/projects/" + toJuce (projectId));

    if (! info.ok())
        return juce::Result::fail (info.message());

    const int head = info.body.value ("headRevision", 0);

    if (head == 0)
        return juce::Result::fail ("このプロジェクトにはまだリビジョンがありません"_ju);

    auto rev = client.get ("/projects/" + toJuce (projectId) + "/revisions/" + juce::String (head));

    if (! rev.ok())
        return juce::Result::fail (rev.message());

    juce::MemoryBlock data;

    if (auto r = client.download (TransferUrl::fromJson (rev.body["download"]), data); r.failed())
        return r;

    collab::Project project;

    try
    {
        project = collab::parseProject (data.toString().toStdString());
    }
    catch (const std::exception& e)
    {
        return juce::Result::fail (juce::String::fromUTF8 (e.what()));
    }

    auto folder = parentDir.getChildFile (juce::File::createLegalFileName (toJuce (project.name)));

    if (folder.getChildFile ("project.json").exists())
        return juce::Result::fail ("同じ名前のフォルダに既にプロジェクトがあります: "_ju + folder.getFullPathName());

    ProjectDocument::createFolderStructure (folder);

    if (auto r = runDownloadAudio (project, folder); r.failed())
        return r;

    const auto text = collab::serialiseProject (project);

    if (auto r = writeAtomically (folder.getChildFile ("project.json"), text.data(), text.size()); r.failed())
        return r;

    saveBase (folder, project);

    Meta m;
    m.serverUrl = client.getServerUrl();
    m.projectId = projectId;
    m.baseRevision = head;
    m.userId = me.body.value ("id", std::string());
    m.userName = toJuce (me.body.value ("displayName", std::string()));
    std::swap (meta, m);
    saveMeta (folder);
    std::swap (meta, m);

    createdFolder = folder;
    return juce::Result::ok();
}
