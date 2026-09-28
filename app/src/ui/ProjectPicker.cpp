#include "ProjectPicker.h"

#include "Dialogs.h"
#include "Theme.h"

namespace
{
    juce::String normaliseUrl (juce::String url)
    {
        url = url.trim();

        while (url.endsWithChar ('/'))
            url = url.dropLastCharacters (1);

        return url.toLowerCase();
    }

    /** 「3 分前」「昨日 14:02」「10/27」のような表示。 */
    juce::String relativeTime (juce::Time t)
    {
        if (t.toMilliseconds() <= 0)
            return {};

        const auto now = juce::Time::getCurrentTime();
        const auto seconds = (now - t).inSeconds();

        if (seconds < 60)          return "たった今"_ju;
        if (seconds < 3600)        return juce::String ((int) (seconds / 60)) + " 分前"_ju;
        if (seconds < 24 * 3600)   return juce::String ((int) (seconds / 3600)) + " 時間前"_ju;
        if (seconds < 48 * 3600)   return "昨日 "_ju + t.formatted ("%H:%M");

        return t.formatted ("%m/%d %H:%M");
    }
}

//==============================================================================
void ProjectPicker::remember (juce::PropertiesFile& settings, const juce::File& folder)
{
    if (folder == juce::File())
        return;

    auto list = juce::StringArray::fromLines (settings.getValue ("knownProjects"));
    list.removeString (folder.getFullPathName());
    list.insert (0, folder.getFullPathName());
    list.removeEmptyStrings();

    while (list.size() > 100)
        list.remove (list.size() - 1);

    settings.setValue ("knownProjects", list.joinIntoString ("\n"));
}

juce::Array<juce::File> ProjectPicker::knownFolders (juce::PropertiesFile& settings)
{
    juce::Array<juce::File> result;
    auto add = [&] (const juce::File& f)
    {
        if (f.getChildFile ("project.json").existsAsFile() && ! result.contains (f))
            result.add (f);
    };

    for (auto& path : juce::StringArray::fromLines (settings.getValue ("knownProjects")))
        if (path.isNotEmpty())
            add (juce::File (path));

    add (juce::File (settings.getValue ("lastProjectDir")));

    // ダウンロード先のフォルダの中も見る
    for (auto& f : projectsFolder (settings).findChildFiles (juce::File::findDirectories, false))
        add (f);

    return result;
}

juce::File ProjectPicker::projectsFolder (juce::PropertiesFile& settings)
{
    auto path = settings.getValue ("projectsFolder");

    if (path.isNotEmpty())
        return juce::File (path);

    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("ShareDAW");
}

//==============================================================================
juce::String ProjectPicker::statusText (const Entry& e)
{
    const int changes = e.local ? e.local->changedScopes : 0;

    switch (e.status)
    {
        case Status::upToDate:     return "最新"_ju;
        case Status::localChanges: return "この PC に変更あり（"_ju + juce::String (changes) + " 件、まだアップしていません）"_ju;
        case Status::serverNewer:  return "新しい変更があります（開くとダウンロード）"_ju;
        case Status::both:         return "この PC とサーバーの両方に変更があります"_ju;
        case Status::serverOnly:   return "まだダウンロードしていません（開くとダウンロード）"_ju;
        case Status::offline:      return "サーバーを確認できません（この PC のコピーを開けます）"_ju;
    }

    return {};
}

juce::Colour ProjectPicker::statusColour (Status s)
{
    switch (s)
    {
        case Status::upToDate:     return Theme::ok;
        case Status::localChanges: return Theme::warning;
        case Status::serverNewer:  return Theme::accent;
        case Status::both:         return Theme::warning;
        case Status::serverOnly:   return Theme::textDim;
        case Status::offline:      return Theme::textDim;
    }

    return Theme::textDim;
}

