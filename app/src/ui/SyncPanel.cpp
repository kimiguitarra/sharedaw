#include "SyncPanel.h"

#include "Theme.h"

namespace
{
    const juce::Colour primaryButton = Theme::accent.darker (0.45f);
    const juce::Colour downloadColour = Theme::accent, uploadColour = Theme::ok;

    constexpr int columnWidth = 64;    // ダウンロード・アップロードの欄
    constexpr int rowHeight = 34;

    juce::Colour scopeColour (const collab::Project& p, const collab::ScopeSyncState& st)
    {
        switch (st.kind)
        {
            case collab::ScopeKind::tempo:  return Theme::tempo;
            case collab::ScopeKind::meter:  return Theme::meter;
            case collab::ScopeKind::chord:  return Theme::selection;
            case collab::ScopeKind::marker: return Theme::warning;
            case collab::ScopeKind::key:    return Theme::ok;
            case collab::ScopeKind::master: return Theme::textDim;
            case collab::ScopeKind::track:
            default:
                if (auto* t = p.findTrack (st.id))
                    return Theme::parseColour (t->color);
                return Theme::textDim;
        }
    }

    void drawCheckbox (juce::Graphics& g, juce::Rectangle<float> box, bool enabled, bool checked, juce::Colour colour)
    {
        if (! enabled)
        {
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.drawRoundedRectangle (box.reduced (0.5f), 4.0f, 1.0f);
            return;
        }

        g.setColour (checked ? colour : juce::Colours::white.withAlpha (0.06f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (checked ? colour.brighter (0.3f) : juce::Colours::white.withAlpha (0.45f));
        g.drawRoundedRectangle (box.reduced (0.5f), 4.0f, 1.2f);

        if (checked)
        {
            juce::Path tick;
            tick.startNewSubPath (box.getX() + box.getWidth() * 0.22f, box.getCentreY());
            tick.lineTo (box.getX() + box.getWidth() * 0.42f, box.getBottom() - box.getHeight() * 0.25f);
            tick.lineTo (box.getRight() - box.getWidth() * 0.2f, box.getY() + box.getHeight() * 0.25f);
            g.setColour (Theme::background);
            g.strokePath (tick, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    juce::String arrow (bool down)   { return juce::String::fromUTF8 (down ? "\xE2\x86\x93" : "\xE2\x86\x91"); }   // ↓ ↑
}

//==============================================================================
/** 表の行（名前 | ダウンロード | アップロード）。行をクリックで中身を開く、チェックの欄をクリックで切り替え。 */
class SyncPanel::List  : public juce::Component
{
public:
    explicit List (SyncPanel& o) : owner (o) {}

    struct Detail
    {
        juce::Rectangle<int> r;
        collab::Change change;
        bool header = false;
        juce::String text;
    };

    struct Row
    {
        collab::ScopeSyncState st;
        juce::Colour colour;
        juce::Rectangle<int> r, downloadCell, uploadCell;
        Checks checks;
        std::vector<Detail> details;
    };

    std::vector<Row> rows;

    void rebuild (const std::vector<collab::ScopeSyncState>& states, const collab::ProjectDiff& localDiff,
                  const collab::ProjectDiff* serverDiff, const collab::Project& local)
    {
        rows.clear();
        const int width = juce::jmax (200, getWidth());
        int y = 0;

        for (auto& st : states)
        {
            Row row;
            row.st = st;
            row.colour = scopeColour (local, st);
            row.checks = owner.checksFor (st);
            row.r = { 0, y, width, rowHeight };
            row.uploadCell = row.r.withLeft (width - columnWidth);
            row.downloadCell = row.r.withLeft (width - 2 * columnWidth).withWidth (columnWidth);
            y += rowHeight;

            // 開いている行: 自分の変更とサーバーの変更の中身
            if (owner.expandedId == st.id)
            {
                auto addDetails = [&] (const juce::String& title, const std::vector<collab::Change>& changes)
                {
                    if (changes.empty())
                        return;

                    row.details.push_back ({ { 26, y, width - 34, 24 }, {}, true, title });
                    y += 24;

                    for (auto& c : changes)
                    {
                        row.details.push_back ({ { 34, y, width - 42, 24 }, c, false, toJuce (c.summary) });
                        y += 24;
                    }
                };

                addDetails ("この PC の変更"_ju, localDiff.forScope (st.id));

                if (serverDiff != nullptr)
                    addDetails ("サーバーの変更"_ju, serverDiff->forScope (st.id));

                if (row.details.empty())
                {
                    row.details.push_back ({ { 26, y, width - 34, 24 }, {}, true, "変更はありません"_ju });
                    y += 24;
                }

                y += 6;
            }

            rows.push_back (std::move (row));
        }

        setSize (width, juce::jmax (y + 4, 40));
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        for (auto& row : rows)
        {
            const auto& st = row.st;
            const bool expanded = owner.expandedId == st.id;

            // 行の色: 競合は橙、サーバーで新しくなったものは青をうっすら
            if (st.conflict)
                g.setColour (Theme::warning.withAlpha (0.16f));
            else if (row.checks.canDownload)
                g.setColour (downloadColour.withAlpha (0.13f));
            else if (expanded)
                g.setColour (juce::Colours::white.withAlpha (0.05f));
            else
                g.setColour (juce::Colours::transparentBlack);

            g.fillRect (row.r);
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.drawHorizontalLine (row.r.getBottom() - 1, 0.0f, (float) getWidth());

            // 名前（競合・新着は名前の後ろに）
            auto name = row.r.withRight (row.downloadCell.getX()).reduced (8, 0).toFloat();
            Theme::drawStatusDot (g, name.removeFromLeft (10.0f).withSizeKeepingCentre (10.0f, 10.0f), row.colour);
            name.removeFromLeft (8.0f);

            juce::String tag;
            juce::Colour tagColour;

            if (st.conflict)                        { tag = "競合"_ju; tagColour = Theme::warning; }
            else if (row.checks.canDownload)        { tag = "新着"_ju; tagColour = downloadColour; }

            if (tag.isNotEmpty())
            {
                g.setFont (juce::FontOptions (14.5f, juce::Font::bold));
                const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), tag) + 6.0f;
                g.setColour (tagColour);
                g.drawText (tag, name.removeFromRight (w), juce::Justification::centredRight);
            }

            const bool quiet = ! st.mine && ! st.theirs;
            g.setColour (quiet ? Theme::textDim : Theme::text);
            g.setFont (juce::FontOptions (16.5f, quiet ? juce::Font::plain : juce::Font::bold));
            g.drawText (toJuce (st.name), name, juce::Justification::centredLeft, true);

            // チェック
            drawCheckbox (g, row.downloadCell.toFloat().withSizeKeepingCentre (20.0f, 20.0f),
                          row.checks.canDownload, row.checks.download, downloadColour);
            drawCheckbox (g, row.uploadCell.toFloat().withSizeKeepingCentre (20.0f, 20.0f),
                          row.checks.canUpload, row.checks.upload, uploadColour);

            // 中身
            for (auto& d : row.details)
            {
                if (! d.header && hovered == &d)
                {
                    g.setColour (Theme::accent.withAlpha (0.15f));
                    g.fillRoundedRectangle (d.r.toFloat().expanded (4.0f, 0.0f), 4.0f);
                }

                g.setColour (d.header ? Theme::textDim : Theme::text);
                g.setFont (juce::FontOptions (d.header ? 13.5f : 14.5f, d.header ? juce::Font::bold : juce::Font::plain));
                g.drawText (d.text, d.r, juce::Justification::centredLeft, true);
            }
        }

        if (rows.empty())
        {
            g.setColour (Theme::textDim);
            g.setFont (juce::FontOptions (16.5f));
            g.drawText ("トラックがありません"_ju, getLocalBounds().reduced (10, 0).removeFromTop (34), juce::Justification::centredLeft);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const Detail* hit = nullptr;
        bool clickable = false;

        for (auto& row : rows)
        {
            for (auto& d : row.details)
                if (! d.header && d.r.contains (e.getPosition()))
                    hit = &d;

            clickable = clickable || row.r.contains (e.getPosition());
        }

        setMouseCursor (hit != nullptr || clickable ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);

        if (hit != hovered)
        {
            hovered = hit;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hovered = nullptr;
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const auto pos = e.getPosition();

        for (auto& row : rows)
        {
            for (auto& d : row.details)
                if (! d.header && d.r.contains (pos) && owner.onJump)
                    return owner.onJump (d.change);

            if (! row.r.contains (pos))
                continue;

            if (row.checks.canDownload && row.downloadCell.contains (pos))
                return owner.toggleCheck (row.st, true);

            if (row.checks.canUpload && row.uploadCell.contains (pos))
                return owner.toggleCheck (row.st, false);

            // それ以外: 中身を開く・閉じる
            owner.expandedId = owner.expandedId == row.st.id ? std::string() : row.st.id;
            return owner.rebuild();
        }
    }

private:
    SyncPanel& owner;
    const Detail* hovered = nullptr;
};

//==============================================================================
SyncPanel::SyncPanel (SyncManager& s, ProjectDocument& d, juce::PropertiesFile& p)
    : sync (s), document (d), settings (p)
{
    collapsed = settings.getBoolValue ("syncPanelCollapsed", false);

    list = std::make_unique<List> (*this);
    viewport.setViewedComponent (list.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);

    toggleButton.onClick = [this] { if (onToggle) onToggle(); };
    addAndMakeVisible (toggleButton);

    statusLabel.setFont (juce::FontOptions (16.0f));
    statusLabel.setMinimumHorizontalScale (0.8f);
    addAndMakeVisible (statusLabel);

    autoPull.setButtonText ("他の人の変更を自動でダウンロード"_ju);
    autoPull.setTooltip ("競合がなく、再生・録音中でなければ、他の人がアップした変更をすぐに取り込む"_ju);
    autoPull.setToggleState (autoPullEnabled(), juce::dontSendNotification);
    autoPull.onClick = [this]
    {
        settings.setValue ("syncAutoPull", autoPull.getToggleState());
        settings.saveIfNeeded();
    };
    addAndMakeVisible (autoPull);

    for (auto* b : { &downloadButton, &uploadButton, &registerButton })
        b->setColour (juce::TextButton::buttonColourId, primaryButton);

    downloadButton.setTooltip ("ダウンロードの欄にチェックの入ったものを取り込む（外したものは今のまま）"_ju);
    downloadButton.onClick = [this]
    {
        juce::String unresolved;
        const auto choices = currentChoices (&unresolved);

        if (unresolved.isNotEmpty())
        {
            statusLabel.setText ("「"_ju + unresolved + "」はどちらを使うか、ダウンロードかアップロードにチェックしてください"_ju,
                                 juce::dontSendNotification);
            statusLabel.setColour (juce::Label::textColourId, Theme::warning);
            return;
        }

        if (onDownload)
            onDownload (choices);
    };

    uploadButton.setTooltip ("アップロードの欄にチェックの入ったものをサーバーに上げる（サーバーに新しい変更があれば先に取り込む）"_ju);
    uploadButton.onClick = [this]
    {
        juce::String unresolved;
        const auto choices = currentChoices (&unresolved);

        if (sync.headPreview() != nullptr && unresolved.isNotEmpty())
        {
            statusLabel.setText ("「"_ju + unresolved + "」はどちらを使うか、ダウンロードかアップロードにチェックしてください"_ju,
                                 juce::dontSendNotification);
            statusLabel.setColour (juce::Label::textColourId, Theme::warning);
            return;
        }

        if (onUpload)
            onUpload (currentExcluded(), {}, choices);
    };

    registerButton.setButtonText ("サーバーにアップして共有"_ju);
    registerButton.onClick = [this] { if (onRegister) onRegister(); };
    settingsButton.setButtonText ("サーバー設定…"_ju);
    settingsButton.onClick = [this] { if (onServerSettings) onServerSettings(); };

    for (auto* b : { &downloadButton, &uploadButton, &registerButton, &settingsButton })
        addChildComponent (b);

    sync.addChangeListener (this);
    document.addChangeListener (this);
    setCollapsed (collapsed);
    startTimer (500);
}

SyncPanel::~SyncPanel()
{
    sync.removeChangeListener (this);
    document.removeChangeListener (this);
}

void SyncPanel::setCollapsed (bool c)
{
    collapsed = c;
    settings.setValue ("syncPanelCollapsed", collapsed);

    // 畳むと «（開く）、開くと »（畳む）
    toggleButton.setButtonText (juce::String::fromUTF8 (collapsed ? "\xC2\xAB" : "\xC2\xBB"));
    toggleButton.setTooltip (collapsed ? "同期パネルを開く（F7）"_ju : "同期パネルを畳む（F7）"_ju);
    setTooltip (collapsed ? "同期（クリックで開く）。↓ ダウンロードできる数、↑ アップロードできる数"_ju : juce::String());

    for (auto* c2 : std::initializer_list<juce::Component*> { &viewport, &statusLabel, &autoPull, &downloadButton, &uploadButton,
                                                                &registerButton, &settingsButton })
        if (collapsed)
            c2->setVisible (false);

    laidOutLinked = ! sync.isLinked();   // 次の rebuild で並べ直す
    dirty = true;
    resized();
}

void SyncPanel::clearAfterSync()
{
    downloadChecks.clear();
    uploadChecks.clear();
    expandedId.clear();
    dirty = true;
}

bool SyncPanel::autoPullEnabled() const
{
    return settings.getBoolValue ("syncAutoPull", false);
}

//==============================================================================
SyncPanel::Checks SyncPanel::checksFor (const collab::ScopeSyncState& st) const
{
    Checks c;
    const bool sameChange = st.mine && st.theirs && ! st.conflict;   // 両方で同じ変更（そのままでよい）
    c.canDownload = st.theirs && ! sameChange;
    c.canUpload = st.mine && ! sameChange;

    // 既定: 必要なほうにチェック。トラックの競合は両方（サーバーの版を使い、自分の版を別トラックで残す）、
    // トラック以外の競合は選んでもらう
    const bool pick = st.conflict && st.kind != collab::ScopeKind::track;
    c.download = c.canDownload && ! pick;
    c.upload = c.canUpload && ! pick;

    if (auto it = downloadChecks.find (st.id); it != downloadChecks.end() && c.canDownload)
        c.download = it->second;

    if (auto it = uploadChecks.find (st.id); it != uploadChecks.end() && c.canUpload)
        c.upload = it->second;

    return c;
}

void SyncPanel::toggleCheck (const collab::ScopeSyncState& st, bool downloadColumn)
{
    const auto c = checksFor (st);
    const bool on = ! (downloadColumn ? c.download : c.upload);
    (downloadColumn ? downloadChecks : uploadChecks)[st.id] = on;

    // トラック以外の競合は、どちらか一方だけ
    if (on && st.conflict && st.kind != collab::ScopeKind::track)
        (downloadColumn ? uploadChecks : downloadChecks)[st.id] = false;

    rebuild();
}

std::map<std::string, collab::Resolution> SyncPanel::currentChoices (juce::String* unresolved) const
{
    std::map<std::string, collab::Resolution> choices;

    for (auto& st : sync.scopeStates())
    {
        const auto c = checksFor (st);

        if (! c.canDownload)
            continue;

        if (! st.conflict)
        {
            choices[st.id] = c.download ? collab::Resolution::theirs : collab::Resolution::mine;
            continue;
        }

        if (c.download && c.upload && st.kind == collab::ScopeKind::track)
            choices[st.id] = collab::Resolution::both;
        else if (c.download)
            choices[st.id] = collab::Resolution::theirs;
        else if (c.upload || st.kind == collab::ScopeKind::track)
            choices[st.id] = collab::Resolution::mine;
        else if (unresolved != nullptr && unresolved->isEmpty())
            *unresolved = toJuce (st.name);
    }

    return choices;
}

std::set<std::string> SyncPanel::currentExcluded() const
{
    std::set<std::string> excluded;

    for (auto& st : sync.scopeStates())
        if (const auto c = checksFor (st); c.canUpload && ! c.upload)
            excluded.insert (st.id);

    return excluded;
}

//==============================================================================
void SyncPanel::paintCounts (juce::Graphics& g, juce::Rectangle<int> area, bool vertical) const
{
    struct Item { juce::String text; juce::Colour colour; };
    std::vector<Item> items;

    if (! sync.hasCredentials())
        items.push_back ({ "未設定"_ju, Theme::textDim });
    else if (! sync.isLinked())
        items.push_back ({ "未登録"_ju, Theme::textDim });
    else if (offline)
        items.push_back ({ "オフライン"_ju, Theme::danger });
    else
    {
        if (conflictCount > 0)   items.push_back ({ "!" + juce::String (conflictCount), Theme::warning });
        if (downloadCount > 0)   items.push_back ({ arrow (true) + juce::String (downloadCount), downloadColour });
        if (uploadCount > 0)     items.push_back ({ arrow (false) + juce::String (uploadCount), uploadColour });

        if (items.empty())
            items.push_back ({ juce::String::fromUTF8 ("\xE2\x9C\x93"), Theme::ok });   // ✓ 最新
    }

    g.setFont (juce::FontOptions (vertical ? 15.0f : 15.5f, juce::Font::bold));

    for (auto& it : items)
    {
        const bool word = it.text.length() > 3;
        juce::Rectangle<int> r;

        if (vertical)
        {
            // 縦の帯: 1 つずつ下へ（長い言葉は 1 文字ずつ縦に）
            const int h = word ? it.text.length() * 18 + 6 : 28;
            r = area.removeFromTop (h);

            if (word)
            {
                g.setColour (it.colour);
                g.setFont (juce::FontOptions (15.5f));

                for (int i = 0; i < it.text.length(); ++i)
                    g.drawText (it.text.substring (i, i + 1), r.getX(), r.getY() + 3 + i * 18, r.getWidth(), 18, juce::Justification::centred);

                continue;
            }
        }
        else
        {
            const int w = (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), it.text) + 16;
            r = area.removeFromRight (w);
        }

        g.setColour (it.colour.withAlpha (0.18f));
        g.fillRoundedRectangle (r.toFloat().reduced (2.0f, 3.0f), 6.0f);
        g.setColour (it.colour);
        g.drawText (it.text, r, juce::Justification::centred);

        if (! vertical)
            area.removeFromRight (2);
    }
}

void SyncPanel::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
    g.setColour (Theme::background);
    g.fillRect (0, 0, 2, getHeight());

    if (collapsed)
    {
        // 縦の帯: «・「同期」・件数
        auto r = getLocalBounds().withTrimmedLeft (2).withTrimmedTop (toggleButton.getBottom() + 8);
        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (17.5f, juce::Font::bold));
        g.drawText (juce::String::fromUTF8 ("\xE5\x90\x8C"), r.removeFromTop (22), juce::Justification::centred);   // 同
        g.drawText (juce::String::fromUTF8 ("\xE6\x9C\x9F"), r.removeFromTop (22), juce::Justification::centred);   // 期
        r.removeFromTop (10);
        paintCounts (g, r.reduced (4, 0), true);
        return;
    }

    auto header = getLocalBounds().withTrimmedLeft (2).removeFromTop (40);
    header.removeFromLeft (toggleButton.getRight() + 6);
    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    g.drawText ("同期"_ju, header.removeFromLeft (60), juce::Justification::centredLeft);
    paintCounts (g, header.reduced (6, 5), false);

    if (sync.isLinked())
    {
        // 表の見出し
        auto columns = viewport.getBounds().withHeight (26).translated (0, -26);
        const int listWidth = list->getWidth();
        auto cols = columns.withWidth (listWidth);
        g.setColour (Theme::background.withAlpha (0.6f));
        g.fillRect (columns);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.setColour (Theme::textDim);
        g.drawText ("トラック"_ju, cols.withTrimmedLeft (10), juce::Justification::centredLeft);
        g.setColour (downloadColour);
        g.drawText (arrow (true) + " DL", cols.withLeft (cols.getRight() - 2 * columnWidth).withWidth (columnWidth), juce::Justification::centred);
        g.setColour (uploadColour);
        g.drawText (arrow (false) + " UP", cols.withLeft (cols.getRight() - columnWidth), juce::Justification::centred);
    }
}

void SyncPanel::mouseUp (const juce::MouseEvent&)
{
    // 畳んだ帯はどこをクリックしても開く
    if (collapsed && onToggle)
        onToggle();
}

void SyncPanel::resized()
{
    laidOutLinked = sync.isLinked();
    auto area = getLocalBounds().withTrimmedLeft (2);

    if (collapsed)
    {
        toggleButton.setBounds (area.removeFromTop (40).reduced (5, 5));
        repaint();
        return;
    }

    auto top = area.removeFromTop (40);
    toggleButton.setBounds (top.removeFromLeft (40).reduced (5, 5));
    statusLabel.setBounds (area.removeFromTop (28).reduced (10, 0));
    statusLabel.setVisible (true);

    const bool linked = sync.isLinked();
    const bool configured = sync.hasCredentials();

    for (auto* c : std::initializer_list<juce::Component*> { &downloadButton, &uploadButton, &autoPull, &viewport })
        c->setVisible (linked);

    registerButton.setVisible (! linked && configured);
    settingsButton.setVisible (! configured);

    if (linked)
    {
        auto bottom = area.removeFromBottom (92).reduced (10, 8);
        auto buttons = bottom.removeFromBottom (36);
        downloadButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2 - 4));
        buttons.removeFromLeft (8);
        uploadButton.setBounds (buttons);
        bottom.removeFromBottom (6);
        autoPull.setBounds (bottom.removeFromBottom (26));

        area.removeFromTop (26);   // 表の見出し（paint で描く）
        viewport.setBounds (area);
    }
    else
    {
        auto bottom = area.removeFromTop (90).reduced (12, 8);
        registerButton.setBounds (bottom.removeFromTop (36));
        bottom.removeFromTop (8);
        settingsButton.setBounds (bottom.removeFromTop (32));
    }

