#include "AppPaths.h"

namespace AppPaths
{

juce::File getAssetsDir()
{
    auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);

    juce::Array<juce::File> candidates;
   #if JUCE_MAC
    candidates.add (exe.getParentDirectory().getParentDirectory().getChildFile ("Resources/assets"));
   #endif
    candidates.add (exe.getParentDirectory().getChildFile ("assets"));

    for (auto& c : candidates)
        if (c.getChildFile ("instruments").isDirectory())
            return c;

    // 開発時: ソースツリーの /assets を上方向に探す
    for (auto dir = exe.getParentDirectory(); dir.exists() && ! dir.isRoot(); dir = dir.getParentDirectory())
        if (auto a = dir.getChildFile ("assets"); a.getChildFile ("instruments").isDirectory())
            return a;

    return {};
}

juce::File getAppDataDir()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                  .getChildFile ("ShareDAW");
    dir.createDirectory();
    return dir;
}

bool isSafeRelativePath (const juce::String& p)
{
    if (p.isEmpty() || p.startsWithChar ('/') || p.startsWithChar ('\\') || p.containsChar (':'))
        return false;

    for (auto& part : juce::StringArray::fromTokens (p, "/\\", {}))
        if (part == ".." || part.isEmpty())
            return false;

    return true;
}

juce::Result writeFileAtomically (const juce::File& target, const void* data, size_t size)
{
    target.getParentDirectory().createDirectory();
    juce::TemporaryFile temp (target);

    if (! temp.getFile().replaceWithData (data, size))
        return juce::Result::fail ("書き込みに失敗しました: "_ju + temp.getFile().getFullPathName());

    if (! temp.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("ファイルを置き換えられませんでした: "_ju + target.getFullPathName());

    return juce::Result::ok();
}

}
