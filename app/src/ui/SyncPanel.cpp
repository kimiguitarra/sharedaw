#include "SyncPanel.h"

#include <set>

#include "Theme.h"

namespace
{
    juce::String ago (juce::Time t)
    {
        if (t.toMilliseconds() == 0)
            return {};

        const auto seconds = (juce::Time::getCurrentTime() - t).inSeconds();

        if (seconds < 45)    return "たった今"_ju;
        if (seconds < 3600)  return juce::String (juce::roundToInt (seconds / 60.0)) + " 分前"_ju;
        if (seconds < 86400) return juce::String (juce::roundToInt (seconds / 3600.0)) + " 時間前"_ju;
        return t.formatted ("%m/%d %H:%M");
    }

    const juce::Colour primaryButton = Theme::accent.darker (0.45f);
}

//==============================================================================
class SyncPanel::Content  : public juce::Component
{
public:
    explicit Content (SyncPanel& o) : owner (o)
    {
        for (auto* b : { &pullButton, &pushButton, &registerButton, &settingsButton, &pickerButton, &refreshButton, &historyButton })
            addChildComponent (b);

        pullButton.setButtonText ("取り込む"_ju);
        pushButton.setButtonText ("アップする"_ju);
        registerButton.setButtonText ("サーバーにアップして共有"_ju);
        settingsButton.setButtonText ("サーバー設定…"_ju);
        pickerButton.setButtonText ("楽曲を選ぶ…"_ju);
        refreshButton.setButtonText ("今すぐ確認"_ju);
        historyButton.setButtonText ("履歴をすべて見る…"_ju);

        for (auto* b : { &pullButton, &pushButton, &registerButton })
            b->setColour (juce::TextButton::buttonColourId, primaryButton);

        pullButton.onClick = [this] { if (owner.onPull) owner.onPull(); };
        pushButton.onClick = [this]
        {
            if (owner.onPush)
                owner.onPush (comment.getText().trim(), releaseLocks.getToggleState());
        };
        registerButton.onClick = [this] { if (owner.onRegister) owner.onRegister(); };
        settingsButton.onClick = [this] { if (owner.onServerSettings) owner.onServerSettings(); };
        pickerButton.onClick = [this] { if (owner.onOpenPicker) owner.onOpenPicker(); };
        refreshButton.onClick = [this] { owner.sync.checkServerNow(); };
        historyButton.onClick = [this] { if (owner.onShowHistory) owner.onShowHistory(); };

        comment.setTextToShowWhenEmpty ("コメント（何を変えたか。任意）"_ju, Theme::textDim);
        comment.setFont (juce::FontOptions (14.0f));
        comment.onReturnKey = [this] { pushButton.triggerClick(); };
        addChildComponent (comment);

        releaseLocks.setButtonText ("アップしたらロックを解除"_ju);
        releaseLocks.setToggleState (true, juce::dontSendNotification);
        addChildComponent (releaseLocks);

        autoPull.setButtonText ("他の人の変更を自動で取り込む"_ju);
        autoPull.setToggleState (owner.autoPullEnabled(), juce::dontSendNotification);
        autoPull.onClick = [this]
        {
            owner.settings.setValue ("syncAutoPull", autoPull.getToggleState());
            owner.settings.saveIfNeeded();
        };
        addChildComponent (autoPull);
    }

    struct Item
    {
        enum class Kind { header, title, text, dim, note, scope, change, revision, lock };
        Kind kind = Kind::text;
        juce::Rectangle<int> r;
        juce::String a, b;
        juce::Colour colour;
        collab::Change change;
        bool clickable = false;
    };

