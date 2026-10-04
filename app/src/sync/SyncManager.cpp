#include "SyncManager.h"

#include "AppPaths.h"
#include "audio/AudioFiles.h"

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

    juce::String megabytes (juce::int64 bytes)
    {
        return juce::String ((double) bytes / (1024.0 * 1024.0), 1) + " MB";
    }

}

SyncManager::SyncManager (ProjectDocument& doc, juce::PropertiesFile& props)
    : document (doc), settings (props)
{
    reloadForDocument();
    startTimer (20 * 1000);   // 他の人のアップロードに早く気付けるように（1 回あたり小さな GET が 3 つ）
}

SyncManager::~SyncManager()
{
    stopTimer();
    *alive = false;

    // バックグラウンドの更新が終わるまで待つ
    while (refreshing)
        juce::Thread::sleep (10);
}

void SyncManager::checkServerNow()
{
    refreshInBackground();
}

SyncManager::ServerStatus SyncManager::getServerStatus() const
{
    const juce::ScopedLock sl (statusLock);
    return serverStatus;
}

void SyncManager::refreshInBackground()
{
    if (! linked || refreshing.exchange (true))
        return;

    {
        const juce::ScopedLock sl (statusLock);
        serverStatus.checking = true;
    }

    sendChangeMessage();

    // ベースはメッセージスレッドで書き換わるので、ここで写しを取る
    const auto projectId = meta.projectId;
    const int baseRevision = meta.baseRevision;
    const auto baseCopy = base;
    const auto cachedPreview = getServerStatus().preview;

    juce::Thread::launch ([this, alive = alive, projectId, baseRevision, baseCopy, cachedPreview]
    {
        ServerStatus st;
        st.checked = true;
        st.checkedAt = juce::Time::getCurrentTime();
        st.base = baseRevision;

        if (*alive)
        {
            auto client = makeClient();
            auto info = client.get ("/projects/" + toJuce (projectId));

            if (! info.ok())
            {
                st.error = info.message();
            }
            else
            {
                st.online = true;
                st.head = info.body.value ("headRevision", 0);

                if (auto revs = client.get ("/projects/" + toJuce (projectId) + "/revisions"); revs.ok() && revs.body.is_array())
                {
                    for (auto& r : revs.body)
                    {
                        RevisionInfo ri;
                        ri.number = r.value ("number", 0);
                        ri.authorId = r.value ("authorId", std::string());
                        ri.author = r.contains ("authorName") && r["authorName"].is_string() ? toJuce (r["authorName"].get<std::string>()) : juce::String ("?");
                        ri.message = toJuce (r.value ("message", std::string()));
                        ri.createdAt = juce::Time::fromISO8601 (toJuce (r.value ("createdAt", std::string())));

                        if (ri.number > baseRevision)
                            st.incoming.push_back (ri);

                        if (st.history.size() < 30)
                            st.history.push_back (ri);
                    }
                }

                // 取り込む変更の中身（ヘッドが変わったときだけダウンロードする）
                if (st.head > baseRevision)
                {
                    if (cachedPreview != nullptr && cachedPreview->head == st.head)
                    {
                        st.preview = cachedPreview;
                    }
                    else
                    {
                        auto preview = std::make_shared<PullPreview>();

                        if (buildPreview (client, projectId, st.head, baseCopy, *preview).wasOk())
                            st.preview = preview;
                    }
                }
            }
        }

        // 終わった印はこのスレッドで付ける（デストラクタがメッセージスレッドで待っているので、メッセージスレッドに任せるとデッドロックする）
        const bool stillAlive = *alive;
        refreshing = false;

        if (stillAlive)
        {
            juce::MessageManager::callAsync ([this, alive, st, baseRevision]
            {
                if (! *alive)
                    return;

                // 確認している間に取り込み・アップロードでベースが変わったら、この結果は古い（すぐ確認し直す）
                if (baseRevision != meta.baseRevision)
                    return refreshInBackground();

                {
                    const juce::ScopedLock sl (statusLock);
                    serverStatus = st;
                }

                // 他の人の新しいリビジョンを知らせる
                std::vector<RevisionInfo> fresh;

                for (auto& r : st.incoming)
                    if (r.number > notifiedHead && r.authorId != meta.userId)
                        fresh.push_back (r);

                notifiedHead = juce::jmax (notifiedHead, st.head);
                sendChangeMessage();

                if (! fresh.empty() && onIncomingRevisions)
                    onIncomingRevisions (fresh);
            });
        }
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
SyncManager::LocalInfo SyncManager::inspectFolder (const juce::File& folder)
{
    LocalInfo info;
    info.folder = folder;
    const auto projectFile = folder.getChildFile ("project.json");

    if (! projectFile.existsAsFile())
        return info;

    try
    {
        const auto project = collab::parseProject (projectFile.loadFileAsString().toStdString());
        info.valid = true;
        info.projectId = project.projectId;
        info.name = toJuce (project.name);
        info.savedAt = projectFile.getLastModificationTime();

        const auto dir = collabDir (folder);
        const auto m = readMeta (folder);

        if (! m)
            return info;

        info.serverUrl = m->serverUrl;
        info.baseRevision = m->baseRevision;
        info.linked = m->projectId == project.projectId && info.serverUrl.isNotEmpty();

        if (auto baseFile = dir.getChildFile ("base.json"); info.linked && baseFile.existsAsFile())
        {
            const auto baseProject = collab::parseProject (baseFile.loadFileAsString().toStdString());
            auto diff = collab::diffProjects (baseProject, project);

            // この PC だけのトラック（外部プラグイン）はアップしないので数えない
            std::erase_if (diff.changedScopeIds, [&] (const std::string& id) { return collab::isLocalOnlyTrack (baseProject, project, id); });
            info.changedScopes = (int) diff.changedScopeIds.size();

            for (auto& id : diff.changedScopeIds)
            {
                if (info.changedNames.size() >= 5)
                    break;

                const auto n = scopeDisplayName (project, &baseProject, id, true);
                info.changedNames.add (n);
            }
        }
    }
    catch (const std::exception&)
    {
        info.valid = false;
    }

    return info;
}

void SyncManager::reloadForDocument()
{
    linked = false;
    meta = {};
    base.reset();

    if (document.hasLocation())
    {
        const auto dir = collabDir (document.getProjectDir());

        if (auto m = readMeta (document.getProjectDir()))
        {
            meta = *m;
            linked = meta.projectId == document.getProject().projectId && meta.serverUrl.isNotEmpty();
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

    // 別の曲を開いたら、サーバーの状況は確認し直す
    {
        const juce::ScopedLock sl (statusLock);
        serverStatus = {};
    }

    notifiedHead = meta.baseRevision;
    sendChangeMessage();
    refreshInBackground();
}

std::optional<SyncManager::Meta> SyncManager::readMeta (const juce::File& projectDir)
{
    const auto metaFile = collabDir (projectDir).getChildFile ("meta.json");

    if (! metaFile.existsAsFile())
        return std::nullopt;

    try
    {
        auto j = nlohmann::json::parse (metaFile.loadFileAsString().toStdString());
        Meta m;
        m.serverUrl = toJuce (j.value ("serverUrl", std::string()));
        m.projectId = j.value ("projectId", std::string());
        m.baseRevision = j.value ("baseRevision", 0);
        m.userId = j.value ("userId", std::string());
        m.userName = toJuce (j.value ("userName", std::string()));
        return m;
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
}

void SyncManager::writeMeta (const juce::File& projectDir, const Meta& m)
{
    nlohmann::ordered_json j;
    j["serverUrl"] = toStd (m.serverUrl);
    j["projectId"] = m.projectId;
    j["baseRevision"] = m.baseRevision;
    j["userId"] = m.userId;
    j["userName"] = toStd (m.userName);
    const auto text = j.dump (2) + "\n";
    AppPaths::writeFileAtomically (collabDir (projectDir).getChildFile ("meta.json"), text.data(), text.size());
}

void SyncManager::saveBase (const juce::File& projectDir, const collab::Project& p)
{
    const auto text = collab::serialiseProject (p);
    AppPaths::writeFileAtomically (collabDir (projectDir).getChildFile ("base.json"), text.data(), text.size());
}

bool SyncManager::hasLocalChanges (const std::string& scopeId) const
{
    if (! linked || ! base)
        return false;

    return ! collab::isLocalOnlyTrack (*base, document.getProject(), scopeId) && ! collab::scopeEquals (*base, document.getProject(), scopeId);
}

juce::String SyncManager::scopeName (const std::string& scopeId) const
{
    return scopeDisplayName (document.getProject(), base ? &*base : nullptr, scopeId, false);
}

juce::String SyncManager::scopeDisplayName (const collab::Project& p, const collab::Project* baseProject, const std::string& scopeId,
                                            bool markRemoved)
{
    if (scopeId == p.tempoTrack.id)  return "テンポ"_ju;
    if (scopeId == p.meterTrack.id)  return "拍子"_ju;
    if (scopeId == p.chordTrack.id)  return "コード"_ju;
    if (scopeId == p.markerTrack.id) return "マーカー"_ju;
    if (scopeId == p.master.id)      return "マスター"_ju;
    if (scopeId == p.keyTrack.id)    return "キー"_ju;

    if (auto* t = p.findTrack (scopeId))
        return toJuce (t->name);

    // この PC では消したトラック（ベースにはある）
    if (baseProject != nullptr)
        if (auto* t = baseProject->findTrack (scopeId))
            return toJuce (t->name) + (markRemoved ? "（削除）"_ju : juce::String());

    return toJuce (scopeId);
}

//==============================================================================
juce::Result SyncManager::uploadMissingBlobs (const SyncClient& client, const std::vector<std::string>& hashes,
                                              const juce::File& projectDir, const std::map<std::string, std::string>& inlineContent,
                                              const SyncProgress& progress)
{
    if (hashes.empty())
        return juce::Result::ok();

    nlohmann::json list = nlohmann::json::array();
    for (auto& h : hashes)
        list.push_back (h);

    if (progress && ! progress ("サーバーにないファイルを確認しています"_ju, -1.0))
        return juce::Result::fail ("中止しました"_ju);

    auto check = client.post ("/blobs/check", { { "hashes", list } });

    if (! check.ok())
        return juce::Result::fail (check.message());

    // 送るものの一覧と合計サイズ（進み具合の表示用）
    struct Item { TransferUrl url; juce::File file; const std::string* inlineText = nullptr; juce::int64 size = 0; };
    std::vector<Item> items;
    juce::int64 totalBytes = 0;

    for (auto& m : check.body.value ("missing", nlohmann::json::array()))
    {
        Item item;
        item.url = TransferUrl::fromJson (m);

        if (auto it = inlineContent.find (item.url.hash); it != inlineContent.end())
        {
            item.inlineText = &it->second;
            item.size = (juce::int64) it->second.size();
        }
        else
        {
            item.file = AudioFiles::fileForHash (projectDir, item.url.hash);

            if (! item.file.existsAsFile())
                return juce::Result::fail ("オーディオが見つかりません: "_ju + item.file.getFullPathName());

            item.size = item.file.getSize();
        }

        totalBytes += item.size;
        items.push_back (std::move (item));
    }

    juce::int64 doneBytes = 0;
    int index = 0;

    for (auto& item : items)
    {
        ++index;
        juce::MemoryBlock data;

        if (item.inlineText != nullptr)
            data.append (item.inlineText->data(), item.inlineText->size());
        else if (! item.file.loadFileAsData (data))
            return juce::Result::fail ("オーディオを読めません: "_ju + item.file.getFullPathName());

        TransferProgress onBytes;

        if (progress)
            onBytes = [&] (juce::int64 sent, juce::int64)
            {
                const auto done = doneBytes + juce::jlimit<juce::int64> (0, item.size, sent);
                return progress ("アップロードしています "_ju + juce::String (index) + " / " + juce::String ((int) items.size())
                                   + "（"_ju + megabytes (done) + " / " + megabytes (totalBytes) + "）"_ju,
                                 totalBytes > 0 ? (double) done / (double) totalBytes : -1.0);
            };

        if (onBytes && ! onBytes (0, item.size))
            return juce::Result::fail ("中止しました"_ju);

        if (auto r = client.uploadBlob (item.url, data, onBytes, &directUploadBroken); r.failed())
            return r;

        doneBytes += item.size;
    }

    return juce::Result::ok();
}

juce::Result SyncManager::runRegister (const collab::Project& snapshot, const juce::File& projectDir, const SyncProgress& progress)
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

    if (auto r = uploadMissingBlobs (client, hashes, projectDir, { { hash, text } }, progress); r.failed())
        return r;

    if (progress)
        progress ("最初のリビジョンを作っています"_ju, -1.0);

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
juce::Result SyncManager::downloadRevision (const SyncClient& client, const std::string& projectId, int revision, collab::Project& result)
{
    auto rev = client.get ("/projects/" + toJuce (projectId) + "/revisions/" + juce::String (revision));

    if (! rev.ok())
        return juce::Result::fail (rev.message());

    juce::MemoryBlock data;

    if (auto r = client.download (TransferUrl::fromJson (rev.body["download"]), data); r.failed())
        return r;

    const auto text = data.toString().toStdString();

    // 途中で壊れた・別のものを受け取ったときは使わない
    if (collab::Sha256::hashHex (text) != rev.body.value ("projectJsonHash", std::string()))
        return juce::Result::fail ("ダウンロードしたプロジェクトのハッシュが一致しません"_ju);

    try
    {
        result = collab::parseProject (text);
    }
    catch (const std::exception& e)
    {
        return juce::Result::fail (juce::String::fromUTF8 (e.what()));
    }

    return juce::Result::ok();
}

juce::Result SyncManager::buildPreview (const SyncClient& client, const std::string& projectId, int head,
                                        const std::optional<collab::Project>& baseProject, PullPreview& preview)
{
    preview.head = head;

    if (auto r = downloadRevision (client, projectId, head, preview.headProject); r.failed())
        return r;

    preview.diff = collab::diffProjects (baseProject ? *baseProject : preview.headProject, preview.headProject);
    return juce::Result::ok();
}

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

    return buildPreview (client, meta.projectId, preview.head, base, preview);
}

juce::Result SyncManager::runDownloadAudio (const collab::Project& p, const juce::File& projectDir, const SyncProgress& progress)
{
    auto client = makeClient();
    auto audioDir = projectDir.getChildFile ("audio");
    audioDir.createDirectory();

    std::vector<std::string> needed;

    for (auto& hash : referencedAudio (p))
        if (! audioDir.getChildFile (toJuce (hash) + ".wav").existsAsFile())
            needed.push_back (hash);

    int index = 0;

    for (auto& hash : needed)
    {
        ++index;
        auto target = audioDir.getChildFile (toJuce (hash) + ".wav");
        const auto label = "オーディオをダウンロードしています "_ju + juce::String (index) + " / " + juce::String ((int) needed.size());

        if (progress && ! progress (label, (double) (index - 1) / (double) needed.size()))
            return juce::Result::fail ("中止しました"_ju);

        auto info = client.get ("/blobs/" + toJuce (hash));

        if (! info.ok())
            return juce::Result::fail (info.message());

        juce::MemoryBlock data;
        TransferProgress onBytes;

        if (progress)
            onBytes = [&] (juce::int64 got, juce::int64 total)
            {
                const double part = total > 0 ? (double) got / (double) total : 0.0;
                return progress (label + "（"_ju + megabytes (got) + (total > 0 ? " / " + megabytes (total) : juce::String()) + "）"_ju,
                                 ((double) (index - 1) + part) / (double) needed.size());
            };

        if (auto r = client.download (TransferUrl::fromJson (info.body), data, onBytes); r.failed())
            return r;

        collab::Sha256 sha;
        sha.update (data.getData(), data.getSize());

        if (sha.finishHex() != hash)
            return juce::Result::fail ("ダウンロードしたオーディオのハッシュが一致しません"_ju);

        // 一時ファイルに書いてから置き換える（§9: 途中で失敗してもローカルを壊さない）
        if (auto r = AppPaths::writeFileAtomically (target, data.getData(), data.getSize()); r.failed())
            return r;
    }

    return juce::Result::ok();
}

void SyncManager::applyDownload (const PullPreview& preview, const std::map<std::string, collab::Resolution>& choices)
{
    const auto& local = document.getProject();
    const auto baseProject = base ? *base : preview.headProject;

    // 取り込む前のローカルを念のため残しておく（競合を自分で選んだとしても、元に戻せるように）
    {
        const auto backup = collabDir (document.getProjectDir()).getChildFile ("before-download")
                              .getChildFile (juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + ".project.json");
        const auto text = collab::serialiseProject (local);
        AppPaths::writeFileAtomically (backup, text.data(), text.size());
    }

    auto merged = collab::resolvePull (baseProject, local, preview.headProject, choices);
    document.replaceFromSync (merged);

    meta.baseRevision = preview.head;
    saveMeta (document.getProjectDir());
    saveBase (document.getProjectDir(), preview.headProject);
    base = preview.headProject;
    document.save();
    notifiedHead = juce::jmax (notifiedHead, preview.head);
    sendChangeMessage();
    refreshInBackground();
}

std::shared_ptr<const SyncManager::PullPreview> SyncManager::headPreview() const
{
    const auto st = getServerStatus();
    return st.preview != nullptr && st.base == meta.baseRevision && st.head > meta.baseRevision ? st.preview : nullptr;
}

std::vector<collab::ScopeSyncState> SyncManager::scopeStates() const
{
    if (! linked || ! base)
        return {};

    const auto preview = headPreview();
    return collab::syncStates (*base, document.getProject(), preview != nullptr ? &preview->headProject : nullptr);
}

collab::ScopeSyncState SyncManager::scopeState (const std::string& scopeId) const
{
    collab::ScopeSyncState st;
    st.id = scopeId;

    if (! linked || ! base)
        return st;

    const auto& local = document.getProject();
    st.localOnly = collab::isLocalOnlyTrack (*base, local, scopeId);
    st.mine = ! st.localOnly && ! collab::scopeEquals (*base, local, scopeId);

    if (const auto preview = headPreview())
    {
        st.theirs = ! collab::scopeEquals (*base, preview->headProject, scopeId);
        st.conflict = st.mine && st.theirs && ! collab::scopeEquals (local, preview->headProject, scopeId);
    }

    return st;
}

//==============================================================================
static bool hasMissingPluginState (const collab::Track& t, const juce::File& dir)   { return PluginHost::hasMissingState (t, dir); }

juce::Result SyncManager::fetchUploadPlan (const collab::Project& local, const std::set<std::string>& scopeIds, UploadPlan& plan)
{
    auto client = makeClient();
    auto info = client.get ("/projects/" + toJuce (meta.projectId));

    if (! info.ok())
        return juce::Result::fail (info.message());

    plan.head = info.body.value ("headRevision", 0);
    plan.needsDownload = plan.head != meta.baseRevision;

    // アップするもの = ベース（= サーバーの最新）に、選んだスコープだけこの PC の内容を入れたもの
    const auto& baseProject = base ? *base : local;
    plan.snapshot = collab::uploadSnapshot (baseProject, local, scopeIds);
    plan.snapshot.name = local.name;
    plan.diff = collab::diffProjects (baseProject, plan.snapshot);

    // 外部プラグインのトラックはバウンスが必須。バウンス後に内容が変わっていたらアップできない（§3.7）
    const auto dir = document.getProjectDir();

    for (auto& t : plan.snapshot.tracks)
    {
        if (! plan.diff.touches (t.id))
            continue;

        auto stateHash = [dir] (const std::string& ref) { return PluginHost::stateHash (dir, ref); };
        const auto fp = collab::trackSourceFingerprint (t, stateHash);
        auto status = collab::renderStatus (t, fp);

        // 他の人のプラグイン（状態ファイルがこの環境にない）は正しいフィンガープリントを計算できない。
        // 音の元がベースから変わっていなければ、ベースのバウンスがそのまま使える（音量などの変更はアップできる）
        if (status == collab::RenderStatus::stale && base && hasMissingPluginState (t, dir))
            if (auto* before = base->findTrack (t.id); before != nullptr && before->render == t.render
                                                        && collab::trackSourceFingerprint (*before, stateHash) == fp)
                status = collab::RenderStatus::upToDate;

        if (status == collab::RenderStatus::missing || status == collab::RenderStatus::stale)
            plan.staleRenders.push_back (t.id);
    }


    return juce::Result::ok();
}

juce::Result SyncManager::runUpload (const UploadPlan& plan, const juce::String& message, const juce::File& projectDir,
                                     int& newRevision, const SyncProgress& progress)
{
    auto client = makeClient();
    const auto text = collab::serialiseProject (plan.snapshot);
    const auto hash = collab::Sha256::hashHex (text);

    auto hashes = referencedAudio (plan.snapshot);
    hashes.push_back (hash);

    if (auto r = uploadMissingBlobs (client, hashes, projectDir, { { hash, text } }, progress); r.failed())
        return r;

    if (progress)
        progress ("リビジョンを登録しています"_ju, -1.0);

    nlohmann::json changed = nlohmann::json::array();
    for (auto& id : plan.diff.changedScopeIds)
        changed.push_back (id);

    auto r = client.post ("/projects/" + toJuce (meta.projectId) + "/revisions",
                          { { "parentNumber", meta.baseRevision }, { "message", toStd (message) }, { "projectJsonHash", hash },
                            { "changedTrackIds", changed } });

    if (! r.ok())
        return juce::Result::fail (r.errorCode() == "not_head" ? "他の人が先にアップしました。ダウンロードしてからもう一度アップしてください"_ju
                                                               : r.message());

    newRevision = r.body.value ("number", meta.baseRevision + 1);
    return juce::Result::ok();
}

void SyncManager::applyUploaded (const UploadPlan& plan, int newRevision)
{
    meta.baseRevision = newRevision;
    saveMeta (document.getProjectDir());
    saveBase (document.getProjectDir(), plan.snapshot);
    base = plan.snapshot;
    notifiedHead = juce::jmax (notifiedHead, newRevision);
    sendChangeMessage();
    refreshInBackground();
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

juce::Result SyncManager::runRenameProject (const std::string& projectId, const juce::String& newName)
{
    nlohmann::json body { { "name", toStd (newName.trim()) } };
    auto client = makeClient();
    auto r = client.patch ("/projects/" + toJuce (projectId), body);

    if (! r.ok())
        return juce::Result::fail (r.message());

    // いま開いている曲なら、曲の名前も合わせる（次のアップで他の人にも伝わる）
    juce::MessageManager::callAsync ([this, alive = alive, projectId, newName]
    {
        if (*alive && linked && meta.projectId == projectId)
            document.perform ("曲名の変更"_ju, [n = toStd (newName.trim())] (collab::Project& p) { p.name = n; });
    });

    return juce::Result::ok();
}

juce::Result SyncManager::runDeleteProject (const std::string& projectId, const juce::File& localFolder)
{
    auto r = makeClient().del ("/projects/" + toJuce (projectId));

    if (! r.ok())
        return juce::Result::fail (r.message());

    // この PC のフォルダはサーバーとのつながりだけを外す（ベースも消して「この PC だけ」の曲にする）
    auto unlink = [&] (const juce::File& folder)
    {
        const auto dir = collabDir (folder);
        const auto m = readMeta (folder);

        if (! m || m->projectId != projectId)
            return;

        dir.getChildFile ("meta.json").deleteFile();
        dir.getChildFile ("base.json").deleteFile();
    };

    if (localFolder != juce::File())
        unlink (localFolder);

    juce::MessageManager::callAsync ([this, alive = alive, projectId]
    {
        if (*alive && linked && meta.projectId == projectId)
            reloadForDocument();   // いま開いている曲なら、同期の状態を「未登録」に戻す
    });

    return juce::Result::ok();
}

juce::Result SyncManager::runOpenFromServer (const std::string& projectId, const juce::File& parentDir, juce::File& createdFolder,
                                             const SyncProgress& progress)
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

    collab::Project project;

    if (auto r = downloadRevision (client, projectId, head, project); r.failed())
        return r;

    auto folder = parentDir.getChildFile (juce::File::createLegalFileName (toJuce (project.name)));

    // 同じ名前のフォルダがあれば「曲名 (2)」のように別の名前にする
    if (folder.exists())
        folder = folder.getNonexistentSibling (true);

    ProjectDocument::createFolderStructure (folder);

    if (auto r = runDownloadAudio (project, folder, progress); r.failed())
    {
        folder.deleteRecursively();   // 途中で止めた・失敗したときは、中途半端なフォルダを残さない
        return r;
    }

    const auto text = collab::serialiseProject (project);

    if (auto r = AppPaths::writeFileAtomically (folder.getChildFile ("project.json"), text.data(), text.size()); r.failed())
        return r;

    saveBase (folder, project);

    Meta m;
    m.serverUrl = client.getServerUrl();
    m.projectId = projectId;
    m.baseRevision = head;
    m.userId = me.body.value ("id", std::string());
    m.userName = toJuce (me.body.value ("displayName", std::string()));
    writeMeta (folder, m);   // 開いている曲の meta（メンバー）は触らない（このスレッドはバックグラウンド）

    createdFolder = folder;
    return juce::Result::ok();
}