    rebuild();
}

void SyncPanel::visibilityChanged()
{
    if (isVisible())
        rebuild();
}

void SyncPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    dirty = true;   // 編集のたびに計算し直すと重いので、タイマーでまとめて
}

void SyncPanel::timerCallback()
{
    if (dirty || ++ticks >= 30)
        rebuild();
}

void SyncPanel::rebuild()
{
    dirty = false;
    ticks = 0;

    if (! isShowing())
        return;   // 隠れている間は計算しない（見えたときに作り直す）

    const bool linked = sync.isLinked();

    // 曲を開いた・登録したときは並びが変わる
    if (! collapsed && linked != laidOutLinked)
        return resized();

    const auto st = sync.getServerStatus();
    offline = st.checked && ! st.online;
    downloadCount = uploadCount = conflictCount = 0;

    if (! linked)
    {
        if (! collapsed)
        {
            statusLabel.setText (sync.hasCredentials() ? "この曲はまだサーバーにありません"_ju : "サーバーが設定されていません"_ju,
                                 juce::dontSendNotification);
            statusLabel.setColour (juce::Label::textColourId, Theme::text);
            list->rows.clear();
            list->setSize (viewport.getWidth(), 10);
        }

        repaint();
        return;
    }

    auto states = sync.scopeStates();
    int checkedDownloads = 0, checkedUploads = 0;

    for (auto& s : states)
    {
        const auto c = checksFor (s);
        downloadCount += c.canDownload ? 1 : 0;
        uploadCount += c.canUpload && ! s.conflict ? 1 : 0;
        conflictCount += s.conflict ? 1 : 0;
        checkedDownloads += c.canDownload && c.download ? 1 : 0;
        checkedUploads += c.canUpload && c.upload ? 1 : 0;
    }

    if (collapsed)
    {
        repaint();
        return;
    }

    const auto preview = sync.headPreview();

    // トラックを先に、テンポ・拍子などは後ろに
    std::stable_partition (states.begin(), states.end(), [] (const collab::ScopeSyncState& s) { return s.kind == collab::ScopeKind::track; });
    const auto& local = document.getProject();
    const auto localDiff = sync.getBase() != nullptr ? collab::diffProjects (*sync.getBase(), local) : collab::ProjectDiff();

    if (offline)
    {
        statusLabel.setText ("オフライン（"_ju + st.error + "）"_ju, juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, Theme::danger);
    }
    else if (preview != nullptr)
    {
        juce::StringArray authors;
        for (auto& r : st.incoming)
            authors.addIfNotAlreadyThere (r.author);

        statusLabel.setText (authors.joinIntoString ("・"_ju) + " さんの新しい変更があります"_ju, juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, conflictCount > 0 ? Theme::warning : downloadColour);
    }
    else if (uploadCount + conflictCount > 0)
    {
        statusLabel.setText ("まだアップしていない変更があります"_ju, juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, Theme::text);
    }
    else
    {
        statusLabel.setText (st.checked ? "サーバーと同じです"_ju : "サーバーを確認しています…"_ju, juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, Theme::textDim);
    }

    const int scrollY = viewport.getViewPositionY();
    list->setSize (viewport.getMaximumVisibleWidth() > 0 ? viewport.getMaximumVisibleWidth() : viewport.getWidth(), list->getHeight());
    list->rebuild (states, localDiff, preview != nullptr ? &preview->diff : nullptr, local);
    viewport.setViewPosition (0, scrollY);

    downloadButton.setButtonText (arrow (true) + " "_ju + "ダウンロード"_ju + (checkedDownloads > 0 ? "（"_ju + juce::String (checkedDownloads) + "）"_ju : juce::String()));
    downloadButton.setEnabled (preview != nullptr);
    uploadButton.setButtonText (arrow (false) + " "_ju + "アップロード"_ju + (checkedUploads > 0 ? "（"_ju + juce::String (checkedUploads) + "）"_ju : juce::String()));
    uploadButton.setEnabled (checkedUploads > 0);
    repaint();
}

