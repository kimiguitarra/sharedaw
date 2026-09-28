#include "ProjectPicker.h"

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
        case Status::upToDate:     return "サーバーとこの PC が同じです"_ju;
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
        case Status::upToDate:     return Theme::green;
        case Status::localChanges: return Theme::orange;
        case Status::serverNewer:  return Theme::accent;
        case Status::both:         return Theme::red;
        case Status::serverOnly:   return Theme::purple;
        case Status::localOnly:    return Theme::pink;
        case Status::unchecked:
        case Status::otherServer:
        case Status::notOnServer:  return Theme::textDim;
    }

    return Theme::textDim;
}

juce::String ProjectPicker::statusShort (const Entry& e)
{
    const int changes = e.local ? e.local->changedScopes : 0;
    const auto up = juce::String::fromUTF8 ("\xE2\x86\x91 "), down = juce::String::fromUTF8 ("\xE2\x86\x93 ");

    switch (e.status)
    {
        case Status::upToDate:     return "最新"_ju;
        case Status::localChanges: return up + juce::String (changes) + " 件 未送信"_ju;
        case Status::serverNewer:  return down + "新しい版あり"_ju;
        case Status::both:         return down + up + "両方に変更"_ju;
        case Status::serverOnly:   return "サーバーだけ"_ju;
        case Status::localOnly:    return "この PC だけ"_ju;
        case Status::unchecked:    return "未確認"_ju;
        case Status::otherServer:  return "別のサーバー"_ju;
        case Status::notOnServer:  return "見当たらない"_ju;
    }

    return {};
}

//==============================================================================
ProjectPicker::ProjectPicker (SyncManager& s, juce::PropertiesFile& p, juce::File current, Callbacks cb)
    : sync (s), settings (p), currentFolder (std::move (current)), callbacks (std::move (cb))
{
    serverLine.setFont (juce::FontOptions (12.5f));
    addAndMakeVisible (serverLine);
    folderLine.setFont (juce::FontOptions (12.0f));
    folderLine.setColour (juce::Label::textColourId, Theme::textDim);
    addAndMakeVisible (folderLine);

    table.setRowHeight (78);
    table.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
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

    createButton.setColour (juce::TextButton::buttonColourId, Theme::pink);
    createButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    createButton.setTooltip ("サーバーに曲を作り、この PC（ダウンロード先のフォルダ）にも置いて開きます"_ju);
    createButton.onClick = [this] { auto fn = callbacks.createOnServer; close(); if (fn) fn(); };
    openButton.setColour (juce::TextButton::buttonColourId, Theme::accent);
    openButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff15162a));

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

    for (auto* b : { &refreshButton, &serverButton, &folderButton, &createButton, &newButton, &otherButton, &forgetButton, &openButton, &closeButton })
        addAndMakeVisible (b);

    setSize (980, 640);
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
        serverLine.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.8f));
    }
    else if (serverError.isNotEmpty())
    {
        serverLine.setText (serverError, juce::dontSendNotification);
        serverLine.setColour (juce::Label::textColourId, Theme::selection);
    }
    else
    {
        serverLine.setText ("サーバー: "_ju + sync.getServerUrl() + "（参加している曲 "_ju + juce::String ((int) serverOrder.size()) + " 曲）"_ju,
                            juce::dontSendNotification);
        serverLine.setColour (juce::Label::textColourId, juce::Colours::white);
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
    else if (e->status == Status::localOnly && sync.hasCredentials())
        openButton.setButtonText ("開いてサーバーにアップ"_ju);
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
    else if (e.status == Status::localOnly && e.local && sync.hasCredentials() && cb.openAndUpload)
    {
        cb.openAndUpload (e.local->folder);
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
void ProjectPicker::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= (int) entries.size())
        return;

    const auto& e = entries[(size_t) row];
    const auto colour = statusColour (e.status);
    auto card = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (4.0f, 4.0f);

    g.setColour (selected ? Theme::panelLight : Theme::panel);
    g.fillRoundedRectangle (card, 12.0f);

    if (selected)
    {
        g.setColour (Theme::accent);
        g.drawRoundedRectangle (card.reduced (1.0f), 12.0f, 2.0f);
    }

    // 左の色の帯
    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addRoundedRectangle (card, 12.0f);
        g.reduceClipRegion (clip);
        g.setColour (colour);
        g.fillRect (card.withWidth (6.0f));
    }

    auto area = card.reduced (16.0f, 8.0f).withTrimmedLeft (4.0f);

    // 曲の頭文字の丸（Google Drive のアイコンのように、曲ごとに色を変える）
    auto icon = area.removeFromLeft (46.0f).withSizeKeepingCentre (42.0f, 42.0f);
    const auto iconColour = Theme::personColour (e.name + toJuce (e.projectId));
    g.setGradientFill (juce::ColourGradient (iconColour.brighter (0.2f), icon.getX(), icon.getY(), iconColour.darker (0.2f),
                                             icon.getRight(), icon.getBottom(), false));
    g.fillRoundedRectangle (icon, 12.0f);
    g.setColour (juce::Colour (0xff15162a));
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText (e.name.isEmpty() ? juce::String ("?") : e.name.substring (0, 1).toUpperCase(), icon, juce::Justification::centred);

    // どこにあるか（雲 = サーバー、PC = この PC）の小さな印
    auto where = [&] (juce::Rectangle<float> r, const juce::String& text, bool on)
    {
        g.setColour (on ? Theme::text.withAlpha (0.9f) : Theme::textDim.withAlpha (0.35f));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (text, r, juce::Justification::centred);
    };

    area.removeFromLeft (14.0f);
    auto whereArea = area.removeFromRight (120.0f);
    where (whereArea.removeFromTop (28.0f).removeFromLeft (56.0f).reduced (0.0f, 3.0f), "サーバー"_ju, e.onServer);
    where (juce::Rectangle<float> (whereArea.getX() + 62.0f, card.getY() + 11.0f, 56.0f, 22.0f), "この PC"_ju, e.local.has_value());

    // 1 行目: 曲名と状況のバッジ
    auto line1 = area.removeFromTop (28.0f);
    const auto name = e.name.isEmpty() ? "（名前なし）"_ju : e.name;
    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    const float nameWidth = juce::jmin (line1.getWidth() * 0.55f,
                                        juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)), name) + 4.0f);
    g.drawText (name, line1.removeFromLeft (nameWidth), juce::Justification::centredLeft, true);
    line1.removeFromLeft (10.0f);

    const auto pill = statusShort (e);
    const float pillWidth = juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)), pill) + 22.0f;
    Theme::drawPill (g, line1.removeFromLeft (pillWidth).withSizeKeepingCentre (pillWidth, 20.0f), colour, pill, 12.0f);

    if (e.local && e.local->folder == currentFolder)
    {
        line1.removeFromLeft (6.0f);
        Theme::drawPill (g, line1.removeFromLeft (92.0f).withSizeKeepingCentre (92.0f, 20.0f), Theme::selection, "いま開いている"_ju, 11.0f);
    }

    // 2 行目: 状況の説明（変更したトラック名など）
    auto line2 = area.removeFromTop (18.0f);
    juce::String detail = statusText (e);

    if (e.local && e.local->changedScopes > 0)
        detail << "　変更: "_ju << e.local->changedNames.joinIntoString ("、"_ju)
               << (e.local->changedScopes > e.local->changedNames.size() ? " ほか"_ju : juce::String());

    g.setColour (colour.interpolatedWith (Theme::text, 0.35f));
    g.setFont (juce::FontOptions (12.5f));
    g.drawText (detail, line2, juce::Justification::centredLeft, true);

    // 3 行目: サーバーとこの PC のリビジョン・日時
    juce::StringArray facts;

    if (e.onServer)
        facts.add ("サーバー rev "_ju + juce::String (e.headRevision) + "・"_ju + relativeTime (e.updatedAt)
                   + (e.updatedBy.isNotEmpty() ? " "_ju + e.updatedBy : juce::String()));

    if (e.local)
        facts.add ("この PC "_ju + (e.local->linked ? "rev "_ju + juce::String (e.local->baseRevision) + "・"_ju : juce::String())
                   + "保存 "_ju + relativeTime (e.local->savedAt));

    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (11.5f));
    g.drawText (facts.joinIntoString ("　　"_ju), area.removeFromTop (18.0f), juce::Justification::centredLeft, true);
}

