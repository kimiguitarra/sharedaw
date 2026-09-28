#include "SyncPanel.h"

#include "Theme.h"

namespace
{
    const juce::Colour primaryButton = Theme::accent.darker (0.45f);

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
}

//==============================================================================
/** トラックの一覧（行を自前で描いて、チェック・選択・展開を受け付ける）。 */
class SyncPanel::List  : public juce::Component
{
public:
    explicit List (SyncPanel& o) : owner (o) {}

    struct Segment
    {
        juce::Rectangle<int> r;
        collab::Resolution value;
        juce::String label;
    };

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
        juce::Rectangle<int> r, checkbox, nameArea;
        bool checkable = false, checked = false;
        std::vector<Segment> segments;
        std::optional<collab::Resolution> choice;
        std::vector<Detail> details;
    };

    std::vector<Row> rows;

    void rebuild (const std::vector<collab::ScopeSyncState>& states, const collab::ProjectDiff& localDiff,
                  const collab::ProjectDiff* serverDiff, const collab::Project& local)
    {
        rows.clear();
        const int width = juce::jmax (220, getWidth());
        int y = 4;

        for (auto& st : states)
        {
            Row row;
            row.st = st;
            row.colour = scopeColour (local, st);

            const bool mineOnly = st.mine && ! st.theirs;
            const bool theirsOnly = st.theirs && ! st.mine;
            const bool bothSame = st.mine && st.theirs && ! st.conflict;   // 同じ変更（そのままでよい）

            row.checkable = mineOnly;
            row.checked = mineOnly && owner.excluded.count (st.id) == 0;

            if (auto it = owner.choices.find (st.id); it != owner.choices.end())
                row.choice = it->second;

            const int h = 46;
            row.r = { 6, y, width - 12, h };
            row.checkbox = { row.r.getX() + 8, row.r.getY() + 14, 18, 18 };
            row.nameArea = row.r.withTrimmedLeft (34);

            // 右側の選択（サーバーの変更・競合）
            auto seg = row.r.reduced (8, 11).removeFromRight (st.conflict ? (st.kind == collab::ScopeKind::track ? 168 : 114) : 128);

            if (st.conflict)
            {
                const int n = st.kind == collab::ScopeKind::track ? 3 : 2;
                const int w = seg.getWidth() / n;
                row.segments.push_back ({ seg.removeFromLeft (w), collab::Resolution::mine, "自分"_ju });
                row.segments.push_back ({ seg.removeFromLeft (w), collab::Resolution::theirs, "サーバー"_ju });

                if (n == 3)
                    row.segments.push_back ({ seg, collab::Resolution::both, "両方"_ju });
            }
            else if (theirsOnly)
            {
                const int w = seg.getWidth() / 2;
                row.segments.push_back ({ seg.removeFromLeft (w), collab::Resolution::theirs, "取り込む"_ju });
                row.segments.push_back ({ seg, collab::Resolution::mine, "今のまま"_ju });

                if (! row.choice)
                    row.choice = collab::Resolution::theirs;
            }

            juce::ignoreUnused (bothSame);
            y += h;

            // 開いている行: 自分の変更とサーバーの変更の中身
            if (owner.expandedId == st.id)
            {
                auto addDetails = [&] (const juce::String& title, const std::vector<collab::Change>& changes)
                {
                    if (changes.empty())
                        return;

                    row.details.push_back ({ { 40, y, width - 52, 22 }, {}, true, title });
                    y += 22;

                    for (auto& c : changes)
                    {
                        row.details.push_back ({ { 48, y, width - 60, 21 }, c, false, toJuce (c.summary) });
                        y += 21;
                    }
                };

                addDetails ("この PC の変更"_ju, localDiff.forScope (st.id));

                if (serverDiff != nullptr)
                    addDetails ("サーバーの変更"_ju, serverDiff->forScope (st.id));

                if (row.details.empty())
                {
                    row.details.push_back ({ { 40, y, width - 52, 22 }, {}, true, "変更はありません"_ju });
                    y += 22;
                }

                y += 6;
            }

            rows.push_back (std::move (row));
        }

        setSize (width, y + 8);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        for (auto& row : rows)
        {
            const auto& st = row.st;
            auto r = row.r.toFloat();
            const bool expanded = owner.expandedId == st.id;

            // 行の面: サーバーで新しくなったものは青、競合は橙をうっすら混ぜたガラス
            juce::Colour tint;

            if (st.conflict)                 tint = Theme::warning.withAlpha (0.2f);
            else if (st.theirs && ! st.mine) tint = Theme::accent.withAlpha (0.18f);
            else if (expanded)               tint = juce::Colours::white.withAlpha (0.05f);

            if (st.mine || st.theirs || expanded)
                Theme::drawGlass (g, r.reduced (0.0f, 2.0f), 8.0f, tint);

            // チェック
            if (row.checkable)
            {
                auto cb = row.checkbox.toFloat();
                g.setColour (row.checked ? Theme::accent : juce::Colours::white.withAlpha (0.12f));
                g.fillRoundedRectangle (cb, 4.0f);
                g.setColour (juce::Colours::white.withAlpha (0.3f));
                g.drawRoundedRectangle (cb.reduced (0.5f), 4.0f, 1.0f);

                if (row.checked)
                {
                    juce::Path tick;
                    tick.startNewSubPath (cb.getX() + 4.0f, cb.getCentreY());
                    tick.lineTo (cb.getX() + 7.5f, cb.getBottom() - 4.5f);
                    tick.lineTo (cb.getRight() - 4.0f, cb.getY() + 4.5f);
                    g.setColour (Theme::background);
                    g.strokePath (tick, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
            }

            // 色の丸と名前
            auto text = row.nameArea.toFloat().withTrimmedRight (row.segments.empty() ? 8.0f : 8.0f + (float) (row.r.getRight() - row.segments.front().r.getX()));
            Theme::drawStatusDot (g, text.removeFromLeft (12.0f).withSizeKeepingCentre (10.0f, 10.0f).translated (0.0f, -8.0f), row.colour);
            text.removeFromLeft (8.0f);

            const bool quiet = ! st.mine && ! st.theirs;
            g.setColour (quiet ? Theme::textDim : Theme::text);
            g.setFont (juce::FontOptions (15.0f, quiet ? juce::Font::plain : juce::Font::bold));
            g.drawText (toJuce (st.name), text.removeFromTop (24.0f).translated (0.0f, 3.0f), juce::Justification::centredLeft, true);

            juce::String status;
            auto statusColour = Theme::textDim;

            if (st.conflict)                    { status = "競合（両方で変更）"_ju; statusColour = Theme::warning; }
            else if (st.theirs && ! st.mine)    { status = "サーバーで更新されました"_ju;       statusColour = Theme::accent; }
            else if (st.mine && st.theirs)      { status = "サーバーと同じ変更"_ju; }
            else if (st.mine)                   { status = row.checked ? "この PC で変更（アップする）"_ju : "この PC で変更（今回はアップしない）"_ju;
                                                  statusColour = row.checked ? Theme::text : Theme::textDim; }
            else                                { status = "変更なし"_ju; }

            if (st.conflict && ! row.choice)
                status = "競合: 採用する版を選択"_ju;

            g.setColour (statusColour);
            g.setFont (juce::FontOptions (13.0f));
            g.drawText (status, text.translated (0.0f, -1.0f), juce::Justification::centredLeft, true);

            // 選択（セグメント）
            for (auto& s : row.segments)
            {
                auto sr = s.r.toFloat().reduced (1.0f, 0.0f);
                const bool on = row.choice && *row.choice == s.value;
                g.setColour (on ? Theme::accent : juce::Colours::white.withAlpha (0.08f));
                g.fillRoundedRectangle (sr, 6.0f);
                g.setColour (juce::Colours::white.withAlpha (on ? 0.0f : 0.2f));
                g.drawRoundedRectangle (sr.reduced (0.5f), 6.0f, 1.0f);
                g.setColour (on ? Theme::background : Theme::text);
                g.setFont (juce::FontOptions (13.0f, on ? juce::Font::bold : juce::Font::plain));
                g.drawText (s.label, sr, juce::Justification::centred, true);
            }

            // 中身
            for (auto& d : row.details)
            {
                const bool hover = ! d.header && hovered == &d;

                if (hover)
                {
                    g.setColour (Theme::accent.withAlpha (0.15f));
                    g.fillRoundedRectangle (d.r.toFloat().expanded (4.0f, 0.0f), 4.0f);
                }

                g.setColour (d.header ? Theme::textDim : Theme::text);
                g.setFont (juce::FontOptions (d.header ? 12.5f : 13.5f, d.header ? juce::Font::bold : juce::Font::plain));
                g.drawText (d.text, d.r, juce::Justification::centredLeft, true);
            }
        }

        if (rows.empty())
        {
            g.setColour (Theme::textDim);
            g.setFont (juce::FontOptions (14.0f));
            g.drawText ("トラックがありません"_ju, getLocalBounds().reduced (12).removeFromTop (30), juce::Justification::centredLeft);
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

            if (row.checkable && row.checkbox.expanded (6).contains (pos))
            {
                if (owner.excluded.count (row.st.id) > 0)
                    owner.excluded.erase (row.st.id);
                else
                    owner.excluded.insert (row.st.id);

                return owner.rebuild();
            }

            for (auto& s : row.segments)
                if (s.r.contains (pos))
                {
                    owner.choices[row.st.id] = s.value;
                    return owner.rebuild();
                }

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
    list = std::make_unique<List> (*this);
    viewport.setViewedComponent (list.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);

    closeButton.setButtonText (juce::String::fromUTF8 ("\xC3\x97"));
    closeButton.setTooltip ("同期パネルを閉じる（F7）"_ju);
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeButton);

    statusLabel.setFont (juce::FontOptions (14.0f));
    statusLabel.setMinimumHorizontalScale (0.8f);
    addAndMakeVisible (statusLabel);

    hintLabel.setFont (juce::FontOptions (13.0f));
    hintLabel.setColour (juce::Label::textColourId, Theme::textDim);
    hintLabel.setText ("行をクリックすると、何が変わったかを表示します"_ju, juce::dontSendNotification);
    addAndMakeVisible (hintLabel);

    comment.setTextToShowWhenEmpty ("コメント（何を変えたか。任意）"_ju, Theme::textDim);
    comment.setFont (juce::FontOptions (15.0f));
    comment.onReturnKey = [this] { uploadButton.triggerClick(); };
    addAndMakeVisible (comment);

    autoPull.setButtonText ("他の人の変更を自動でダウンロード（競合がないとき）"_ju);
    autoPull.setToggleState (autoPullEnabled(), juce::dontSendNotification);
    autoPull.onClick = [this]
    {
        settings.setValue ("syncAutoPull", autoPull.getToggleState());
        settings.saveIfNeeded();
    };
    addAndMakeVisible (autoPull);

    for (auto* b : { &downloadButton, &uploadButton, &registerButton })
        b->setColour (juce::TextButton::buttonColourId, primaryButton);

    downloadButton.onClick = [this]
    {
        // 競合は選んでもらってから
        for (auto& st : sync.scopeStates())
            if (st.conflict && choices.count (st.id) == 0)
            {
                expandedId = st.id;
                rebuild();
                statusLabel.setText ("「"_ju + toJuce (st.name) + "」は両方で変更されています。採用する版を選んでください"_ju,
                                     juce::dontSendNotification);
                statusLabel.setColour (juce::Label::textColourId, Theme::warning);
                return;
            }

        if (onDownload)
            onDownload (choices);
    };

    uploadButton.onClick = [this]
    {
        if (sync.headPreview() != nullptr)
            for (auto& st : sync.scopeStates())
                if (st.conflict && choices.count (st.id) == 0)
                {
                    expandedId = st.id;
                    rebuild();
                    statusLabel.setText ("先にダウンロードします。「"_ju + toJuce (st.name) + "」の採用する版を選んでください"_ju,
                                         juce::dontSendNotification);
                    statusLabel.setColour (juce::Label::textColourId, Theme::warning);
                    return;
                }

        if (onUpload)
            onUpload (excluded, comment.getText().trim(), choices);
    };

    registerButton.setButtonText ("サーバーにアップして共有"_ju);
    registerButton.onClick = [this] { if (onRegister) onRegister(); };
    settingsButton.setButtonText ("サーバー設定…"_ju);
    settingsButton.onClick = [this] { if (onServerSettings) onServerSettings(); };

    for (auto* b : { &downloadButton, &uploadButton, &registerButton, &settingsButton })
        addChildComponent (b);

    sync.addChangeListener (this);
    document.addChangeListener (this);
    startTimer (500);
}

SyncPanel::~SyncPanel()
{
    sync.removeChangeListener (this);
    document.removeChangeListener (this);
}

void SyncPanel::clearAfterSync()
{
    comment.clear();
    choices.clear();
    excluded.clear();
    expandedId.clear();
    dirty = true;
}

bool SyncPanel::autoPullEnabled() const
{
    return settings.getBoolValue ("syncAutoPull", false);
}

void SyncPanel::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
    g.setColour (Theme::background);
    g.fillRect (0, 0, 2, getHeight());

    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ("同期"_ju, 14, 0, 200, 34, juce::Justification::centredLeft);

    // 下の操作の場所はガラスの面にする
    if (sync.isLinked())
        Theme::drawGlass (g, juce::Rectangle<float> (8.0f, (float) getHeight() - 150.0f, (float) getWidth() - 16.0f, 142.0f), 12.0f);
}

void SyncPanel::resized()
{
    laidOutLinked = sync.isLinked();
    auto area = getLocalBounds().withTrimmedLeft (2);
    auto top = area.removeFromTop (34);
    closeButton.setBounds (top.removeFromRight (34).reduced (4));

    statusLabel.setBounds (area.removeFromTop (26).reduced (10, 0));
    hintLabel.setBounds (area.removeFromTop (20).reduced (10, 0));

    if (sync.isLinked())
    {
        auto bottom = area.removeFromBottom (150).reduced (18, 14);
        auto buttons = bottom.removeFromBottom (34);
        downloadButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2 - 4));
        buttons.removeFromLeft (8);
        uploadButton.setBounds (buttons);
        bottom.removeFromBottom (8);
        autoPull.setBounds (bottom.removeFromBottom (24));
        bottom.removeFromBottom (6);
        comment.setBounds (bottom.removeFromBottom (32));
    }
    else
    {
        auto bottom = area.removeFromTop (90).reduced (14, 8);
        registerButton.setBounds (bottom.removeFromTop (34));
        bottom.removeFromTop (8);
        settingsButton.setBounds (bottom.removeFromTop (30));
    }

    viewport.setBounds (area.reduced (4, 4));
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
    const bool configured = sync.hasCredentials();

    // 曲を開いた・登録したときは並びが変わる
    if (linked != laidOutLinked)
        return resized();

    // 登録していない・設定していない
    for (auto* c : std::initializer_list<juce::Component*> { &downloadButton, &uploadButton, &comment, &autoPull })
        c->setVisible (linked);

    registerButton.setVisible (! linked && configured);
    settingsButton.setVisible (! configured);
    hintLabel.setVisible (linked);

    if (! linked)
    {
        statusLabel.setText (configured ? "この曲はまだサーバーにありません"_ju : "サーバーが設定されていません"_ju, juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, Theme::text);
        list->rows.clear();
        list->setSize (viewport.getWidth(), 10);
        list->repaint();
        return;
    }

    const auto st = sync.getServerStatus();
    const auto preview = sync.headPreview();
    auto states = sync.scopeStates();

    // トラックを先に、テンポ・拍子などは後ろに
    std::stable_partition (states.begin(), states.end(), [] (const collab::ScopeSyncState& s) { return s.kind == collab::ScopeKind::track; });
    const auto& local = document.getProject();
    const auto localDiff = sync.getBase() != nullptr ? collab::diffProjects (*sync.getBase(), local) : collab::ProjectDiff();

    // 使われなくなった選択を消す
    for (auto it = choices.begin(); it != choices.end();)
    {
        const bool used = std::any_of (states.begin(), states.end(), [&] (auto& s) { return s.id == it->first && s.theirs; });
        it = used ? std::next (it) : choices.erase (it);
    }

    // 上の状況の行
    int mine = 0, theirs = 0, conflicts = 0, changedHere = 0;

    for (auto& s : states)
    {
        changedHere += s.mine ? 1 : 0;
        mine += s.mine && ! s.theirs && excluded.count (s.id) == 0 ? 1 : 0;
        theirs += s.theirs ? 1 : 0;
        conflicts += s.conflict ? 1 : 0;
    }

    if (st.checked && ! st.online)
    {
        statusLabel.setText ("オフライン（"_ju + st.error + "）"_ju, juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, Theme::danger);
    }
    else if (preview != nullptr)
    {
        juce::StringArray authors;
        for (auto& r : st.incoming)
            authors.addIfNotAlreadyThere (r.author);

        statusLabel.setText (authors.joinIntoString ("・"_ju) + " さんの新しい変更があります"_ju
                               + (conflicts > 0 ? "（競合 "_ju + juce::String (conflicts) + " 件）"_ju : juce::String()),
                             juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, conflicts > 0 ? Theme::warning : Theme::accent);
    }
    else if (changedHere > 0)
    {
        statusLabel.setText ("この PC に、まだアップしていない変更が "_ju + juce::String (changedHere) + " 件あります"_ju, juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, Theme::text);
    }
    else
    {
        statusLabel.setText (st.checked ? "サーバーと同じ状態です"_ju : "サーバーを確認しています…"_ju, juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, Theme::text);
    }

    const int scrollY = viewport.getViewPositionY();
    list->setSize (viewport.getMaximumVisibleWidth() > 0 ? viewport.getMaximumVisibleWidth() : viewport.getWidth(), list->getHeight());
    list->rebuild (states, localDiff, preview != nullptr ? &preview->diff : nullptr, local);
    viewport.setViewPosition (0, scrollY);

    downloadButton.setButtonText (theirs > 0 ? "ダウンロード（"_ju + juce::String (theirs) + "）"_ju : "ダウンロード"_ju);
    downloadButton.setEnabled (preview != nullptr);
    uploadButton.setButtonText (preview != nullptr && mine > 0 ? "取り込んでアップ（"_ju + juce::String (mine) + "）"_ju
                                : mine > 0 ? "アップ（"_ju + juce::String (mine) + "）"_ju : "アップ"_ju);
    uploadButton.setEnabled (mine > 0 || (preview != nullptr && conflicts > 0));
    repaint();
}