    void rebuild (const collab::ProjectDiff* local)
    {
        hovered = nullptr;
        items.clear();

        // 表示する部品を集めて最後にまとめて切り替える（一度隠すと入力中のコメント欄からフォーカスが外れる）
        std::set<juce::Component*> shown;

        auto& sync = owner.sync;
        const int width = juce::jmax (200, getWidth());
        const int pad = 12;
        int y = 0;

        auto add = [&] (Item::Kind kind, int height, juce::String a, juce::String b = {}, juce::Colour colour = {}) -> Item&
        {
            Item it;
            it.kind = kind;
            it.r = kind == Item::Kind::header ? juce::Rectangle<int> (0, y, width, height) : juce::Rectangle<int> (pad, y, width - pad * 2, height);
            it.a = std::move (a);
            it.b = std::move (b);
            it.colour = colour;
            items.push_back (it);
            y += height;
            return items.back();
        };

        auto place = [&] (juce::Component& c, int height)
        {
            c.setBounds (pad, y, width - pad * 2, height);
            shown.insert (&c);
            y += height + 6;
        };

        auto section = [&] (const juce::String& title, int count)
        {
            y += 8;
            add (Item::Kind::header, 28, title, count > 0 ? juce::String (count) + " 件"_ju : juce::String());
            y += 6;
        };

        auto addChanges = [&] (const collab::ProjectDiff& diff, int maxLines)
        {
            std::string lastScope;
            int lines = 0, hidden = 0;

            for (auto& c : diff.changes)
            {
                if (lines >= maxLines)
                {
                    ++hidden;
                    continue;
                }

                if (c.scopeId != lastScope)
                {
                    auto& it = add (Item::Kind::scope, 22, toJuce (c.scopeName));
                    it.change = c;
                    it.clickable = true;
                    lastScope = c.scopeId;
                    ++lines;
                }

                auto& it = add (Item::Kind::change, 20, toJuce (c.summary));
                it.change = c;
                it.clickable = true;
                ++lines;
            }

            if (hidden > 0)
                add (Item::Kind::dim, 20, "ほか "_ju + juce::String (hidden) + " 件"_ju);
        };

        auto addRevision = [&] (const SyncManager::RevisionInfo& r, bool markNew)
        {
            auto& it = add (Item::Kind::revision, 40, "#"_ju + juce::String (r.number) + "  " + r.author + "  " + ago (r.createdAt),
                            r.message.isNotEmpty() ? r.message : "（コメントなし）"_ju);

            if (markNew)
                it.colour = Theme::accent;
        };

        //======================================================================
        // 曲名と接続の状況
        y += 10;
        add (Item::Kind::title, 26, toJuce (owner.document.getProject().name));

        if (! sync.hasCredentials())
        {
            add (Item::Kind::text, 22, "サーバーが設定されていません"_ju);
            y += 6;
            place (settingsButton, 28);
        }
        else if (! sync.isLinked())
        {
            add (Item::Kind::text, 22, "この曲はまだサーバーにありません"_ju);
            add (Item::Kind::dim, 38, "アップすると、仲間がダウンロードして一緒に作業できます。"_ju);
            y += 6;
            place (registerButton, 30);
            place (pickerButton, 28);
        }
        else
        {
            const auto st = sync.getServerStatus();

            if (st.checking && ! st.checked)
                add (Item::Kind::text, 22, "サーバーを確認しています…"_ju, {}, Theme::textDim);
            else if (! st.online)
                add (Item::Kind::text, 22, "オフライン"_ju + (st.error.isNotEmpty() ? "（"_ju + st.error + "）"_ju : juce::String()), "dot", Theme::danger);
            else
                add (Item::Kind::text, 22, "接続中（"_ju + ago (st.checkedAt) + "に確認）"_ju, "dot", Theme::ok);

            const int localRev = sync.getMeta().baseRevision;
            juce::String revs = "この PC: rev "_ju + juce::String (localRev);

            if (st.online)
                revs << "　　サーバー: rev "_ju << st.head;

            add (Item::Kind::text, 22, revs, {}, st.online && st.head > localRev ? Theme::accent : Theme::text);
            y += 4;
            place (refreshButton, 26);
        }

        if (! sync.isLinked())
        {
            applyVisibility (shown);
            setSize (width, y + 10);
            repaint();
            return;
        }

        const auto st = sync.getServerStatus();

        //======================================================================
        // サーバーの新しい変更
        {
            const int n = (int) st.incoming.size();
            section ("サーバーの新しい変更"_ju, n);

            if (n == 0)
            {
                add (Item::Kind::dim, 22, st.online ? "新しい変更はありません"_ju : "サーバーを確認できません"_ju);
            }
            else
            {
                for (int i = 0; i < juce::jmin (n, 4); ++i)
                    addRevision (st.incoming[(size_t) i], false);

                if (n > 4)
                    add (Item::Kind::dim, 20, "ほか "_ju + juce::String (n - 4) + " 件のリビジョン"_ju);

                if (st.preview != nullptr)
                {
                    y += 4;
                    add (Item::Kind::note, 20, "変わったところ（クリックでその場所へ）"_ju);
                    addChanges (st.preview->diff, 16);
                }

                y += 8;
                place (pullButton, 30);
            }

            place (autoPull, 22);
        }

        //======================================================================
        // この PC の変更
        {
            const int n = local != nullptr ? (int) local->changedScopeIds.size() : 0;
            section ("この PC の変更（未送信）"_ju, n);

            if (local == nullptr || local->empty())
            {
                add (Item::Kind::dim, 22, "まだアップしていない変更はありません"_ju);
            }
            else
            {
                addChanges (*local, 18);

                if (! st.incoming.empty())
                {
                    y += 4;
                    add (Item::Kind::text, 36, "サーバーに新しい変更があるので、取り込んでからアップします"_ju, {}, Theme::warning);
                }

                y += 8;
                place (comment, 28);
                place (releaseLocks, 22);
                place (pushButton, 30);
            }
        }

        //======================================================================
        // ロック
        {
            const auto locks = sync.getLocks();
            section ("ロック（編集中）"_ju, (int) locks.size());

            if (locks.empty())
                add (Item::Kind::dim, 22, "誰もロックしていません"_ju);

            for (auto& [scopeId, info] : locks)
            {
                const bool mine = info.userId == sync.getMeta().userId;
                add (Item::Kind::lock, 22, sync.scopeName (scopeId), mine ? info.displayName + "（自分）"_ju : info.displayName,
                     mine ? Theme::ok : Theme::warning);
            }
        }

        //======================================================================
        // 履歴
        {
            section ("最近の履歴"_ju, 0);

            if (st.history.empty())
                add (Item::Kind::dim, 22, "まだありません"_ju);

            for (int i = 0; i < juce::jmin ((int) st.history.size(), 6); ++i)
                addRevision (st.history[(size_t) i], st.history[(size_t) i].number > sync.getMeta().baseRevision);

            y += 6;
            place (historyButton, 26);
        }

        applyVisibility (shown);
        setSize (width, y + 12);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panel);

