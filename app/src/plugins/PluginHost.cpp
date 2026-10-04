#include "PluginHost.h"

#include "AppPaths.h"

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
    // 曲の JSON（他の人がアップしたもの）から来るので、曲のフォルダの plugins-state/ の外は指させない
    const auto ref = toJuce (stateRef);

    if (! ref.startsWith ("plugins-state/") || ! AppPaths::isSafeRelativePath (ref))
        return {};

    return projectDir.getChildFile (ref);
}

bool hasMissingState (const collab::Track& t, const juce::File& dir)
{
    auto missing = [&] (const std::string& ref) { return ! ref.empty() && ! stateFile (dir, ref).existsAsFile(); };

    if (t.instrument && t.instrument->kind == collab::Instrument::Kind::external && missing (t.instrument->stateRef))
        return true;

    for (auto& e : t.effects)
        if (missing (e.stateRef))
            return true;

    return false;
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

    startTimer (15);

   #if ! JUCE_WINDOWS
    w->addKeyListener (&filteredKeys);
   #endif

    w->setContentOwned (editor, true);
    w->setResizable (editor->isResizable(), false);
    w->centreWithSize (w->getWidth(), w->getHeight());
    w->setVisible (true);
    windows[&plugin] = std::move (w);
}

void PluginWindows::closeFor (te::Plugin* p)
{
    windows.erase (p);

    if (windows.empty())
        stopTimer();
}

void PluginWindows::closeAll()
{
    windows.clear();
    stopTimer();
}

bool PluginWindows::forwardsKey (const juce::KeyPress& key)
{
    const auto code = key.getKeyCode();
    const auto mods = key.getModifiers();

    if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
        return false;

    if (code == juce::KeyPress::spaceKey || code == juce::KeyPress::numberPadMultiply || code == '*')
        return true;

    // Shift + 数字（マーカーへ移動）。キー配列によっては記号で届く
    return mods.isShiftDown() && ((code >= '1' && code <= '9') || juce::String ("!@#$%^&*(\"'()").containsChar ((juce::juce_wchar) code));
}

void PluginWindows::timerCallback()
{
    // ShareDAW を使っている間は、プラグインの画面を DAW の画面の上に浮かせておく（Cubase と同じ）。
    // ルーラーなど DAW の画面をクリックしてもプラグインの画面が後ろに隠れない。他のアプリに切り替えたら浮かせない
    const bool appInFront = juce::Process::isForegroundProcess();

    for (auto& [plugin, w] : windows)
        if (w->isAlwaysOnTop() != appInFront)
            w->setAlwaysOnTop (appInFront);

   #if JUCE_WINDOWS
    // プラグインの画面が前にあるときだけ（DAW の画面ではふつうにキーが届く）
    const bool pluginInFront = std::any_of (windows.begin(), windows.end(), [] (auto& w) { return w.second->isActiveWindow(); });

    // プラグインの画面で数値を打つこともあるので、テンキーの数字・「.」・「+」「-」「/」は DAW に送らない
    static const int keys[] = { juce::KeyPress::spaceKey, juce::KeyPress::numberPadMultiply,
                                '1', '2', '3', '4', '5', '6', '7', '8', '9' };   // 数字は Shift と一緒のときだけ（マーカーへ移動）

    const auto mods = juce::ModifierKeys::getCurrentModifiersRealtime();
    const bool plain = ! (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown());

    for (int key : keys)
    {
        const bool digit = key >= '1' && key <= '9';
        const bool down = pluginInFront && juce::KeyPress::isKeyCurrentlyDown (key) && (! digit || mods.isShiftDown());

        // 押した瞬間だけ（押しっぱなしで繰り返さない）
        if (down && ! keysDown[key] && plain && onKey)
            onKey (juce::KeyPress (key, digit ? juce::ModifierKeys (juce::ModifierKeys::shiftModifier) : juce::ModifierKeys(), 0));

        keysDown[key] = down;
    }
   #endif
}
