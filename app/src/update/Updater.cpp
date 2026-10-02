#include "Updater.h"

#include "AppPaths.h"

#include "collab/Sha256.h"

#ifndef SHAREDAW_BUILD_NUMBER
 #define SHAREDAW_BUILD_NUMBER 0
#endif

namespace Updater
{

namespace
{
    std::atomic<bool> relaunchRequested { false };

    const char* oldSuffix = ".sharedaw-old";

    /** ダウンロードしたファイルを置いておく場所（置き換え先と同じドライブにして rename できるようにする）。 */
    juce::File stagingDir()
    {
        const auto root = installRoot();

       #if JUCE_MAC
        // .app の中に余計なファイルを置くと署名の検証に響くので、.app の隣に置く
        return root.getParentDirectory().getChildFile (".ShareDAW-update");
       #else
        return root.getChildFile (".sharedaw-update");
       #endif
    }

    juce::String hashFile (const juce::File& f)
    {
        juce::FileInputStream in (f);

        if (! in.openedOk())
            return {};

        collab::Sha256 sha;
        juce::HeapBlock<char> buffer (1 << 16);

        for (;;)
        {
            const auto n = in.read (buffer.get(), 1 << 16);

            if (n <= 0)
                break;

            sha.update (buffer.get(), (size_t) n);
        }

        return toJuce (sha.finishHex());
    }

    /** マニフェストのパスが installRoot の外を指していないこと。 */
    struct FileEntry
    {
        juce::String path, hash;
        juce::int64 size = 0;
        bool executable = false;
    };
}

juce::String executableName()
{
    // 起動時の名前を覚えておく（更新で exe の名前をずらした後に聞かれても、元の名前を返す）
    static const auto name = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFileName();
    return name;
}

int currentBuild()
{
    return SHAREDAW_BUILD_NUMBER;
}

juce::String platformName()
{
   #if JUCE_WINDOWS
    return "windows";
   #elif JUCE_MAC
    return "mac";
   #else
    return "linux";
   #endif
}

juce::File installRoot()
{
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);

   #if JUCE_MAC
    // .../ShareDAW.app/Contents/MacOS/ShareDAW
    return exe.getParentDirectory().getParentDirectory().getParentDirectory();
   #else
    return exe.getParentDirectory();
   #endif
}

juce::Result fetchLatest (const SyncClient& client, std::optional<Info>& out)
{
    out.reset();
    auto r = client.get ("/app/latest?platform=" + platformName());

    if (r.status == 404)
        return juce::Result::ok();

    if (! r.ok())
        return juce::Result::fail (r.message());

    Info info;
    info.build = r.body.value ("build", 0);
    info.version = toJuce (r.body.value ("version", std::string()));
    info.notes = toJuce (r.body.value ("notes", std::string()));

    if (r.body.contains ("manifest"))
        info.manifest = TransferUrl::fromJson (r.body["manifest"]);

    if (info.build <= 0 || info.manifest.url.isEmpty())
        return juce::Result::fail ("サーバーの更新情報が正しくありません"_ju);

    out = info;
    return juce::Result::ok();
}


namespace
{
    /**
        staging のファイル（名前はハッシュ）を root の中の本来の場所に置く。今あるファイルは「名前 + oldSuffix」にずらしてから置くので、
        実行中の exe も置き換えられる（Windows は使用中のファイルを上書きできないが、名前は変えられる）。
        途中で失敗したら、それまでに置いた分を消して元のファイルを戻す（古いのと新しいのが混ざった状態にしない）。
        ずらした古いファイルは、Windows では次の起動時に消す（cleanup.txt）。ほかの OS ではすぐ消す。
    */
    juce::Result applyStagedFiles (const juce::File& root, const juce::File& staging, const std::vector<FileEntry>& changed)
    {
        struct Done { juce::File dest, old; };
        std::vector<Done> done;

        auto rollback = [&]
        {
            for (auto it = done.rbegin(); it != done.rend(); ++it)
            {
                it->dest.deleteFile();

                if (it->old != juce::File())
                    it->old.moveFileTo (it->dest);
            }
        };

        for (auto& e : changed)
        {
            const auto source = staging.getChildFile (e.hash);
            const auto dest = root.getChildFile (e.path);
            dest.getParentDirectory().createDirectory();

            juce::File old;

            if (dest.existsAsFile())
            {
                old = dest.getSiblingFile (dest.getFileName() + oldSuffix);
                old.deleteFile();

                if (! dest.moveFileTo (old))
                {
                    rollback();
                    return juce::Result::fail ("置き換えられません（使用中）: "_ju + dest.getFullPathName());
                }
            }

            done.push_back ({ dest, old });

            if (! source.copyFileTo (dest))
            {
                rollback();
                return juce::Result::fail ("置き換えられません: "_ju + dest.getFullPathName());
            }

           #if ! JUCE_WINDOWS
            if (e.executable)
                dest.setExecutePermission (true);
           #endif
        }

        juce::StringArray leftovers;

        for (auto& d : done)
            if (d.old != juce::File())
            {
               #if JUCE_WINDOWS
                leftovers.add (d.old.getFullPathName());
               #else
                d.old.deleteFile();   // 実行中のプロセスは消したファイルの中身を使い続けられる
               #endif
            }

        // 残ったファイルは次の起動時に消す
        staging.getChildFile ("cleanup.txt").replaceWithText (leftovers.joinIntoString ("\n"));

        for (auto& f : staging.findChildFiles (juce::File::findFiles, false))
            if (f.getFileName() != "cleanup.txt")
                f.deleteFile();

        return juce::Result::ok();
    }
}