        for (auto& it : items)
        {
            auto r = it.r.toFloat();
            const bool hover = it.clickable && hovered == &it;

            if (hover)
            {
                g.setColour (Theme::accent.withAlpha (0.15f));
                g.fillRect (r.expanded (4.0f, 0.0f));
            }

            switch (it.kind)
            {
                case Item::Kind::header:
                    Theme::drawSectionHeader (g, it.r, it.a, it.b);
                    break;

                case Item::Kind::title:
                    g.setColour (Theme::text);
                    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
                    g.drawText (it.a, r, juce::Justification::centredLeft, true);
                    break;

                case Item::Kind::text:
                {
                    auto area = r;

                    if (it.b == "dot")
                    {
                        Theme::drawStatusDot (g, area.removeFromLeft (10.0f).withSizeKeepingCentre (8.0f, 8.0f), it.colour);
                        area.removeFromLeft (6.0f);
                    }

                    g.setColour (it.b == "dot" || it.colour.isTransparent() ? Theme::text : it.colour);
                    g.setFont (juce::FontOptions (14.0f));
                    g.drawFittedText (it.a, area.toNearestInt(), juce::Justification::centredLeft, 2, 0.9f);
                    break;
                }

                case Item::Kind::dim:
                case Item::Kind::note:
                    g.setColour (Theme::textDim);
                    g.setFont (juce::FontOptions (13.0f));
                    g.drawFittedText (it.a, it.r, juce::Justification::centredLeft, 2, 0.9f);
                    break;

                case Item::Kind::scope:
                    g.setColour (Theme::text);
                    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
                    g.drawText (it.a, r, juce::Justification::centredLeft, true);
                    break;

                case Item::Kind::change:
                    g.setColour (hover ? Theme::text : Theme::text.withAlpha (0.8f));
                    g.setFont (juce::FontOptions (13.0f));
                    g.drawText (it.a, r.withTrimmedLeft (14.0f), juce::Justification::centredLeft, true);
                    break;

                case Item::Kind::revision:
                    g.setColour (it.colour.isTransparent() ? Theme::text : it.colour);
                    g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
                    g.drawText (it.a, r.withHeight (20.0f), juce::Justification::centredLeft, true);
                    g.setColour (Theme::textDim);
                    g.setFont (juce::FontOptions (13.0f));
                    g.drawText (it.b, r.withTrimmedTop (20.0f).withTrimmedLeft (14.0f), juce::Justification::centredLeft, true);
                    break;

                case Item::Kind::lock:
                {
                    auto area = r;
                    g.setColour (Theme::text);
                    g.setFont (juce::FontOptions (13.5f));
                    g.drawText (it.a, area.removeFromLeft (area.getWidth() * 0.5f), juce::Justification::centredLeft, true);
                    g.setColour (it.colour);
                    g.drawText (it.b, area, juce::Justification::centredLeft, true);
                    break;
                }
            }
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const Item* hit = nullptr;

        for (auto& it : items)
            if (it.clickable && it.r.expanded (4, 0).contains (e.getPosition()))
                hit = &it;

        if (hit != hovered)
        {
            hovered = hit;
            setMouseCursor (hit != nullptr ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
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
        for (auto& it : items)
            if (it.clickable && it.r.expanded (4, 0).contains (e.getPosition()) && owner.onJump)
                return owner.onJump (it.change);
    }

    juce::TextButton pullButton, pushButton, registerButton, settingsButton, pickerButton, refreshButton, historyButton;
    juce::TextEditor comment;
    juce::ToggleButton releaseLocks, autoPull;

private:
    SyncPanel& owner;
    std::vector<Item> items;
    const Item* hovered = nullptr;

    void applyVisibility (const std::set<juce::Component*>& shown)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &pullButton, &pushButton, &registerButton, &settingsButton, &pickerButton,
                                                                 &refreshButton, &historyButton, &comment, &releaseLocks, &autoPull })
            c->setVisible (shown.count (c) > 0);
    }
};

