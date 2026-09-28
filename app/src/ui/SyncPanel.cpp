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

    juce::Colour scopeColour (const collab::Change& c)
    {
        switch (c.scopeKind)
        {
            case collab::ScopeKind::tempo:  return Theme::tempo;
            case collab::ScopeKind::meter:  return Theme::meter;
            case collab::ScopeKind::chord:  return Theme::selection;
            case collab::ScopeKind::marker: return Theme::orange;
            case collab::ScopeKind::key:    return Theme::green;
            case collab::ScopeKind::master: return Theme::red;
            case collab::ScopeKind::track:
            default:                        return Theme::accent;
        }
    }

    /** 丸の中の矢印（↑ ↓）・錠・時計などの小さなアイコン。 */
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& kind, juce::Colour colour)
    {
        g.setColour (colour);
        g.fillEllipse (r);

        const auto c = r.getCentre();
        const float s = r.getWidth() * 0.26f;
        juce::Path p;
        const auto ink = juce::Colour (0xff15162a);

        if (kind == "down" || kind == "up")
        {
            const float dir = kind == "down" ? 1.0f : -1.0f;
            p.startNewSubPath (c.x, c.y - s * dir);
            p.lineTo (c.x, c.y + s * dir);
            p.startNewSubPath (c.x - s * 0.8f, c.y + s * 0.2f * dir);
            p.lineTo (c.x, c.y + s * dir);
            p.lineTo (c.x + s * 0.8f, c.y + s * 0.2f * dir);
            g.setColour (ink);
            g.strokePath (p, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else if (kind == "lock")
        {
            g.setColour (ink);
            g.fillRoundedRectangle (c.x - s, c.y - s * 0.1f, s * 2.0f, s * 1.3f, 2.0f);
            p.addCentredArc (c.x, c.y - s * 0.15f, s * 0.6f, s * 0.7f, 0.0f, -juce::MathConstants<float>::halfPi,
                             juce::MathConstants<float>::halfPi, true);
            g.strokePath (p, juce::PathStrokeType (1.8f));
        }
        else if (kind == "clock")
        {
            g.setColour (ink);
            g.drawEllipse (juce::Rectangle<float> (s * 2.4f, s * 2.4f).withCentre (c), 1.6f);
            p.startNewSubPath (c.x, c.y - s * 0.8f);
            p.lineTo (c.x, c.y);
            p.lineTo (c.x + s * 0.6f, c.y + s * 0.3f);
            g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else   // cloud
        {
            g.setColour (ink);
            g.fillEllipse (c.x - s * 1.1f, c.y - s * 0.3f, s * 1.2f, s * 1.0f);
            g.fillEllipse (c.x - s * 0.5f, c.y - s * 0.8f, s * 1.3f, s * 1.3f);
            g.fillEllipse (c.x + s * 0.1f, c.y - s * 0.3f, s * 1.1f, s * 1.0f);
            g.fillRect (c.x - s * 0.6f, c.y + s * 0.1f, s * 1.3f, s * 0.6f);
        }
    }
}

//==============================================================================
class SyncPanel::Content  : public juce::Component
{
public:
    explicit Content (SyncPanel& o) : owner (o)
    {
        auto setup = [this] (juce::TextButton& b, const juce::String& text, juce::Colour colour)
        {
            b.setButtonText (text);
            b.setColour (juce::TextButton::buttonColourId, colour);
            b.setColour (juce::TextButton::textColourOffId, colour.getPerceivedBrightness() > 0.55f ? juce::Colour (0xff15162a) : Theme::text);
            addChildComponent (b);
        };

        setup (pullButton, "取り込む"_ju, Theme::accent);
        setup (pushButton, "アップする"_ju, Theme::orange);
        setup (registerButton, "サーバーにアップして共有"_ju, Theme::pink);
        setup (settingsButton, "サーバー設定…"_ju, Theme::panelLight);
        setup (pickerButton, "楽曲を選ぶ…"_ju, Theme::panelLight);
        setup (refreshButton, "今すぐ確認"_ju, juce::Colours::white.withAlpha (0.22f));
        setup (historyButton, "履歴をすべて見る…"_ju, Theme::panelLight);
        refreshButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);

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

        comment.setTextToShowWhenEmpty ("何を変えたか（任意）"_ju, Theme::textDim);
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
        enum class Kind { text, dim, warning, scope, change, revision, lock };
        Kind kind = Kind::text;
        juce::Rectangle<int> r;
        juce::String a, b, who;   // who: アバターの名前（色を人ごとに固定する）
        juce::Colour colour;
        collab::Change change;
        bool clickable = false;
    };

    struct Card
    {
        juce::Rectangle<int> r;
        juce::Colour colour;
        juce::String icon, title, pill;
        bool hero = false;
    };

    void rebuild (const collab::ProjectDiff* local)
    {
        hovered = nullptr;
        items.clear();
        cards.clear();

        // 表示する部品を集めて最後にまとめて切り替える（一度隠すと入力中のコメント欄からフォーカスが外れる）
        std::set<juce::Component*> shown;

        auto& sync = owner.sync;
        const int width = juce::jmax (200, getWidth());
        const int x = 12, w = width - 24;
        int y = 10;

        auto addItem = [&] (Item::Kind kind, int height, juce::String a, juce::String b = {}, juce::Colour colour = {})
        {
            Item it;
            it.kind = kind;
            it.r = { x + 14, y, w - 28, height };
            it.a = std::move (a);
            it.b = std::move (b);
            it.colour = colour;
            items.push_back (it);
            y += height;
            return &items.back();
        };

        auto place = [&] (juce::Component& c, int height, int indent = 14)
        {
            c.setBounds (x + indent, y, w - indent * 2, height);
            shown.insert (&c);
            y += height + 6;
        };

        auto applyVisibility = [&]
        {
            for (auto* c : std::initializer_list<juce::Component*> { &pullButton, &pushButton, &registerButton, &settingsButton, &pickerButton,
                                                                     &refreshButton, &historyButton, &comment, &releaseLocks, &autoPull })
                c->setVisible (shown.count (c) > 0);
        };

        auto beginCard = [&] (juce::Colour colour, const juce::String& icon, const juce::String& title, const juce::String& pill = {})
        {
            Card c;
            c.colour = colour;
            c.icon = icon;
            c.title = title;
            c.pill = pill;
            c.r = { x, y, w, 0 };
            cards.push_back (c);
            y += 46;
        };

        auto endCard = [&]
        {
            y += 8;
            cards.back().r.setBottom (y);
            y += 12;
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
                    auto* it = addItem (Item::Kind::scope, 24, toJuce (c.scopeName), {}, scopeColour (c));
                    it->change = c;
                    it->clickable = true;
                    lastScope = c.scopeId;
                    ++lines;
                }

                auto* it = addItem (Item::Kind::change, 20, toJuce (c.summary));
                it->change = c;
                it->clickable = true;
                ++lines;
            }

            if (hidden > 0)
                addItem (Item::Kind::dim, 20, "ほか "_ju + juce::String (hidden) + " 件"_ju);
        };

        const auto& project = owner.document.getProject();

        //======================================================================
        // 上の見出し（曲名とサーバーの状況）
        {
            Card hero;
            hero.hero = true;
            hero.r = { x, y, w, 0 };
            hero.title = toJuce (project.name);
            cards.push_back (hero);
            y += 40;

            if (! sync.hasCredentials())
            {
                addItem (Item::Kind::text, 22, "サーバーにつながっていません"_ju, {}, juce::Colours::white);
                y += 6;
                place (settingsButton, 30);
            }
            else if (! sync.isLinked())
            {
                addItem (Item::Kind::text, 22, "この曲はまだサーバーにありません"_ju, {}, juce::Colours::white);
                addItem (Item::Kind::dim, 38, "アップすると、仲間がダウンロードして一緒に作業できます。"_ju, {}, juce::Colours::white.withAlpha (0.8f));
                y += 4;
                place (registerButton, 34);
                place (pickerButton, 28);
            }
            else
            {
                const auto st = sync.getServerStatus();
                juce::String line;

                if (st.checking && ! st.checked)
                    line = "サーバーを確認しています…"_ju;
                else if (! st.online)
                    line = "オフライン"_ju + (st.error.isNotEmpty() ? "（"_ju + st.error + "）"_ju : juce::String());
                else
                    line = "接続中・"_ju + ago (st.checkedAt) + "に確認"_ju;

                addItem (Item::Kind::text, 22, line, st.online ? "online" : "offline", juce::Colours::white);

                const int serverHead = st.online ? st.head : -1;
                addItem (Item::Kind::text, 26, "この PC  rev "_ju + juce::String (sync.getMeta().baseRevision),
                         serverHead >= 0 ? "サーバー  rev "_ju + juce::String (serverHead) : juce::String(), juce::Colours::white)->kind = Item::Kind::revision;
                items.back().a = "pills";
                items.back().b = juce::String (sync.getMeta().baseRevision) + "|" + juce::String (serverHead);
                y += 4;
                place (refreshButton, 26);
            }

            y += 4;
            cards.back().r.setBottom (y);
            y += 14;
        }

        if (! sync.isLinked())
        {
            applyVisibility();
            setSize (width, y + 10);
            repaint();
            return;
        }

        const auto st = sync.getServerStatus();

        //======================================================================
        // サーバーの新しい変更
        {
            const int n = (int) st.incoming.size();
            beginCard (Theme::accent, "down", "サーバーの新しい変更"_ju, n > 0 ? juce::String (n) : juce::String());

            if (n == 0)
            {
                addItem (Item::Kind::dim, 22, st.online ? "新しい変更はありません（最新です）"_ju : "サーバーを確認できません"_ju);
            }
            else
            {
                for (int i = 0; i < juce::jmin (n, 4); ++i)
                {
                    auto& r = st.incoming[(size_t) i];
                    auto* it = addItem (Item::Kind::revision, 42, "#"_ju + juce::String (r.number) + "  " + r.author + "・"_ju + ago (r.createdAt),
                                        r.message.isNotEmpty() ? r.message : "（コメントなし）"_ju);
                    it->who = r.author;
                }

                if (n > 4)
                    addItem (Item::Kind::dim, 20, "ほか "_ju + juce::String (n - 4) + " 件のリビジョン"_ju);

                if (st.preview != nullptr)
                {
                    y += 4;
                    addChanges (st.preview->diff, 16);
                }

                y += 8;
                place (pullButton, 34);
            }

            place (autoPull, 24);
            endCard();
        }

        //======================================================================
        // この PC の変更
        {
            const int n = local != nullptr ? (int) local->changedScopeIds.size() : 0;
            beginCard (Theme::orange, "up", "この PC の変更"_ju, n > 0 ? juce::String (n) : juce::String());

            if (local == nullptr || local->empty())
            {
                addItem (Item::Kind::dim, 22, "まだアップしていない変更はありません"_ju);
            }
            else
            {
                addChanges (*local, 18);

                if (! st.incoming.empty())
                {
                    y += 4;
                    addItem (Item::Kind::warning, 36, "先にサーバーの新しい変更を取り込んでからアップします"_ju);
                }

                y += 8;
                place (comment, 28);
                place (releaseLocks, 22);
                place (pushButton, 34);
            }

            endCard();
        }

        //======================================================================
        // ロック
        {
            const auto locks = sync.getLocks();
            beginCard (Theme::purple, "lock", "ロック（編集中）"_ju, locks.empty() ? juce::String() : juce::String ((int) locks.size()));

            if (locks.empty())
                addItem (Item::Kind::dim, 22, "誰もロックしていません"_ju);

            for (auto& [scopeId, info] : locks)
            {
                auto* it = addItem (Item::Kind::lock, 26, sync.scopeName (scopeId), info.displayName);
                it->colour = info.userId == sync.getMeta().userId ? Theme::green : Theme::personColour (info.displayName);
            }

            endCard();
        }

        //======================================================================
        // 履歴
        {
            beginCard (Theme::pink, "clock", "最近の履歴"_ju);

            if (st.history.empty())
                addItem (Item::Kind::dim, 22, "まだありません"_ju);

            for (int i = 0; i < juce::jmin ((int) st.history.size(), 6); ++i)
            {
                auto& r = st.history[(size_t) i];
                auto* it = addItem (Item::Kind::revision, 42, "#"_ju + juce::String (r.number) + "  " + r.author + "・"_ju + ago (r.createdAt),
                                    r.message.isNotEmpty() ? r.message : "（コメントなし）"_ju);
                it->who = r.author;

                if (r.number > sync.getMeta().baseRevision)
                    it->b << "  （未取り込み）"_ju;
            }

            y += 6;
            place (historyButton, 28);
            endCard();
        }

        applyVisibility();
        setSize (width, y + 10);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        for (auto& c : cards)
        {
            auto r = c.r.toFloat();

            if (c.hero)
            {
                g.setGradientFill (juce::ColourGradient (Theme::pink, r.getX(), r.getY(), Theme::purple.darker (0.1f), r.getRight(), r.getBottom(), false));
                g.fillRoundedRectangle (r, Theme::cornerRadius + 4.0f);

                // 水玉（カードの中だけに描く）
                {
                    juce::Graphics::ScopedSaveState save (g);
                    juce::Path clip;
                    clip.addRoundedRectangle (r, Theme::cornerRadius + 4.0f);
                    g.reduceClipRegion (clip);
                    g.setColour (juce::Colours::white.withAlpha (0.09f));
                    g.fillEllipse (r.getRight() - 70.0f, r.getY() - 20.0f, 90.0f, 90.0f);
                    g.fillEllipse (r.getRight() - 130.0f, r.getBottom() - 30.0f, 50.0f, 50.0f);
                }

                g.setColour (juce::Colours::white);
                g.setFont (juce::FontOptions (19.0f, juce::Font::bold));
                g.drawText (c.title, r.reduced (14.0f, 0.0f).withTop (r.getY() + 8.0f).withHeight (28.0f), juce::Justification::centredLeft, true);
                continue;
            }

            Theme::drawCard (g, r, c.colour);
            drawIcon (g, { r.getX() + 12.0f, r.getY() + 14.0f, 24.0f, 24.0f }, c.icon, c.colour);

            g.setColour (Theme::text);
            g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
            g.drawText (c.title, juce::Rectangle<float> (r.getX() + 44.0f, r.getY() + 12.0f, r.getWidth() - 100.0f, 28.0f),
                        juce::Justification::centredLeft, true);

            if (c.pill.isNotEmpty())
                Theme::drawPill (g, { r.getRight() - 48.0f, r.getY() + 16.0f, 36.0f, 20.0f }, c.colour, c.pill, 12.5f);
        }

        for (auto& it : items)
        {
            auto r = it.r.toFloat();
            const bool hover = it.clickable && hovered == &it;

            if (hover)
            {
                g.setColour (Theme::accent.withAlpha (0.12f));
                g.fillRoundedRectangle (r.expanded (4.0f, 0.0f), 4.0f);
            }

            switch (it.kind)
            {
                case Item::Kind::text:
                {
                    auto area = r;

                    if (it.b == "online" || it.b == "offline")
                    {
                        const auto dot = area.removeFromLeft (12.0f).withSizeKeepingCentre (10.0f, 10.0f);
                        g.setColour (it.b == "online" ? Theme::green : Theme::red);
                        g.fillEllipse (dot);
                        g.setColour (juce::Colours::white);
                        g.drawEllipse (dot, 1.2f);
                        area.removeFromLeft (6.0f);
                    }

                    g.setColour (it.colour.isTransparent() ? Theme::text : it.colour);
                    g.setFont (juce::FontOptions (14.0f));
                    g.drawText (it.a, area, juce::Justification::centredLeft, true);
                    break;
                }

                case Item::Kind::dim:
                case Item::Kind::warning:
                {
                    if (it.kind == Item::Kind::warning)
                    {
                        g.setColour (Theme::warning.withAlpha (0.15f));
                        g.fillRoundedRectangle (r.expanded (4.0f, 0.0f), 6.0f);
                    }

                    g.setColour (it.kind == Item::Kind::warning ? Theme::warning
                                                               : (it.colour.isTransparent() ? Theme::textDim : it.colour));
                    g.setFont (juce::FontOptions (13.0f));
                    g.drawFittedText (it.a, r.reduced (it.kind == Item::Kind::warning ? 6.0f : 0.0f, 0.0f).toNearestInt(),
                                      juce::Justification::centredLeft, 2, 0.9f);
                    break;
                }

                case Item::Kind::scope:
                    g.setColour (it.colour);
                    g.fillRoundedRectangle (r.getX(), r.getCentreY() - 5.0f, 10.0f, 10.0f, 3.0f);
                    g.setColour (Theme::text);
                    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
                    g.drawText (it.a, r.withTrimmedLeft (16.0f), juce::Justification::centredLeft, true);
                    break;

                case Item::Kind::change:
                    g.setColour (hover ? Theme::text : Theme::textDim);
                    g.setFont (juce::FontOptions (13.0f));
                    g.drawText (it.a, r.withTrimmedLeft (16.0f), juce::Justification::centredLeft, true);
                    break;

                case Item::Kind::revision:
                {
                    if (it.a == "pills")
                    {
                        // 「この PC rev 10」「サーバー rev 12」の 2 つの丸いバッジ
                        const auto parts = juce::StringArray::fromTokens (it.b, "|", {});
                        const int localRev = parts[0].getIntValue(), serverRev = parts[1].getIntValue();
                        auto area = r;
                        Theme::drawPill (g, area.removeFromLeft (120.0f).reduced (0.0f, 2.0f), juce::Colours::white.withAlpha (0.9f),
                                         "この PC  rev "_ju + juce::String (localRev), 12.0f);
                        area.removeFromLeft (8.0f);

                        if (serverRev >= 0)
                            Theme::drawPill (g, area.removeFromLeft (130.0f).reduced (0.0f, 2.0f),
                                             serverRev > localRev ? Theme::selection : juce::Colours::white.withAlpha (0.9f),
                                             "サーバー  rev "_ju + juce::String (serverRev), 12.0f);
                        break;
                    }

                    Theme::drawAvatar (g, { r.getX(), r.getY() + 5.0f, 26.0f, 26.0f }, it.who);
                    g.setColour (Theme::text);
                    g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
                    g.drawText (it.a, r.withTrimmedLeft (34.0f).withHeight (20.0f).translated (0.0f, 2.0f), juce::Justification::centredLeft, true);
                    g.setColour (Theme::textDim);
                    g.setFont (juce::FontOptions (13.0f));
                    g.drawText (it.b, r.withTrimmedLeft (34.0f).withTrimmedTop (20.0f), juce::Justification::centredLeft, true);
                    break;
                }

                case Item::Kind::lock:
                {
                    auto area = r;
                    g.setColour (Theme::text);
                    g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
                    g.drawText (it.a, area.removeFromLeft (area.getWidth() * 0.55f), juce::Justification::centredLeft, true);
                    Theme::drawAvatar (g, area.removeFromLeft (20.0f).withSizeKeepingCentre (20.0f, 20.0f), it.b);
                    area.removeFromLeft (6.0f);
                    g.setColour (it.colour);
                    g.setFont (juce::FontOptions (13.0f));
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
    std::vector<Card> cards;
    const Item* hovered = nullptr;
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
    g.fillAll (Theme::background);
    g.setColour (Theme::panelLight);
    g.fillRect (0, 0, 1, getHeight());

    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("同期"_ju, 14, 0, 100, 30, juce::Justification::centredLeft);
}

void SyncPanel::resized()
{
    closeButton.setBounds (getWidth() - 34, 3, 28, 24);
    viewport.setBounds (getLocalBounds().withTrimmedTop (30).withTrimmedLeft (1));
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

void SyncPanel::visibilityChanged()
{
    if (isVisible())
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

    // 入力中のコメントは残す
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
    auto r = getLocalBounds().toFloat().reduced (1.0f, 2.0f);
    juce::Colour colour;
    juce::String label;
    const auto st = sync.getServerStatus();
    const int incoming = (int) st.incoming.size();

    if (! sync.hasCredentials())
    {
        colour = Theme::panelLight;
        label = "サーバー未設定"_ju;
    }
    else if (! sync.isLinked())
    {
        colour = Theme::pink;
        label = "サーバーにアップ"_ju;
    }
    else if (st.checked && ! st.online)
    {
        colour = Theme::red;
        label = "オフライン"_ju;
    }
    else if (incoming > 0 || local > 0)
    {
        colour = incoming > 0 ? Theme::accent : Theme::orange;
        label = {};

        if (incoming > 0)
            label << juce::String::fromUTF8 ("\xE2\x86\x93 ") << incoming << " "_ju;   // ↓

        if (local > 0)
            label << juce::String::fromUTF8 ("\xE2\x86\x91 ") << local;                // ↑

        label = label.trim();
    }
    else
    {
        colour = Theme::green;
        label = st.checked ? "最新"_ju : "確認中…"_ju;
    }

    if (down)
        colour = colour.darker (0.2f);
    else if (highlighted)
        colour = colour.brighter (0.15f);

    g.setColour (colour);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);

    const auto ink = colour.getPerceivedBrightness() > 0.55f ? juce::Colour (0xff15162a) : Theme::text;
    auto iconArea = r.removeFromLeft (r.getHeight()).reduced (5.0f);

    // 雲のアイコン
    g.setColour (ink);
    const auto c = iconArea.getCentre();
    const float s = iconArea.getWidth() * 0.28f;
    g.fillEllipse (c.x - s * 1.4f, c.y - s * 0.3f, s * 1.4f, s * 1.2f);
    g.fillEllipse (c.x - s * 0.7f, c.y - s * 1.0f, s * 1.6f, s * 1.6f);
    g.fillEllipse (c.x + s * 0.1f, c.y - s * 0.3f, s * 1.3f, s * 1.2f);
    g.fillRect (c.x - s * 0.8f, c.y + s * 0.2f, s * 1.7f, s * 0.7f);

    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (label, r.withTrimmedRight (10.0f), juce::Justification::centred, true);
}

//==============================================================================
SyncToast::SyncToast()
{
    actionButton.setColour (juce::TextButton::buttonColourId, juce::Colours::white);
    actionButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff15162a));
    actionButton.onClick = [this]
    {
        setVisible (false);

        if (action)
            action();
    };
    addAndMakeVisible (actionButton);

    closeButton.setButtonText (juce::String::fromUTF8 ("\xC3\x97"));
    closeButton.setColour (juce::TextButton::buttonColourId, juce::Colours::white.withAlpha (0.2f));
    closeButton.onClick = [this] { setVisible (false); };
    addAndMakeVisible (closeButton);

    setSize (380, 104);
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
    auto r = getLocalBounds().toFloat().reduced (4.0f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r.translated (0.0f, 3.0f), 14.0f);
    g.setGradientFill (juce::ColourGradient (colour, r.getX(), r.getY(), colour.withRotatedHue (0.08f).darker (0.15f), r.getRight(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 14.0f);

    const auto ink = colour.getPerceivedBrightness() > 0.55f ? juce::Colour (0xff15162a) : juce::Colours::white;
    g.setColour (ink);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText (title, r.reduced (16.0f, 0.0f).withTop (r.getY() + 10.0f).withHeight (22.0f).withTrimmedRight (30.0f),
                juce::Justification::centredLeft, true);
    g.setFont (juce::FontOptions (13.0f));
    g.drawFittedText (body, r.reduced (16.0f, 0.0f).withTop (r.getY() + 34.0f).withHeight (36.0f).toNearestInt(),
                      juce::Justification::topLeft, 2, 0.9f);
}

void SyncToast::resized()
{
    auto r = getLocalBounds().reduced (4);
    closeButton.setBounds (r.getRight() - 32, r.getY() + 8, 24, 22);
    actionButton.setBounds (r.getRight() - 124, r.getBottom() - 34, 110, 26);
}

void SyncToast::mouseUp (const juce::MouseEvent&)
{
    setVisible (false);
}