//==============================================================================
SyncBadge::SyncBadge (SyncManager& s, ProjectDocument& d)
    : juce::Button ("sync"), sync (s), document (d)
{
    setTooltip ("サーバーとの同期の状況（クリックで同期パネル。F7）"_ju);
    sync.addChangeListener (this);
    document.addChangeListener (this);
    startTimer (700);
}

SyncBadge::~SyncBadge()
{
    sync.removeChangeListener (this);
    document.removeChangeListener (this);
}

void SyncBadge::changeListenerCallback (juce::ChangeBroadcaster*)
{
    dirty = true;
}

void SyncBadge::timerCallback()
{
    if (! dirty)
        return;

    dirty = false;
    mine = theirs = conflicts = 0;

    for (auto& st : sync.scopeStates())
    {
        mine += st.mine && ! st.theirs ? 1 : 0;
        theirs += st.theirs ? 1 : 0;
        conflicts += st.conflict ? 1 : 0;
    }

    repaint();
}

void SyncBadge::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    getLookAndFeel().drawButtonBackground (g, *this, findColour (juce::TextButton::buttonColourId), highlighted, down);

    auto r = getLocalBounds().toFloat().reduced (10.0f, 0.0f);
    juce::Colour dot;
    juce::String label;
    const auto st = sync.getServerStatus();

    if (! sync.hasCredentials())                 { dot = Theme::textDim; label = "同期: 未設定"_ju; }
    else if (! sync.isLinked())                  { dot = Theme::textDim; label = "同期: 未登録"_ju; }
    else if (st.checked && ! st.online)          { dot = Theme::danger;  label = "同期: オフライン"_ju; }
    else if (conflicts > 0)                      { dot = Theme::warning; label = "競合 "_ju + juce::String (conflicts); }
    else if (theirs > 0 || mine > 0)
    {
        dot = theirs > 0 ? Theme::accent : Theme::warning;
        juce::StringArray parts;

        if (theirs > 0)
            parts.add (juce::String::fromUTF8 ("\xE2\x86\x93") + juce::String (theirs) + " 新着"_ju);   // ↓

        if (mine > 0)
            parts.add (juce::String::fromUTF8 ("\xE2\x86\x91") + juce::String (mine) + " 変更"_ju);     // ↑

        label = parts.joinIntoString ("  ");
    }
    else                                         { dot = Theme::ok; label = st.checked ? "同期: 最新"_ju : "同期: 確認中"_ju; }

    Theme::drawStatusDot (g, r.removeFromLeft (10.0f).withSizeKeepingCentre (9.0f, 9.0f), dot);
    r.removeFromLeft (7.0f);
    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (label, r, juce::Justification::centredLeft, true);
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
    g.setFont (juce::FontOptions (15.5f, juce::Font::bold));
    g.drawText (title, titleRow, juce::Justification::centredLeft, true);

    g.setFont (juce::FontOptions (14.0f));
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
