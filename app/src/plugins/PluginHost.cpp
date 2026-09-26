#include "PluginHost.h"

#include "collab/Sha256.h"
#include "ui/Theme.h"

namespace PluginHost
{

std::string currentOs()
{
   #if JUCE_WINDOWS
    return "windows";
   #elif JUCE_MAC
    return "mac";
   #else
    return "linux";
   #endif
}

collab::ExternalPlugin describe (const juce::PluginDescription& d)
{
    collab::ExternalPlugin p;
    p.format = d.pluginFormatName == "AudioUnit" ? "AU" : "VST3";
    p.name = toStd (d.name);
    p.vendor = toStd (d.manufacturerName);
    p.uid = toStd (d.createIdentifierString());
    p.os = currentOs();
    return p;
}

std::optional<juce::PluginDescription> find (te::Engine& engine, const collab::ExternalPlugin& p)
{
    auto& known = engine.getPluginManager().knownPluginList;

    if (auto d = known.getTypeForIdentifierString (toJuce (p.uid)))
        return *d;

    // 別の環境で作られた場合など: 名前・メーカー・形式で探す
    for (auto& d : known.getTypes())
        if (d.name == toJuce (p.name) && d.manufacturerName == toJuce (p.vendor) && describe (d).format == p.format)
            return d;

    return std::nullopt;
}

juce::Array<juce::PluginDescription> list (te::Engine& engine, bool instruments)
{
    juce::Array<juce::PluginDescription> result;

    for (auto& d : engine.getPluginManager().knownPluginList.getTypes())
        if (d.isInstrument == instruments && (d.pluginFormatName == "VST3" || d.pluginFormatName == "AudioUnit"))
            result.add (d);

    std::sort (result.begin(), result.end(), [] (auto& a, auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    return result;
}

std::string stateRefFor (const std::string& id)
{
    return "plugins-state/" + id + ".bin";
}

juce::File stateFile (const juce::File& projectDir, const std::string& stateRef)
{
    return projectDir.getChildFile (toJuce (stateRef));
}

std::string stateHash (const juce::File& projectDir, const std::string& stateRef)
{
    juce::MemoryBlock data;

    if (stateRef.empty() || ! stateFile (projectDir, stateRef).loadFileAsData (data))
        return {};

    collab::Sha256 sha;
    sha.update (data.getData(), data.getSize());
    return sha.finishHex();
}

ScanResult scan (te::Engine& engine, std::function<void (float, const juce::String&)> onProgress)
{
    ScanResult result;
    auto& pm = engine.getPluginManager();
    auto& known = pm.knownPluginList;
    auto deadMansPedal = engine.getTemporaryFileManager().getTempDirectory().getChildFile ("plugin-scan-dead-mans-pedal");
    const auto blacklistBefore = known.getBlacklistedFiles();

    for (int i = 0; i < pm.pluginFormatManager.getNumFormats(); ++i)
    {
        auto* format = pm.pluginFormatManager.getFormat (i);
        const auto name = format->getName();

        if (! (name == "VST3" || name == "AudioUnit"))
            continue;

        juce::PluginDirectoryScanner scanner (known, *format, format->getDefaultLocationsToSearch(), true, deadMansPedal, true);
        juce::String current;

        while (scanner.scanNextFile (true, current))
            if (onProgress)
                onProgress (scanner.getProgress(), current);

        // スキャンに失敗した（クラッシュした）ものは、以後読み込まない（§3.4）。
        // 子プロセスがクラッシュしたものは KnownPluginList がすでにブラックリストに入れている。
        for (auto& failed : scanner.getFailedFiles())
            known.addToBlacklist (failed);
    }

    for (auto& f : known.getBlacklistedFiles())
        if (! blacklistBefore.contains (f))
            result.blacklisted.add (f);

    result.found = known.getNumTypes();

    // 一覧は変更通知で非同期に保存されるので、すぐ終了しても残るようにここで保存する
    if (auto xml = known.createXml())
        engine.getPropertyStorage().setXmlProperty (te::SettingID::knownPluginList64, *xml);   // 64bit のみ対応
    return result;
}

}

//==============================================================================
void PluginWindows::show (te::Plugin& plugin, const juce::String& title)
{
    if (auto it = windows.find (&plugin); it != windows.end())
    {
        it->second->setVisible (true);
        it->second->toFront (true);
        return;
    }

    auto* ext = dynamic_cast<te::ExternalPlugin*> (&plugin);

    if (ext == nullptr || ext->getAudioPluginInstance() == nullptr)
        return;

    auto* editor = ext->getAudioPluginInstance()->createEditorIfNeeded();

    if (editor == nullptr)
        editor = new juce::GenericAudioProcessorEditor (*ext->getAudioPluginInstance());

    struct Window  : public juce::DocumentWindow
    {
        Window (const juce::String& t, PluginWindows& o, te::Plugin* p)
            : DocumentWindow (t, Theme::panel, DocumentWindow::closeButton), owner (o), plugin (p) {}

        void closeButtonPressed() override
        {
            juce::MessageManager::callAsync ([&o = owner, p = plugin] { o.closeFor (p); });
        }

        PluginWindows& owner;
        te::Plugin* plugin;
    };

    auto w = std::make_unique<Window> (title, *this, &plugin);
    w->setUsingNativeTitleBar (true);
    w->setContentOwned (editor, true);
    w->setResizable (editor->isResizable(), false);
    w->centreWithSize (w->getWidth(), w->getHeight());
    w->setVisible (true);
    windows[&plugin] = std::move (w);
}

void PluginWindows::closeFor (te::Plugin* p)
{
    windows.erase (p);
}

void PluginWindows::closeAll()
{
    windows.clear();
}