//==============================================================================
void ProjectPicker::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    // 上の帯（ピンク → 紫のグラデーションと水玉）
    auto banner = getLocalBounds().removeFromTop (96).toFloat().reduced (12.0f, 12.0f).withTrimmedBottom (-4.0f);
    g.setGradientFill (juce::ColourGradient (Theme::pink, banner.getX(), banner.getY(), Theme::purple, banner.getRight(), banner.getBottom(), false));
    g.fillRoundedRectangle (banner, 16.0f);

    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addRoundedRectangle (banner, 16.0f);
        g.reduceClipRegion (clip);
        g.setColour (juce::Colours::white.withAlpha (0.1f));
        g.fillEllipse (banner.getRight() - 150.0f, banner.getY() - 40.0f, 120.0f, 120.0f);
        g.fillEllipse (banner.getRight() - 260.0f, banner.getBottom() - 30.0f, 60.0f, 60.0f);
        g.fillEllipse (banner.getCentreX(), banner.getY() - 20.0f, 36.0f, 36.0f);
    }

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("楽曲を選ぶ"_ju, banner.reduced (20.0f, 0.0f).withHeight (46.0f).translated (0.0f, 8.0f), juce::Justification::centredLeft);
}

void ProjectPicker::resized()
{
    auto area = getLocalBounds();

    auto banner = area.removeFromTop (96).reduced (12, 12).withTrimmedBottom (-4).reduced (20, 0);
    auto bannerTop = banner.removeFromTop (46).translated (0, 8);
    serverButton.setBounds (bannerTop.removeFromRight (120).withSizeKeepingCentre (120, 28));
    bannerTop.removeFromRight (6);
    refreshButton.setBounds (bannerTop.removeFromRight (70).withSizeKeepingCentre (70, 28));
    serverLine.setBounds (banner.translated (-4, 2).withHeight (22));

    area.reduce (14, 4);

    auto actions = area.removeFromTop (38);
    createButton.setBounds (actions.removeFromLeft (240).reduced (0, 2));
    actions.removeFromLeft (8);
    newButton.setBounds (actions.removeFromLeft (170).reduced (0, 4));
    actions.removeFromLeft (6);
    otherButton.setBounds (actions.removeFromLeft (150).reduced (0, 4));
    area.removeFromTop (6);

    auto bottom = area.removeFromBottom (44).reduced (0, 6);
    openButton.setBounds (bottom.removeFromRight (200));
    bottom.removeFromRight (8);
    closeButton.setBounds (bottom.removeFromRight (100));
    forgetButton.setBounds (bottom.removeFromLeft (120));
    bottom.removeFromLeft (10);
    folderButton.setBounds (bottom.removeFromRight (70).reduced (0, 2));
    folderLine.setBounds (bottom);

    table.setBounds (area);
}