//==============================================================================
SyncToast::SyncToast()
{
    actionButton.setColour (juce::TextButton::buttonColourId, primaryButton);
    actionButton.onClick = [this]
    {
        setVisible (false);

        if (action)
            action();
    };
    addAndMakeVisible (actionButton);

    closeButton.setButtonText (juce::String::fromUTF8 ("\xC3\x97"));
    closeButton.onClick = [this] { setVisible (false); };
    addAndMakeVisible (closeButton);

    setSize (400, 104);
    setVisible (false);
}

void SyncToast::show (const juce::String& t, const juce::String& b, const juce::String& actionLabel, std::function<void()> fn, juce::Colour c)
{
    title = t;
    body = b;
    colour = c;
    action = std::move (fn);
    actionButton.setButtonText (actionLabel);
    actionButton.setVisible (actionLabel.isNotEmpty());

    // ボタンがなければ低くする（右下の位置は保つ）
    const auto bottom = getBottom();
    setSize (getWidth(), actionLabel.isNotEmpty() ? 104 : 80);
    setTopLeftPosition (getX(), bottom - getHeight());
    setVisible (true);
    toFront (false);
    resized();
    repaint();
    startTimer (15000);
}

void SyncToast::timerCallback()
{
    stopTimer();
    setVisible (false);
}

void SyncToast::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Theme::panel.withAlpha (0.96f));
    g.fillRoundedRectangle (r, 14.0f);
    Theme::drawGlass (g, r, 14.0f);

    auto area = r.reduced (16.0f, 12.0f);
    auto titleRow = area.removeFromTop (22.0f).withTrimmedRight (28.0f);
    Theme::drawStatusDot (g, titleRow.removeFromLeft (10.0f).withSizeKeepingCentre (9.0f, 9.0f), colour);
    titleRow.removeFromLeft (8.0f);
    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    g.drawText (title, titleRow, juce::Justification::centredLeft, true);

    g.setFont (juce::FontOptions (15.5f));
    g.drawFittedText (body, area.withHeight (38.0f).toNearestInt(), juce::Justification::topLeft, 2, 0.9f);
}

void SyncToast::resized()
{
    closeButton.setBounds (getWidth() - 36, 10, 26, 24);
    actionButton.setBounds (getWidth() - 136, getHeight() - 40, 120, 28);
}

void SyncToast::mouseUp (const juce::MouseEvent&)
{
    setVisible (false);
}
