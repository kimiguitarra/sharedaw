#pragma once

#include <map>
#include <set>

#include "Common.h"
#include "sync/SyncManager.h"

/**
    同期パネル（右側）。プロジェクトのすべてのトラック（とテンポ・拍子・コードなど）を一覧にして:

    ・この PC で変えたもの … チェック。チェックしたものだけをアップする
    ・他の人がアップして新しくなったもの … 色を付けてダウンロードを促す（採用しない、も選べる）
    ・両方で変わったもの（競合）… 自分の版 / サーバーの版 / 両方残す（トラックだけ）を選んでからダウンロードする

    行を選ぶと、自分の変更とサーバーの変更の中身（何小節目の何が変わったか）を並べて見せる。
*/
class SyncPanel  : public juce::Component,
                   private juce::ChangeListener,
                   private juce::Timer
{
public:
    SyncPanel (SyncManager&, ProjectDocument&, juce::PropertiesFile&);
    ~SyncPanel() override;

    std::function<void()> onRegister, onServerSettings, onOpenPicker, onClose;

    /** ダウンロード（競合などの選択つき）。 */
    std::function<void (const std::map<std::string, collab::Resolution>& choices)> onDownload;

    /**
        アップ（この PC で変えたもののうち、チェックを外したもの以外）。
        サーバーに新しい版があれば、先に choices でダウンロードしてからアップする（「両方残す」でできたトラックも含む）。
    */
    std::function<void (const std::set<std::string>& excludedScopes, const juce::String& message,
                        const std::map<std::string, collab::Resolution>& choices)> onUpload;

    std::function<void (const collab::Change&)> onJump;

    /** 他の人の変更を自動でダウンロードするか（この PC の設定）。 */
    bool autoPullEnabled() const;

    /** アップ・ダウンロードが済んだら、コメントと選択を消す。 */
    void clearAfterSync();

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

    static constexpr int preferredWidth = 380;

private:
    class List;

    SyncManager& sync;
    ProjectDocument& document;
    juce::PropertiesFile& settings;

    juce::Viewport viewport;
    std::unique_ptr<List> list;
    juce::TextButton closeButton, downloadButton, uploadButton, registerButton, settingsButton;
    juce::TextEditor comment;
    juce::ToggleButton autoPull;
    juce::Label statusLabel, hintLabel;

    std::set<std::string> excluded;                          // この PC の変更のうち、チェックを外したもの
    std::map<std::string, collab::Resolution> choices;       // サーバーの変更・競合への選択
    std::string expandedId;                                  // 中身を開いている行

    bool dirty = true;
    int ticks = 0;
    bool laidOutLinked = false;   // 最後に resized() したときに、サーバーにある曲だったか

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void rebuild();
    void updateButtons();
};

/** ツールバーの同期の状態（「最新」「↓2」「↑3」「競合」など）。クリックで同期パネルを開く。 */
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
    int mine = 0, theirs = 0, conflicts = 0;
    bool dirty = true;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
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
