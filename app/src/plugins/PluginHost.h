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

    struct ScanResult
    {
        int found = 0;
        juce::StringArray blacklisted;
    };

    /** プラグインをスキャンする（バックグラウンドスレッドから呼ぶ）。 */
    ScanResult scan (te::Engine&, std::function<void (float progress, const juce::String& name)> onProgress);
}

/** プラグインのエディタ（GUI）のウィンドウ。 */
class PluginWindows
{
public:
    void show (te::Plugin&, const juce::String& title);
    void closeAll();
    void closeFor (te::Plugin*);

private:
    std::map<te::Plugin*, std::unique_ptr<juce::DocumentWindow>> windows;
};