//==============================================================================
SyncPanel::SyncPanel (SyncManager& s, ProjectDocument& d, juce::PropertiesFile& p)
    : sync (s), document (d), settings (p)
{
    content = std::make_unique<Content> (*this);
    viewport.setViewedComponent (content.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);

    closeButton.setButtonText (juce::String::fromUTF8 ("\xC3\x97"));
    closeButton.setTooltip ("同期パネルを閉じる（F7）"_ju);
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeButton);

    sync.addChangeListener (this);
    document.addChangeListener (this);
    startTimer (1000);
}

SyncPanel::~SyncPanel()
{
    sync.removeChangeListener (this);
    document.removeChangeListener (this);
}

void SyncPanel::clearComment()
{
    content->comment.clear();
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
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("同期"_ju, 14, 0, 100, 30, juce::Justification::centredLeft);
    g.setColour (Theme::background);
    g.drawHorizontalLine (29, 0.0f, (float) getWidth());
}

void SyncPanel::resized()
{
    closeButton.setBounds (getWidth() - 32, 3, 26, 24);
    viewport.setBounds (getLocalBounds().withTrimmedTop (30).withTrimmedLeft (2));
    rebuild();
}

void SyncPanel::visibilityChanged()
{
    if (isVisible())
        rebuild();
}

void SyncPanel::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &document)
        localDirty = true;   // 編集のたびに計算し直すと重いので、タイマーでまとめて
    else
        rebuild();
}

void SyncPanel::timerCallback()
{
    // 編集があったらまとめて計算し直す。「○分前」の表示は 15 秒ごとに進める
    if (localDirty || ++ticksSinceRebuild >= 15)
        rebuild();
}