//==============================================================================
ProjectPicker::ProjectPicker (SyncManager& s, juce::PropertiesFile& p, juce::File current, Callbacks cb)
    : sync (s), settings (p), currentFolder (std::move (current)), callbacks (std::move (cb))
{
    serverLine.setFont (juce::FontOptions (15.5f));
    addAndMakeVisible (serverLine);
    folderLine.setFont (juce::FontOptions (14.5f));
    folderLine.setColour (juce::Label::textColourId, Theme::textDim);
    addAndMakeVisible (folderLine);

    list.setRowHeight (68);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setMultipleSelectionEnabled (false);
    addAndMakeVisible (list);

    const auto primary = Theme::accent.darker (0.45f);
    createButton.setColour (juce::TextButton::buttonColourId, primary);
    openButton.setColour (juce::TextButton::buttonColourId, primary);
    createButton.setTooltip ("サーバーに曲を作って、ダウンロードして開きます"_ju);
    createButton.onClick = [this] { auto fn = callbacks.createOnServer; close(); if (fn) fn(); };

    refreshButton.onClick = [this] { refresh(); };
    serverButton.onClick = [this] { if (callbacks.serverSettings) callbacks.serverSettings(); };
    folderButton.setTooltip ("ダウンロードした曲を置くフォルダを変える"_ju);
    folderButton.onClick = [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> ("ダウンロード先のフォルダ"_ju, projectsFolder (settings));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this, chooser, safe = juce::Component::SafePointer<ProjectPicker> (this)] (const juce::FileChooser& fc)
        {
            if (safe == nullptr || fc.getResult() == juce::File())
                return;

            settings.setValue ("projectsFolder", fc.getResult().getFullPathName());
            refresh();
        });
    };

    openButton.onClick = [this] { openSelected(); };
    closeButton.onClick = [this] { close(); };

    for (auto* b : { &createButton, &refreshButton, &serverButton, &folderButton, &openButton, &closeButton })
        addAndMakeVisible (b);

    setWantsKeyboardFocus (true);
    setSize (900, 600);
    refresh();
}

ProjectPicker::~ProjectPicker()
{
    *alive = false;
}

void ProjectPicker::refresh()
{
    // この PC にダウンロードしてある曲
    locals.clear();

    for (auto& f : knownFolders (settings))
    {
        auto info = SyncManager::inspectFolder (f);

        if (info.valid && info.linked)
            locals.push_back (info);
    }

    folderLine.setText ("ダウンロード先: "_ju + projectsFolder (settings).getFullPathName(), juce::dontSendNotification);

    serverList.reset();
    serverError = {};

    if (! sync.hasCredentials())
    {
        serverError = "サーバーが設定されていません（「サーバー設定…」で URL とトークンを入れてください）"_ju;
        loadingServer = false;
    }
    else
    {
        loadingServer = true;

        juce::Thread::launch ([this, flag = alive]
        {
            nlohmann::json result;
            auto r = sync.fetchProjects (result);

            juce::MessageManager::callAsync ([this, flag, r, result]
            {
                if (! *flag)
                    return;

                loadingServer = false;

                if (r.failed())
                    serverError = "サーバーに接続できません: "_ju + r.getErrorMessage();
                else
                    serverList = result;

                rebuildEntries();
            });
        });
    }

    rebuildEntries();
}

void ProjectPicker::rebuildEntries()
{
    const auto* sel = selected();
    const auto selectedId = sel != nullptr ? sel->projectId : std::string();

    entries.clear();
    const auto currentServer = normaliseUrl (sync.getServerUrl());

    // この PC のコピー（同じサーバーのもの。同じ曲が複数あれば最近保存したもの）
    auto localFor = [&] (const std::string& id) -> std::optional<SyncManager::LocalInfo>
    {
        std::optional<SyncManager::LocalInfo> best;

        for (auto& l : locals)
            if (l.projectId == id && normaliseUrl (l.serverUrl) == currentServer
                && (! best || l.savedAt > best->savedAt))
                best = l;

        return best;
    };

    if (serverList)
    {
        for (auto& p : *serverList)
        {
            Entry e;
            e.projectId = p.value ("id", std::string());
            e.name = toJuce (p.value ("name", std::string()));
            e.headRevision = p.value ("headRevision", 0);
            e.updatedAt = juce::Time::fromISO8601 (toJuce (p.value ("updatedAt", std::string())));

            if (p.contains ("updatedBy") && p["updatedBy"].is_string())
                e.updatedBy = toJuce (p["updatedBy"].get<std::string>());

            e.local = localFor (e.projectId);

            if (! e.local)
            {
                e.status = Status::serverOnly;
            }
            else
            {
                const bool newer = e.headRevision > e.local->baseRevision;
                const bool changed = e.local->changedScopes > 0;
                e.status = newer && changed ? Status::both : newer ? Status::serverNewer : changed ? Status::localChanges : Status::upToDate;
            }

            entries.push_back (e);
        }
    }
    else if (! loadingServer)
    {
        // オフライン: この PC のコピーだけ開ける
        for (auto& l : locals)
        {
            Entry e;
            e.status = Status::offline;
            e.name = l.name;
            e.projectId = l.projectId;
            e.local = l;
            entries.push_back (e);
        }
    }

    // サーバーの状態の行
    if (loadingServer)
    {
        serverLine.setText ("サーバーを確認しています…"_ju, juce::dontSendNotification);
        serverLine.setColour (juce::Label::textColourId, Theme::textDim);
    }
    else if (serverError.isNotEmpty())
    {
        serverLine.setText (serverError, juce::dontSendNotification);
        serverLine.setColour (juce::Label::textColourId, Theme::warning);
    }
    else
    {
        serverLine.setText (juce::String ((int) entries.size()) + " 曲（"_ju + sync.getServerUrl() + "）"_ju, juce::dontSendNotification);
        serverLine.setColour (juce::Label::textColourId, Theme::textDim);
    }

    list.updateContent();

    // 選択を戻す（なければ、いま開いている曲かいちばん上）
    int select = entries.empty() ? -1 : 0;

    for (size_t i = 0; i < entries.size(); ++i)
    {
        const auto& e = entries[i];

        if ((! selectedId.empty() && e.projectId == selectedId)
            || (selectedId.empty() && e.local && e.local->folder == currentFolder))
            select = (int) i;
    }

    if (select >= 0)
        list.selectRow (select);

    list.repaint();
    updateButtons();
}

