#pragma once

#include "Common.h"
#include "sync/SyncManager.h"

/**
    同期パネル（右側に出す。GitHub Desktop / TortoiseSVN のような「今の状況」の一覧）。

    ・サーバーとの接続、この PC とサーバーのリビジョン
    ・サーバーの新しい変更（他の人のアップロード）と「取り込む」
    ・この PC の変更（まだ送っていないもの）と「アップ」
    ・ロック、最近の履歴

    サーバーの状況は SyncManager が定期的に確認する。この PC の変更は編集のたびに計算し直す。
*/
class SyncPanel  : public juce::Component,
                   private juce::ChangeListener,
                   private juce::Timer
{
public:
    SyncPanel (SyncManager&, ProjectDocument&, juce::PropertiesFile&);
    ~SyncPanel() override;

    std::function<void()> onRegister, onServerSettings, onPull, onOpenPicker, onClose, onShowHistory;
    std::function<void (const juce::String& message, bool releaseLocks)> onPush;
    std::function<void (const collab::Change&)> onJump;

    /** 他の人の変更を自動で取り込むか（この PC の設定）。 */
    bool autoPullEnabled() const;

    /** 編集で変わったこの PC の変更の数（ツールバーのバッジ用）。 */
    int localChangeCount() const noexcept        { return localCount; }

    /** アップしたらコメント欄を空にする。 */
    void clearComment();

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

    static constexpr int preferredWidth = 360;

private:
    class Content;

    SyncManager& sync;
    ProjectDocument& document;
    juce::PropertiesFile& settings;

    juce::Viewport viewport;
    std::unique_ptr<Content> content;
    juce::TextButton closeButton;

    int localCount = 0;
    bool localDirty = true;
    int ticksSinceRebuild = 0;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void rebuild();
};

/** ツールバーに出す同期の状態（「最新」「↑3」「↓2」など）。クリックで同期パネルを開く。 */
class SyncBadge  : public juce::Button,
                   private juce::ChangeListener,
                   private juce::Timer
{
public:
    SyncBadge (SyncManager&, ProjectDocument&);
    ~SyncBadge() override;

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    SyncManager& sync;
    ProjectDocument& document;
    int local = 0;
    bool dirty = true;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
};

/** 画面の右下に出すお知らせ（「○○ さんがアップしました [取り込む]」）。 */
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