void SyncPanel::rebuild()
{
    if (! isShowing())
        return;   // 隠れている間は計算しない（見えたときに作り直す）

    const int scrollY = viewport.getViewPositionY();
    std::optional<collab::ProjectDiff> diff;

    if (sync.isLinked() && sync.getBase() != nullptr)
        diff = collab::diffProjects (*sync.getBase(), document.getProject());

    localCount = diff ? (int) diff->changedScopeIds.size() : 0;
    localDirty = false;
    ticksSinceRebuild = 0;

    content->setSize (viewport.getMaximumVisibleWidth() > 0 ? viewport.getMaximumVisibleWidth() : getWidth() - 10, content->getHeight());
    content->rebuild (diff ? &*diff : nullptr);
    viewport.setViewPosition (0, scrollY);
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

void SyncBadge::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &document)
        dirty = true;
    else
        repaint();
}

void SyncBadge::timerCallback()
{
    if (! dirty)
        return;

    dirty = false;
    local = 0;

    if (sync.isLinked() && sync.getBase() != nullptr)
        local = (int) collab::diffProjects (*sync.getBase(), document.getProject()).changedScopeIds.size();

    repaint();
}

void SyncBadge::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    // ほかのボタンと同じ見た目に、状態の丸と文字
    getLookAndFeel().drawButtonBackground (g, *this, findColour (juce::TextButton::buttonColourId), highlighted, down);

    auto r = getLocalBounds().toFloat().reduced (10.0f, 0.0f);
    juce::Colour dot;
    juce::String label;
    const auto st = sync.getServerStatus();
    const int incoming = (int) st.incoming.size();

    if (! sync.hasCredentials())
    {
        dot = Theme::textDim;
        label = "同期: 未設定"_ju;
    }
    else if (! sync.isLinked())
    {
        dot = Theme::textDim;
        label = "同期: 未登録"_ju;
    }
    else if (st.checked && ! st.online)
    {
        dot = Theme::danger;
        label = "同期: オフライン"_ju;
    }
    else if (incoming > 0 || local > 0)
    {
        dot = incoming > 0 ? Theme::accent : Theme::warning;
        juce::StringArray parts;

        if (incoming > 0)
            parts.add (juce::String::fromUTF8 ("\xE2\x86\x93") + juce::String (incoming) + " 取り込み"_ju);   // ↓

        if (local > 0)
            parts.add (juce::String::fromUTF8 ("\xE2\x86\x91") + juce::String (local) + " 未送信"_ju);        // ↑

        label = parts.joinIntoString ("  ");
    }
    else
    {
        dot = Theme::ok;
        label = st.checked ? "同期: 最新"_ju : "同期: 確認中"_ju;
    }

    Theme::drawStatusDot (g, r.removeFromLeft (10.0f).withSizeKeepingCentre (8.0f, 8.0f), dot);
    r.removeFromLeft (6.0f);
    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (13.0f));
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

    setSize (380, 100);
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
    setSize (getWidth(), actionLabel.isNotEmpty() ? 100 : 76);
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
    g.setColour (Theme::panelLight);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (Theme::gridBar);
    g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);

    auto area = r.reduced (14.0f, 10.0f);
    auto titleRow = area.removeFromTop (22.0f).withTrimmedRight (28.0f);
    Theme::drawStatusDot (g, titleRow.removeFromLeft (10.0f).withSizeKeepingCentre (8.0f, 8.0f), colour);
    titleRow.removeFromLeft (6.0f);
    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (14.5f, juce::Font::bold));
    g.drawText (title, titleRow, juce::Justification::centredLeft, true);

    g.setFont (juce::FontOptions (13.5f));
    g.drawFittedText (body, area.withHeight (36.0f).toNearestInt(), juce::Justification::topLeft, 2, 0.9f);
}

void SyncToast::resized()
{
    closeButton.setBounds (getWidth() - 32, 8, 24, 22);
    actionButton.setBounds (getWidth() - 124, getHeight() - 34, 110, 26);
}

void SyncToast::mouseUp (const juce::MouseEvent&)
{
    setVisible (false);
}
