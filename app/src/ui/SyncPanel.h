#pragma once

#include <map>
#include <set>

#include "Common.h"
#include "sync/SyncManager.h"

/**
    同期パネル（右側）。プロジェクトのすべてのトラック（とテンポ・拍子・コードなど）を
    「名前 | ダウンロード | アップロード」の表にする。

    ・他の人がアップして新しくなったもの … ダウンロードの欄に自動でチェック（外すと今のまま）
    ・この PC で変えたもの … アップロードの欄に自動でチェック（外すと今回はアップしない）
    ・両方で変わったもの（競合）… 色を付ける。トラックは両方にチェック（サーバーの版を使い、自分の版を別トラックで残す）。
      片方だけにすればその版を使う。テンポなどトラック以外は、どちらかを選ぶ

    行をクリックすると、自分の変更とサーバーの変更の中身（何小節目の何が変わったか）を並べて見せる。
    自分の変更は 1 件ずつ「変更前にする」で元に戻せて、戻したものは「変更後にする」でいつでも戻せる（聞き比べ用）。
    「>>」で右端の細い帯に畳み、「<<」で開く。畳んでいてもダウンロード・アップロードの件数を色分けして出す。
*/
class SyncPanel  : public juce::Component,
                   public juce::SettableTooltipClient,
                   private juce::ChangeListener,
                   private juce::Timer
{
public:
    SyncPanel (SyncManager&, ProjectDocument&, juce::PropertiesFile&);
    ~SyncPanel() override;

    std::function<void()> onRegister, onServerSettings, onOpenPicker, onToggle;

    /** ダウンロード（サーバーの変更・競合への選択つき）。 */
    std::function<void (const std::map<std::string, collab::Resolution>& choices)> onDownload;

    /**
        アップロード（この PC で変えたもののうち、アップロードの欄のチェックを外したもの以外）。
        サーバーに新しい版があれば、先に choices でダウンロードしてからアップする（「両方」でできたトラックも含む）。
    */
    std::function<void (const std::set<std::string>& excludedScopes, const juce::String& message,
                        const std::map<std::string, collab::Resolution>& choices)> onUpload;

    std::function<void (const collab::Change&)> onJump;

    /** 他の人の変更を自動でダウンロードするか（この PC の設定）。 */
    bool autoPullEnabled() const;

    /** アップ・ダウンロードが済んだら、選択を消す。 */
    void clearAfterSync();

    bool isCollapsed() const noexcept           { return collapsed; }
    void setCollapsed (bool);
    int getPreferredWidth() const noexcept      { return collapsed ? collapsedWidth : expandedWidth; }

    /** 開いたときの幅（左の端をドラッグで変える。この PC の設定）。 */
    void setExpandedWidth (int w) noexcept      { expandedWidth = juce::jlimit (minExpandedWidth, maxExpandedWidth, w); }
    int getExpandedWidth() const noexcept       { return expandedWidth; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    void mouseUp (const juce::MouseEvent&) override;

    static constexpr int collapsedWidth = 46, minExpandedWidth = 210, maxExpandedWidth = 520;
    int expandedWidth = 240;

private:
    class List;

    SyncManager& sync;
    ProjectDocument& document;
    juce::PropertiesFile& settings;

    juce::Viewport viewport;
    std::unique_ptr<List> list;
    juce::TextButton toggleButton, downloadButton, uploadButton, registerButton, settingsButton;
    juce::ToggleButton autoPull;
    juce::Label statusLabel;

    // 表のチェック（既定から変えたものだけ覚える）
    std::map<std::string, bool> downloadChecks, uploadChecks;
    std::string expandedId;                                  // 中身を開いている行
    bool collapsed = false;

    // 畳んだ帯・見出しに出す件数
    int downloadCount = 0, uploadCount = 0, conflictCount = 0;
    bool offline = false;

    bool dirty = true;
    int ticks = 0;
    bool laidOutLinked = false;   // 最後に resized() したときに、サーバーにある曲だったか

    // 変更前に戻した変更（それぞれ「変更後にする」で戻せる）。戻す前の版を覚えておき、その部分だけをそこから戻す
    struct Reverted { collab::Change change; collab::Project after; };
    std::vector<Reverted> reverted;
    void revertChange (const collab::Change&);
    void restoreChange (size_t index);

    struct Checks { bool canDownload = false, canUpload = false, download = false, upload = false; };
    Checks checksFor (const collab::ScopeSyncState&) const;
    void toggleCheck (const collab::ScopeSyncState&, bool downloadColumn);

    /** 表のチェックから、ダウンロードの選択とアップしないものを作る。未解決の競合があればその名前を返す。 */
    std::map<std::string, collab::Resolution> currentChoices (juce::String* unresolved) const;
    std::set<std::string> currentExcluded() const;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void rebuild();
    void paintCounts (juce::Graphics&, juce::Rectangle<int>, bool vertical) const;
};

/** 画面の右下に出すお知らせ（「○○ さんがアップしました [ダウンロード]」）。 */
class SyncToast  : public juce::Component,
                   private juce::Timer
{
public:
    SyncToast();

    void show (const juce::String& title, const juce::String& body, const juce::String& actionLabel, std::function<void()> action,
               juce::Colour colour);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::String title, body;
    juce::Colour colour;
    juce::TextButton actionButton, closeButton;
    std::function<void()> action;

    void timerCallback() override;
};
