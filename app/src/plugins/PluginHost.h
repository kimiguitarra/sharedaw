#pragma once

#include <optional>

#include "Common.h"
#include "collab/Project.h"

/**
    外部プラグイン（§3.4）: VST3（Win / Mac）、AU（Mac のみ）。
    - スキャンは別プロセスで行う（Tracktion の子プロセススキャナー）。クラッシュしたものはブラックリストに入れる
    - 状態は plugins-state/<id>.bin に保存し、JSON には stateRef として参照だけを持つ（持ち主の環境専用）
*/
namespace PluginHost
{
    /** このアプリの OS 名（JSON の plugin.os）。 */
    std::string currentOs();

    collab::ExternalPlugin describe (const juce::PluginDescription&);
    std::optional<juce::PluginDescription> find (te::Engine&, const collab::ExternalPlugin&);

    /** 音源（isInstrument）またはエフェクトの一覧。 */
    juce::Array<juce::PluginDescription> list (te::Engine&, bool instruments);

    std::string stateRefFor (const std::string& id);
    juce::File stateFile (const juce::File& projectDir, const std::string& stateRef);
    std::string stateHash (const juce::File& projectDir, const std::string& stateRef);

    /** トラックの外部プラグインの状態ファイルがこの PC にないか（他の人のプラグインのトラック）。 */
    bool hasMissingState (const collab::Track&, const juce::File& projectDir);

    struct ScanResult
    {
        int found = 0;
        juce::StringArray blacklisted;
    };

    /** プラグインをスキャンする（バックグラウンドスレッドから呼ぶ）。 */
    ScanResult scan (te::Engine&, std::function<void (float progress, const juce::String& name)> onProgress);
}

/** プラグインのエディタ（GUI）のウィンドウ。 */
class PluginWindows  : private juce::Timer
{
public:
    void show (te::Plugin&, const juce::String& title);
    void closeAll();
    void closeFor (te::Plugin*);

    /**
        プラグインの画面の上でも DAW のキー（Space で再生・停止、テンキーの「*」で録音、Shift + 数字でマーカー）が効くようにする。
        Mac はプラグインが使わなかったキーがウィンドウに届くので keyListener で受ける。
        Windows はプラグインの画面（別のウィンドウ）がキーを持っていってしまうので、プラグインの画面が前にある間、
        キーが押されたかを見て onKey を呼ぶ。
    */
    juce::KeyListener* keyListener = nullptr;
    std::function<void (const juce::KeyPress&)> onKey;

    /** プラグインの画面から DAW に送るキーか（数値の入力に使うテンキーの数字・「.」などは送らない）。 */
    static bool forwardsKey (const juce::KeyPress&);

private:
    std::map<te::Plugin*, std::unique_ptr<juce::DocumentWindow>> windows;
    std::map<int, bool> keysDown;

    struct FilteredKeys  : public juce::KeyListener
    {
        explicit FilteredKeys (PluginWindows& o) : owner (o) {}

        bool keyPressed (const juce::KeyPress& key, juce::Component* origin) override
        {
            return owner.keyListener != nullptr && forwardsKey (key) && owner.keyListener->keyPressed (key, origin);
        }

        PluginWindows& owner;
    };

    FilteredKeys filteredKeys { *this };

    void timerCallback() override;
};
