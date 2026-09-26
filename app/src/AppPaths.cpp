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

}