const ProjectPicker::Entry* ProjectPicker::selected() const
{
    const int row = list.getSelectedRow();
    return row >= 0 && row < (int) entries.size() ? &entries[(size_t) row] : nullptr;
}

void ProjectPicker::updateButtons()
{
    const auto* e = selected();
    openButton.setEnabled (e != nullptr);
    createButton.setEnabled (sync.hasCredentials());
    openButton.setButtonText (e != nullptr && ! e->local ? "ダウンロードして開く"_ju : "開く"_ju);
}

void ProjectPicker::openSelected()
{
    const auto* sel = selected();

    if (sel == nullptr)
        return;

    const auto e = *sel;
    auto cb = callbacks;
    close();

    if (! e.local)
    {
        if (cb.download)
            cb.download (e.projectId);
    }
    else if (cb.openLocal)
    {
        cb.openLocal (e.local->folder, e.status == Status::serverNewer || e.status == Status::both);
    }
}

void ProjectPicker::renameSelected()
{
    const auto* sel = selected();

    if (sel == nullptr || sel->status == Status::offline)
        return;

    const auto e = *sel;

    Dialogs::askText ("名前を変更"_ju, "新しい曲名"_ju, e.name, [this, flag = alive, e] (const juce::String& entered)
    {
        const auto name = entered.trim();

        if (! *flag || name.isEmpty() || name == e.name)
            return;

        juce::Thread::launch ([this, flag, e, name]
        {
            auto r = sync.runRenameProject (e.projectId, name);

            juce::MessageManager::callAsync ([this, flag, r]
            {
                if (! *flag)
                    return;

                if (r.failed())
                    Dialogs::showError ("名前を変更できませんでした"_ju, r.getErrorMessage());

                refresh();
            });
        });
    });
}

void ProjectPicker::deleteSelected()
{
    const auto* sel = selected();

    if (sel == nullptr || sel->status == Status::offline)
        return;

    const auto e = *sel;

    if (e.local && e.local->folder == currentFolder)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "削除"_ju,
                                                "いま開いている曲は削除できません。別の曲を開いてから削除してください。"_ju, {}, this);
        return;
    }

    auto options = juce::MessageBoxOptions()
                     .withIconType (juce::MessageBoxIconType::WarningIcon)
                     .withTitle ("曲を削除"_ju)
                     .withMessage ("「"_ju + e.name + "」を削除します。\n\n"_ju
                                   + "・サーバーから消え、仲間も開けなくなります（履歴も消えます）。元に戻せません。\n"_ju
                                   + (e.local ? "・この PC のコピーはゴミ箱に移します。"_ju : juce::String()))
                     .withButton ("削除する"_ju)
                     .withButton ("やめる"_ju)
                     .withAssociatedComponent (this);

    juce::AlertWindow::showAsync (options, [this, flag = alive, e] (int result)
    {
        if (! *flag || result != 1)
            return;

        juce::Thread::launch ([this, flag, e]
        {
            const auto folder = e.local ? e.local->folder : juce::File();
            auto r = sync.runDeleteProject (e.projectId, folder);

            juce::MessageManager::callAsync ([this, flag, r, folder]
            {
                if (! *flag)
                    return;

                if (r.failed())
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "削除できませんでした"_ju, r.getErrorMessage(), {}, this);
                else if (folder != juce::File())
                    folder.moveToTrash();

                refresh();
            });
        });
    });
}

