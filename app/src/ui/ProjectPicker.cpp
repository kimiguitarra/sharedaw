#include "ProjectPicker.h"

#include "Theme.h"

namespace
{
    enum Column { nameColumn = 1, statusColumn, serverColumn, localColumn };

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

void ProjectPicker::forget (juce::PropertiesFile& settings, const juce::File& folder)
{
    auto list = juce::StringArray::fromLines (settings.getValue ("knownProjects"));
    list.removeString (folder.getFullPathName());
    settings.setValue ("knownProjects", list.joinIntoString ("\n"));

    // ダウンロード先のフォルダにあるものは、次に一覧を作るとまた出てくるので、外した印を付ける
    auto hidden = juce::StringArray::fromLines (settings.getValue ("hiddenProjects"));
    hidden.addIfNotAlreadyThere (folder.getFullPathName());
    settings.setValue ("hiddenProjects", hidden.joinIntoString ("\n"));
}

juce::Array<juce::File> ProjectPicker::knownFolders (juce::PropertiesFile& settings)
{
    juce::Array<juce::File> result;
    const auto hidden = juce::StringArray::fromLines (settings.getValue ("hiddenProjects"));

    auto add = [&] (const juce::File& f)
    {
        if (f.getChildFile ("project.json").existsAsFile() && ! result.contains (f) && ! hidden.contains (f.getFullPathName()))
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
    const int base = e.local ? e.local->baseRevision : 0;

    switch (e.status)
    {
        case Status::upToDate:     return "最新"_ju;
        case Status::localChanges: return "この PC に未送信の変更 "_ju + juce::String (changes) + " 件（送信 = push してください）"_ju;
        case Status::serverNewer:  return "サーバーに新しい版（rev "_ju + juce::String (base) + " → "_ju + juce::String (e.headRevision) + "。開くと取り込みます）"_ju;
        case Status::both:         return "両方に変更あり（取り込んでから送信してください）"_ju;
        case Status::serverOnly:   return "サーバーだけ（開くとダウンロードします）"_ju;
        case Status::localOnly:    return "この PC だけ（サーバー未登録）"_ju;
        case Status::unchecked:    return changes > 0 ? "サーバー未確認（この PC に未送信の変更 "_ju + juce::String (changes) + " 件）"_ju
                                                      : "サーバー未確認"_ju;
        case Status::otherServer:  return "別のサーバーの曲"_ju;
        case Status::notOnServer:  return "サーバーに見当たりません（削除されたか、参加していません）"_ju;
    }

    return {};
}

juce::Colour ProjectPicker::statusColour (Status s)
{
    switch (s)
    {
        case Status::upToDate:     return juce::Colour (0xff66bb6a);
        case Status::localChanges: return juce::Colour (0xffffb74d);
        case Status::serverNewer:  return juce::Colour (0xff4fc3f7);
        case Status::both:         return juce::Colour (0xffef5350);
        case Status::serverOnly:   return juce::Colour (0xffba68c8);
        case Status::localOnly:
        case Status::unchecked:
        case Status::otherServer:
        case Status::notOnServer:  return Theme::textDim;
    }

    return Theme::textDim;
}

//==============================================================================
ProjectPicker::ProjectPicker (SyncManager& s, juce::PropertiesFile& p, juce::File current, Callbacks cb)
    : sync (s), settings (p), currentFolder (std::move (current)), callbacks (std::move (cb))
{
    title.setText ("楽曲を選ぶ"_ju, juce::dontSendNotification);
    title.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    addAndMakeVisible (title);

    serverLine.setFont (juce::FontOptions (12.5f));
    addAndMakeVisible (serverLine);
    folderLine.setFont (juce::FontOptions (12.0f));
    folderLine.setColour (juce::Label::textColourId, Theme::textDim);
    addAndMakeVisible (folderLine);

    auto& header = table.getHeader();
    header.addColumn ("曲名"_ju, nameColumn, 220, 120, 400, juce::TableHeaderComponent::notSortable);
    header.addColumn ("状況"_ju, statusColumn, 330, 160, 600, juce::TableHeaderComponent::notSortable);
    header.addColumn ("サーバー"_ju, serverColumn, 190, 120, 400, juce::TableHeaderComponent::notSortable);
    header.addColumn ("この PC"_ju, localColumn, 260, 120, 600, juce::TableHeaderComponent::notSortable);
    header.setColour (juce::TableHeaderComponent::backgroundColourId, Theme::panelLight);
    header.setColour (juce::TableHeaderComponent::textColourId, Theme::text);
    header.setColour (juce::TableHeaderComponent::outlineColourId, Theme::background);
    table.setRowHeight (46);
    table.setColour (juce::ListBox::backgroundColourId, Theme::background);
    table.setMultipleSelectionEnabled (false);
    addAndMakeVisible (table);

    refreshButton.onClick = [this] { refresh(); };
    serverButton.onClick = [this] { if (callbacks.serverSettings) callbacks.serverSettings(); };
    folderButton.setTooltip ("サーバーからダウンロードした曲を置くフォルダを変える"_ju);
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

    newButton.onClick = [this] { auto fn = callbacks.newProject; close(); if (fn) fn(); };
    otherButton.onClick = [this] { auto fn = callbacks.openOther; close(); if (fn) fn(); };
    forgetButton.setTooltip ("この PC の一覧から外す（フォルダは消しません）"_ju);
    forgetButton.onClick = [this]
    {
        const int row = table.getSelectedRow();

        if (row >= 0 && row < (int) entries.size() && entries[(size_t) row].local)
        {
            forget (settings, entries[(size_t) row].local->folder);
            refresh();
        }
    };

    openButton.onClick = [this] { openSelected(); };
    closeButton.onClick = [this] { close(); };

    for (auto* b : { &refreshButton, &serverButton, &folderButton, &newButton, &otherButton, &forgetButton, &openButton, &closeButton })
        addAndMakeVisible (b);

    setSize (1040, 560);
    refresh();
}

ProjectPicker::~ProjectPicker()
{
    *alive = false;
}

void ProjectPicker::refresh()
{
    // この PC の曲（すぐに読める）
    locals.clear();

    for (auto& f : knownFolders (settings))
    {
        auto info = SyncManager::inspectFolder (f);

        if (info.valid)
            locals.push_back (info);
    }

    folderLine.setText ("ダウンロード先: "_ju + projectsFolder (settings).getFullPathName(), juce::dontSendNotification);

    // サーバー（バックグラウンドで取得）
    serverList.reset();
    serverError = {};

    if (! sync.hasCredentials())
    {
        serverError = "サーバー未設定（「サーバー設定…」で URL とトークンを入れると、サーバーの曲が出ます）"_ju;
        loadingServer = false;
    }
    else
    {
        loadingServer = true;

        juce::Thread::launch ([this, flag = alive]
        {
            nlohmann::json list;
            auto r = sync.fetchProjects (list);

            juce::MessageManager::callAsync ([this, flag, r, list]
            {
                if (! *flag)
                    return;

                loadingServer = false;

                if (r.failed())
                    serverError = "サーバーに接続できません: "_ju + r.getErrorMessage();
                else
                    serverList = list;

                rebuildEntries();
            });
        });
    }

    rebuildEntries();
}

void ProjectPicker::rebuildEntries()
{
    // 選択を保つ
    std::string selectedKey;

    if (const int row = table.getSelectedRow(); row >= 0 && row < (int) entries.size())
        selectedKey = entries[(size_t) row].local ? entries[(size_t) row].local->folder.getFullPathName().toStdString()
                                                  : entries[(size_t) row].projectId;

    entries.clear();
    const auto currentServer = normaliseUrl (sync.getServerUrl());

    struct ServerItem { juce::String name; int head = 0; juce::Time updatedAt; juce::String updatedBy; };
    std::map<std::string, ServerItem> server;
    std::vector<std::string> serverOrder;

    if (serverList)
    {
        for (auto& p : *serverList)
        {
            ServerItem item;
            item.name = toJuce (p.value ("name", std::string()));
            item.head = p.value ("headRevision", 0);
            item.updatedAt = juce::Time::fromISO8601 (toJuce (p.value ("updatedAt", std::string())));

            if (p.contains ("updatedBy") && p["updatedBy"].is_string())
                item.updatedBy = toJuce (p["updatedBy"].get<std::string>());

            const auto id = p.value ("id", std::string());
            server[id] = item;
            serverOrder.push_back (id);
        }
    }

    std::set<std::string> localIds;

    for (auto& l : locals)
    {
        Entry e;
        e.local = l;
        e.name = l.name;
        e.projectId = l.projectId;

        if (! l.linked)
        {
            e.status = Status::localOnly;
        }
        else if (normaliseUrl (l.serverUrl) != currentServer)
        {
            e.status = Status::otherServer;
        }
        else if (! serverList)
        {
            e.status = Status::unchecked;
        }
        else if (auto it = server.find (l.projectId); it == server.end())
        {
            e.status = Status::notOnServer;
        }
        else
        {
            e.onServer = true;
            e.headRevision = it->second.head;
            e.updatedAt = it->second.updatedAt;
            e.updatedBy = it->second.updatedBy;
            const bool newer = it->second.head > l.baseRevision;
            const bool changed = l.changedScopes > 0;
            e.status = newer && changed ? Status::both : newer ? Status::serverNewer : changed ? Status::localChanges : Status::upToDate;
            localIds.insert (l.projectId);
        }

        entries.push_back (e);
    }

    // サーバーだけにある曲
    for (auto& id : serverOrder)
    {
        if (localIds.count (id) > 0)
            continue;

        bool hasLocalCopy = false;

        for (auto& l : locals)
            hasLocalCopy = hasLocalCopy || (l.projectId == id && l.linked);

        if (hasLocalCopy)
            continue;

        const auto& item = server[id];
        Entry e;
        e.status = Status::serverOnly;
        e.name = item.name;
        e.projectId = id;
        e.onServer = true;
        e.headRevision = item.head;
        e.updatedAt = item.updatedAt;
        e.updatedBy = item.updatedBy;
        entries.push_back (e);
    }

    // 並び順: 手を付けるべきもの（変更あり・新しい版）→ 最新 → サーバーだけ → その他。同じなら新しい順
    auto rank = [] (Status s)
    {
        switch (s)
        {
            case Status::both:          return 0;
            case Status::serverNewer:   return 1;
            case Status::localChanges:  return 2;
            case Status::upToDate:      return 3;
            case Status::serverOnly:    return 4;
            case Status::unchecked:     return 5;
            case Status::localOnly:     return 6;
            case Status::otherServer:
            case Status::notOnServer:   return 7;
        }

        return 8;
    };

    std::stable_sort (entries.begin(), entries.end(), [&] (const Entry& a, const Entry& b)
    {
        if (rank (a.status) != rank (b.status))
            return rank (a.status) < rank (b.status);

        auto time = [] (const Entry& e) { return std::max (e.updatedAt.toMilliseconds(), e.local ? e.local->savedAt.toMilliseconds() : 0); };
        return time (a) > time (b);
    });

    // サーバーの状態の行
    if (loadingServer)
    {
        serverLine.setText ("サーバーを確認しています…（"_ju + sync.getServerUrl() + "）"_ju, juce::dontSendNotification);
        serverLine.setColour (juce::Label::textColourId, Theme::textDim);
    }
    else if (serverError.isNotEmpty())
    {
        serverLine.setText (serverError, juce::dontSendNotification);
        serverLine.setColour (juce::Label::textColourId, Theme::warning);
    }
    else
    {
        serverLine.setText ("サーバー: "_ju + sync.getServerUrl() + "（参加している曲 "_ju + juce::String ((int) serverOrder.size()) + " 曲）"_ju,
                            juce::dontSendNotification);
        serverLine.setColour (juce::Label::textColourId, juce::Colour (0xff66bb6a));
    }

    table.updateContent();

    // 選択を戻す（なければ、いま開いている曲かいちばん上）
    int select = entries.empty() ? -1 : 0;

    for (size_t i = 0; i < entries.size(); ++i)
    {
        const auto& e = entries[i];
        const auto key = e.local ? e.local->folder.getFullPathName().toStdString() : e.projectId;

        if ((! selectedKey.empty() && key == selectedKey)
            || (selectedKey.empty() && e.local && e.local->folder == currentFolder))
            select = (int) i;
    }

    if (select >= 0)
        table.selectRow (select);

    table.repaint();
    updateButtons();
}

void ProjectPicker::updateButtons()
{
    const int row = table.getSelectedRow();
    const auto* e = row >= 0 && row < (int) entries.size() ? &entries[(size_t) row] : nullptr;

    openButton.setEnabled (e != nullptr);
    forgetButton.setEnabled (e != nullptr && e->local.has_value());

    if (e == nullptr)
        openButton.setButtonText ("開く"_ju);
    else if (e->status == Status::serverOnly)
        openButton.setButtonText ("ダウンロードして開く"_ju);
    else if (e->status == Status::serverNewer || e->status == Status::both)
        openButton.setButtonText ("開いて取り込む"_ju);
    else
        openButton.setButtonText ("開く"_ju);
}

void ProjectPicker::openSelected()
{
    const int row = table.getSelectedRow();

    if (row < 0 || row >= (int) entries.size())
        return;

    const auto e = entries[(size_t) row];
    auto cb = callbacks;
    close();

    if (e.status == Status::serverOnly)
    {
        if (cb.download)
            cb.download (e.projectId);
    }
    else if (e.local && cb.openLocal)
    {
        cb.openLocal (e.local->folder, e.status == Status::serverNewer || e.status == Status::both);
    }
}

void ProjectPicker::close()
{
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
        dw->exitModalState (0);
}

//==============================================================================
void ProjectPicker::paintRowBackground (juce::Graphics& g, int row, int, int height, bool selected)
{
    g.fillAll (selected ? Theme::accent.withAlpha (0.25f) : (row % 2 ? Theme::laneAlt : Theme::lane));

    if (row >= 0 && row < (int) entries.size())
    {
        g.setColour (statusColour (entries[(size_t) row].status));
        g.fillRect (0, 0, 4, height);
    }
}

void ProjectPicker::paintCell (juce::Graphics& g, int row, int column, int width, int height, bool)
{
    if (row < 0 || row >= (int) entries.size())
        return;

    const auto& e = entries[(size_t) row];
    auto area = juce::Rectangle<int> (0, 0, width, height).reduced (8, 4);
    auto top = area.removeFromTop (area.getHeight() / 2);

    auto line = [&] (juce::Rectangle<int> r, const juce::String& text, juce::Colour colour, float size, bool bold)
    {
        g.setColour (colour);
        g.setFont (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
        g.drawText (text, r, juce::Justification::centredLeft, true);
    };

    switch (column)
    {
        case nameColumn:
        {
            const bool isCurrent = e.local && e.local->folder == currentFolder;
            line (top, e.name.isEmpty() ? "（名前なし）"_ju : e.name, Theme::text, 15.0f, true);
            line (area, isCurrent ? "いま開いている曲"_ju : juce::String(), Theme::accent, 11.0f, false);
            break;
        }

        case statusColumn:
        {
            // 色の付いた丸と状況
            g.setColour (statusColour (e.status));
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f, 10.0f, 10.0f).withY ((float) top.getCentreY() - 5.0f));
            line (top.withTrimmedLeft (16), statusText (e), statusColour (e.status).brighter (0.2f), 13.0f, true);

            if (e.local && e.local->changedScopes > 0)
                line (area.withTrimmedLeft (16), "変更: "_ju + e.local->changedNames.joinIntoString ("、"_ju)
                                                 + (e.local->changedScopes > e.local->changedNames.size() ? " ほか"_ju : juce::String()),
                      Theme::textDim, 11.5f, false);
            break;
        }

        case serverColumn:
        {
            if (! e.onServer)
            {
                line (top, e.local && e.local->linked ? "—"_ju : "未登録"_ju, Theme::textDim, 12.5f, false);
                break;
            }

            line (top, "rev "_ju + juce::String (e.headRevision), Theme::text, 13.0f, true);
            line (area, relativeTime (e.updatedAt) + (e.updatedBy.isNotEmpty() ? "　"_ju + e.updatedBy : juce::String()),
                  Theme::textDim, 11.5f, false);
            break;
        }

        case localColumn:
        {
            if (! e.local)
            {
                line (top, "この PC にはありません"_ju, Theme::textDim, 12.5f, false);
                break;
            }

            line (top, (e.local->linked ? "rev "_ju + juce::String (e.local->baseRevision) + "　"_ju : juce::String())
                          + "保存 "_ju + relativeTime (e.local->savedAt),
                  Theme::text, 13.0f, e.local->linked);
            line (area, e.local->folder.getFullPathName(), Theme::textDim, 11.0f, false);
            break;
        }

        default:
            break;
    }
}

//==============================================================================
void ProjectPicker::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
}

void ProjectPicker::resized()
{
    auto area = getLocalBounds().reduced (14);

    auto top = area.removeFromTop (30);
    title.setBounds (top.removeFromLeft (200));
    serverButton.setBounds (top.removeFromRight (120).reduced (0, 2));
    top.removeFromRight (6);
    refreshButton.setBounds (top.removeFromRight (80).reduced (0, 2));

    serverLine.setBounds (area.removeFromTop (22));
    area.removeFromTop (6);

    auto bottom = area.removeFromBottom (32);
    openButton.setBounds (bottom.removeFromRight (180));
    bottom.removeFromRight (6);
    closeButton.setBounds (bottom.removeFromRight (100));
    newButton.setBounds (bottom.removeFromLeft (150));
    bottom.removeFromLeft (6);
    otherButton.setBounds (bottom.removeFromLeft (150));
    bottom.removeFromLeft (6);
    forgetButton.setBounds (bottom.removeFromLeft (130));

    area.removeFromBottom (6);
    auto folderRow = area.removeFromBottom (24);
    folderButton.setBounds (folderRow.removeFromRight (70).reduced (0, 1));
    folderLine.setBounds (folderRow);
    area.removeFromBottom (4);

    table.setBounds (area);
}