juce::Result downloadAndInstall (const SyncClient& client, const Info& info, std::function<bool (double, const juce::String&)> progress)
{
    const auto root = installRoot();
    auto report = [&] (double p, const juce::String& text) { return progress == nullptr || progress (p, text); };

    // 1. マニフェスト
    report (0.0, "更新の内容を確認しています…"_ju);
    juce::MemoryBlock manifestData;

    if (auto r = client.download (info.manifest, manifestData); r.failed())
        return r;

    if (toJuce (collab::Sha256::hashHex (std::string_view ((const char*) manifestData.getData(), manifestData.getSize()))) != toJuce (info.manifest.hash))
        return juce::Result::fail ("更新の一覧が壊れています（ハッシュが一致しません）"_ju);

    std::vector<FileEntry> files;

    try
    {
        auto j = nlohmann::json::parse ((const char*) manifestData.getData(), (const char*) manifestData.getData() + manifestData.getSize());

        for (auto& f : j.at ("files"))
        {
            FileEntry e { toJuce (f.at ("path").get<std::string>()), toJuce (f.at ("hash").get<std::string>()),
                          f.value ("size", (juce::int64) 0), f.value ("executable", false) };

            if (! AppPaths::isSafeRelativePath (e.path))
                return juce::Result::fail ("更新の一覧に不正なパスがあります: "_ju + e.path);

            files.push_back (e);
        }
    }
    catch (const std::exception&)
    {
        return juce::Result::fail ("更新の一覧を読み込めません"_ju);
    }

    // 2. 手元と違うファイルを探す
    std::vector<FileEntry> changed;
    juce::int64 totalBytes = 0;

    for (size_t i = 0; i < files.size(); ++i)
    {
        auto& e = files[i];
        const auto local = root.getChildFile (e.path);

        if (! report (0.05 * (double) i / (double) files.size(), "手元のファイルを確認しています…"_ju))
            return juce::Result::fail ("中止しました"_ju);

        if (local.existsAsFile() && local.getSize() == e.size && hashFile (local) == e.hash)
            continue;

        changed.push_back (e);
        totalBytes += e.size;
    }

    if (changed.empty())
        return juce::Result::ok();

    // 3. 変わったファイルをダウンロード（まだ何も置き換えない）
    auto staging = stagingDir();

    if (! staging.createDirectory())
        return juce::Result::fail ("更新用のフォルダを作れません（書き込みできない場所にアプリがあります）: "_ju + staging.getFullPathName());

    juce::int64 doneBytes = 0;

    for (size_t i = 0; i < changed.size(); ++i)
    {
        auto& e = changed[i];
        const auto target = staging.getChildFile (e.hash);

        const auto text = "ダウンロードしています（"_ju + juce::String ((int) i + 1) + " / " + juce::String ((int) changed.size())
                            + "、"_ju + juce::File::descriptionOfSizeInBytes (totalBytes) + "）"_ju;

        if (! report (0.05 + 0.9 * (double) doneBytes / (double) std::max<juce::int64> (1, totalBytes), text))
            return juce::Result::fail ("中止しました"_ju);

        if (! (target.existsAsFile() && hashFile (target) == e.hash))   // 前回の途中までを再利用する
        {
            auto t = client.get ("/blobs/" + e.hash);

            if (! t.ok())
                return juce::Result::fail (e.path + ": " + t.message());

            juce::MemoryBlock data;

            if (auto r = client.download (TransferUrl::fromJson (t.body), data); r.failed())
                return juce::Result::fail (e.path + ": " + r.getErrorMessage());

            if (toJuce (collab::Sha256::hashHex (std::string_view ((const char*) data.getData(), data.getSize()))) != e.hash)
                return juce::Result::fail (e.path + ": ダウンロードした内容が壊れています"_ju);

            if (! target.replaceWithData (data.getData(), data.getSize()))
                return juce::Result::fail ("書き込めません: "_ju + target.getFullPathName());
        }

        doneBytes += e.size;
    }

    // 4. 置き換える（途中で失敗したら、置き換えた分を元に戻す）
    report (0.97, "ファイルを置き換えています…"_ju);

    if (auto r = applyStagedFiles (root, staging, changed); r.failed())
        return r;

    report (1.0, "完了しました"_ju);
    return juce::Result::ok();
}

void cleanUpPreviousUpdate()
{
    executableName();   // 元の名前を覚えておく
    const auto staging = stagingDir();

    if (! staging.isDirectory())
        return;

    juce::StringArray lines;
    lines.addLines (staging.getChildFile ("cleanup.txt").loadFileAsString());

    for (auto& l : lines)
        if (l.trim().endsWith (oldSuffix))
            juce::File (l.trim()).deleteFile();

    staging.deleteRecursively();
}

void requestRelaunch()
{
    relaunchRequested = true;
}

void relaunchIfRequested()
{
    if (! relaunchRequested)
        return;

    // 引数を付けると多重起動のチェックを通る（終了処理中の古いプロセスがまだいるため）
   #if JUCE_MAC
    juce::ChildProcess p;
    p.start (juce::StringArray { "/usr/bin/open", "-n", installRoot().getFullPathName(), "--args", "--after-update" });
   #else
    // Windows では古い exe は *.old に名前が変わっているので、元の名前で新しい exe を起動する
    installRoot().getChildFile (executableName()).startAsProcess ("--after-update");
   #endif
}

} // namespace Updater