void ProjectPicker::showMenu (int row)
{
    if (row < 0 || row >= (int) entries.size())
        return;

    list.selectRow (row);
    const auto& e = entries[(size_t) row];
    juce::PopupMenu m;
    m.addItem (e.local ? "開く"_ju : "ダウンロードして開く"_ju, [this] { openSelected(); });
    m.addSeparator();
    m.addItem ("名前を変更…"_ju, e.status != Status::offline, false, [this] { renameSelected(); });

    if (e.local)
        m.addItem ("フォルダを表示"_ju, [folder = e.local->folder] { folder.revealToUser(); });

    m.addSeparator();
    m.addItem ("削除…"_ju, e.status != Status::offline, false, [this] { deleteSelected(); });
    m.showMenuAsync (juce::PopupMenu::Options());
}

void ProjectPicker::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showMenu (row);
}

bool ProjectPicker::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::F2Key)
    {
        renameSelected();
        return true;
    }

    return false;
}

void ProjectPicker::close()
{
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
        dw->exitModalState (0);
}

//==============================================================================
void ProjectPicker::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool isSelected)
{
    if (row < 0 || row >= (int) entries.size())
        return;

    const auto& e = entries[(size_t) row];
    auto card = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (6.0f, 4.0f);
    Theme::drawGlass (g, card, 12.0f, isSelected ? Theme::accent.withAlpha (0.22f) : juce::Colour());

    auto area = card.reduced (18.0f, 10.0f);
    auto right = area.removeFromRight (230.0f);

    // 曲名と、いま開いている印
    auto line1 = area.removeFromTop (26.0f);
    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText (e.name.isEmpty() ? "（名前なし）"_ju : e.name, line1, juce::Justification::centredLeft, true);

    // 状況（色の丸と文）
    auto line2 = area.removeFromTop (22.0f);
    Theme::drawStatusDot (g, line2.removeFromLeft (12.0f).withSizeKeepingCentre (9.0f, 9.0f), statusColour (e.status));
    line2.removeFromLeft (6.0f);
    g.setColour (e.status == Status::serverOnly || e.status == Status::offline ? Theme::textDim : Theme::text);
    g.setFont (juce::FontOptions (16.0f));
    juce::String text = statusText (e);

    if (e.local && e.local->folder == currentFolder)
        text = "いま開いている曲・"_ju + text;

    g.drawText (text, line2, juce::Justification::centredLeft, true);

    // 右: 最後にアップした人と日時
    if (e.updatedAt.toMilliseconds() > 0)
    {
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (15.0f));
        g.drawText (relativeTime (e.updatedAt) + (e.updatedBy.isNotEmpty() ? "　"_ju + e.updatedBy : juce::String()),
                    right.removeFromTop (26.0f), juce::Justification::centredRight, true);
        g.drawText (e.local ? "この PC にダウンロード済み"_ju : juce::String(), right.removeFromTop (22.0f), juce::Justification::centredRight, true);
    }
}

void ProjectPicker::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);
    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("楽曲"_ju, 22, 14, 200, 36, juce::Justification::centredLeft);

    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (14.5f));
    g.drawText ("右クリック（または Delete / F2）で、名前の変更・削除"_ju, getLocalBounds().reduced (22, 0).withTop (getHeight() - 96).withHeight (20),
                juce::Justification::centredLeft);
}

void ProjectPicker::resized()
{
    auto area = getLocalBounds().reduced (16, 12);

    auto top = area.removeFromTop (40);
    top.removeFromLeft (120);
    serverButton.setBounds (top.removeFromRight (130).reduced (0, 4));
    top.removeFromRight (8);
    refreshButton.setBounds (top.removeFromRight (80).reduced (0, 4));
    top.removeFromRight (8);
    createButton.setBounds (top.removeFromRight (140).reduced (0, 2));

    serverLine.setBounds (area.removeFromTop (24));
    area.removeFromTop (6);

    auto bottom = area.removeFromBottom (40).withTrimmedTop (6);
    openButton.setBounds (bottom.removeFromRight (190));
    bottom.removeFromRight (8);
    closeButton.setBounds (bottom.removeFromRight (100));
    folderButton.setBounds (bottom.removeFromRight (70).reduced (0, 3));
    folderLine.setBounds (bottom);

    area.removeFromBottom (30);
    list.setBounds (area);
}
