#include "TrackHeader.h"

#include "Dialogs.h"
#include "InstrumentPanel.h"
#include "Theme.h"
#include "ValueText.h"
#include "sync/SyncManager.h"
#include "collab/Render.h"
#include "plugins/PluginHost.h"

TrackHeader::TrackHeader (AppContext& c, const std::string& id)
    : ctx (c), trackId (id)
{
    nameLabel.setEditable (false, true);
    nameLabel.setFont (juce::FontOptions (15.5f, juce::Font::bold));
    nameLabel.setColour (juce::Label::textColourId, Theme::text);
    nameLabel.onTextChange = [this]
    {
        auto name = nameLabel.getText().trim();

        if (name.isNotEmpty())
            editTrack ("トラック名の変更"_ju, [name] (collab::Track& t) { t.name = toStd (name); });
        else
            update();
    };
    addAndMakeVisible (nameLabel);
    nameLabel.setInterceptsMouseClicks (false, false);   // 名前の上でも選択・ドラッグ（並べ替え）・右クリックが効くように

    // 録音待機（オーディオトラックと MIDI トラック）
    armButton.setButtonText ("●"_ju);
    armButton.setWantsKeyboardFocus (false);
    armButton.setClickingTogglesState (false);
    armButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe57373));
    armButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffc62828));
    armButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    armButton.onClick = [this]
    {
        select();
        ctx.toggleRecordArm (trackId);
    };
    addChildComponent (armButton);

    for (auto* b : { &muteButton, &soloButton })
    {
        b->setClickingTogglesState (false);
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    muteButton.setTooltip ("ミュート"_ju);
    soloButton.setTooltip ("ソロ"_ju);
    muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe57373));
    soloButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffffd54f).darker (0.2f));
    muteButton.onClick = [this] { editTrack ("ミュート"_ju, [] (collab::Track& t) { t.mute = ! t.mute; }); };
    soloButton.onClick = [this] { editTrack ("ソロ"_ju, [] (collab::Track& t) { t.solo = ! t.solo; }); };

    update();
}

void TrackHeader::editTrack (const juce::String& description, std::function<void (collab::Track&)> fn, const juce::String& mergeId)
{
    ctx.editTrack (trackId, description, std::move (fn), mergeId);
}

void TrackHeader::update()
{
    auto* t = ctx.document.getProject().findTrack (trackId);

    if (t == nullptr)
        return;

    if (badgeShown != (ctx.sync.isLinked() && getWidth() > 130))
        resized();

    nameLabel.setText (toJuce (t->name), juce::dontSendNotification);
    muteButton.setToggleState (t->mute, juce::dontSendNotification);
    soloButton.setToggleState (t->solo, juce::dontSendNotification);
    if (t->type == collab::TrackType::audio)
        armButton.setToggleState (ctx.engine.getTrackInput (trackId).armed, juce::dontSendNotification);
    else if (t->type == collab::TrackType::midi)
        armButton.setToggleState (ctx.state.midiArmedTrackId == trackId, juce::dontSendNotification);

    armButton.setTooltip ("録音待機（R）"_ju);

    const bool canArm = t->type == collab::TrackType::audio || t->type == collab::TrackType::midi;

    if (armButton.isVisible() != canArm)
    {
        armButton.setVisible (canArm);
        resized();
    }
    // 音源が読めないなど: 名前を橙にして、マウスを乗せると理由を出す
    problem = ctx.engine.getInstrumentProblem (trackId);
    nameLabel.setColour (juce::Label::textColourId, problem.isEmpty() ? Theme::text : Theme::warning);
    setTooltip (problem);

    repaint();
}

void TrackHeader::paint (juce::Graphics& g)
{
    auto* t = ctx.document.getProject().findTrack (trackId);
    const bool selected = ctx.state.selectedTrackId == trackId;

    g.fillAll (Theme::panel);

    // 不透明のカード（選択中は明るく。ガラスにはしない: 内容の面なので読みやすさを優先）
    const auto card = getLocalBounds().toFloat().reduced (3.0f, 2.0f);
    g.setColour (selected ? Theme::panelLight.brighter (0.12f) : Theme::panelLight.darker (0.08f));
    g.fillRoundedRectangle (card, 6.0f);

    if (selected)
    {
        g.setColour (Theme::text.withAlpha (0.55f));
        g.drawRoundedRectangle (card.reduced (0.5f), 6.0f, 1.0f);
    }

    // 他の人がアップして新しくなったトラックは青、競合は橙をうっすら重ねて、ダウンロードを促す
    if (ctx.sync.isLinked())
    {
        const auto st = ctx.sync.scopeState (trackId);

        if (st.conflict || st.theirs)
        {
            g.setColour ((st.conflict ? Theme::warning : Theme::accent).withAlpha (0.16f));
            g.fillRoundedRectangle (card, 7.0f);
        }
    }

    if (t != nullptr)
    {
        // トラックの色: カードの左に丸い棒
        g.setColour (Theme::parseColour (t->color));
        g.fillRoundedRectangle (card.withWidth (5.0f).reduced (0.0f, 4.0f).translated (4.0f, 0.0f), 2.5f);
    }

    g.setColour (Theme::background);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    // 同期: この PC の変更・サーバーで更新・競合（名前の右に小さな文字で）
    if (ctx.sync.isLinked() && ! badgeArea.isEmpty())
    {
        const auto st = ctx.sync.scopeState (trackId);
        juce::String label;
        auto colour = Theme::textDim;

        if (st.conflict)          { label = "競合"_ju;   colour = Theme::warning; }
        else if (st.theirs)       { label = "新着"_ju;   colour = Theme::accent; }
        else if (st.mine)         { label = "変更"_ju;   colour = Theme::text; }

        if (label.isNotEmpty())
        {
            auto r = badgeArea.toFloat();
            Theme::drawStatusDot (g, r.removeFromLeft (8.0f).withSizeKeepingCentre (7.0f, 7.0f), colour);
            r.removeFromLeft (4.0f);
            g.setColour (colour);
            g.setFont (juce::FontOptions (15.0f));
            g.drawText (label, r, juce::Justification::centredLeft, true);
        }
    }

    // バウンスの状態（§3.7）
    if (t != nullptr)
    {
        juce::String renderBadge;
        bool done = false;

        if (ctx.engine.isPlayingRender (trackId))
            renderBadge = "バウンス音で再生"_ju;
        else
            switch (collab::renderStatus (*t, ctx.fingerprint (*t)))
            {
                case collab::RenderStatus::missing:  renderBadge = "要バウンス"_ju; break;
                case collab::RenderStatus::stale:    renderBadge = "バウンスが古い"_ju; break;
                case collab::RenderStatus::upToDate: renderBadge = "バウンス済み"_ju; done = true; break;
                case collab::RenderStatus::notNeeded: break;
            }

        if (renderBadge.isNotEmpty())
        {
            g.setColour (done ? Theme::textDim : Theme::warning);
            g.setFont (juce::FontOptions (13.5f));
            g.drawText (renderBadge, getLocalBounds().withTrimmedLeft (10).withTrimmedRight (10).removeFromBottom (32).removeFromTop (12),
                        juce::Justification::centredRight, true);
        }
    }

}

void TrackHeader::resized()
{
    // 名前と ●・M・S だけ（音源・入出力・音量・パンは左のインスペクター）
    auto area = getLocalBounds().reduced (10, 4).withTrimmedLeft (5);   // 左はトラックの色の棒
    auto top = area.removeFromTop (juce::jmin (22, area.getHeight()));
    soloButton.setBounds (top.removeFromRight (24));
    top.removeFromRight (3);
    muteButton.setBounds (top.removeFromRight (24));
    top.removeFromRight (3);

    if (armButton.isVisible())
    {
        armButton.setBounds (top.removeFromRight (24));
        top.removeFromRight (4);
    }

    // 同期の印（名前の右）
    badgeShown = ctx.sync.isLinked() && top.getWidth() > 110;
    badgeArea = badgeShown ? top.removeFromRight (juce::jmin (70, top.getWidth() / 2)).reduced (0, 2) : juce::Rectangle<int>();

    if (badgeShown)
        top.removeFromRight (4);

    nameLabel.setBounds (top);
}

void TrackHeader::select()
{
    if (ctx.state.selectedTrackId != trackId)
    {
        ctx.state.selectedTrackId = trackId;
        ctx.state.changed();
    }
}

void TrackHeader::mouseDown (const juce::MouseEvent& e)
{
    select();
    drag = Drag::none;

    if (e.mods.isPopupMenu())
        return showMenu();

    const auto p = e.getEventRelativeTo (this).getPosition();
    dragStartHeight = getHeight();
    dragStartY = getY();
    drag = p.y >= getHeight() - resizeEdge ? Drag::resize : Drag::pending;
}

void TrackHeader::mouseDrag (const juce::MouseEvent& e)
{
    const int dy = e.getDistanceFromDragStartY();

    if (drag == Drag::resize)
    {
        // 段階式（Cubase のトラックの高さと同じく、決まった高さに吸い付く）
        const int wanted = dragStartHeight + dy;
        int h = EditorState::trackHeightSteps[0];

        for (int step : EditorState::trackHeightSteps)
            if (std::abs (step - wanted) < std::abs (h - wanted))
                h = step;

        if (h != ctx.state.trackHeight (trackId))
        {
            ctx.state.trackHeights[trackId] = h;
            ctx.state.changed();
        }

        return;
    }

    if (drag == Drag::pending && std::abs (dy) > 6 && ! nameLabel.isBeingEdited())
    {
        drag = Drag::reorder;
        toFront (false);
        setAlpha (0.85f);
    }

    if (drag == Drag::reorder)
        setTopLeftPosition (getX(), dragStartY + dy);
}

void TrackHeader::mouseUp (const juce::MouseEvent&)
{
    const auto was = drag;
    drag = Drag::none;
    setAlpha (1.0f);

    if (was == Drag::reorder && onReorderDrop)
        onReorderDrop (trackId, getY() + getHeight() / 2);
}

void TrackHeader::mouseDoubleClick (const juce::MouseEvent& e)
{
    // 名前をダブルクリックで名前の変更
    if (nameLabel.getBounds().contains (e.getEventRelativeTo (this).getPosition()))
        nameLabel.showEditor();
}

void TrackHeader::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.getEventRelativeTo (this).getPosition();
    setMouseCursor (p.y >= getHeight() - resizeEdge ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
}

void TrackHeader::mouseExit (const juce::MouseEvent&)
{
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void TrackHeader::showMenu()
{
    const auto& project = ctx.document.getProject();
    const int index = project.indexOfTrack (trackId);

    juce::PopupMenu colours;

    for (int i = 0; i < 10; ++i)
    {
        auto hex = Theme::trackColourHex (i);
        colours.addColouredItem (100 + i, hex, Theme::trackColour (i), true, false, nullptr);
    }

    juce::PopupMenu m;

    if (ctx.addTrackMenu)
    {
        m.addSubMenu ("トラックを追加"_ju, ctx.addTrackMenu());
        m.addSeparator();
    }

    m.addItem ("名前の変更…"_ju, [this]
    {
        nameLabel.showEditor();
    });
    m.addSubMenu ("色"_ju, colours);
    m.addSeparator();
    m.addItem ("上へ移動"_ju, index > 0, false, [this]
    {
        auto id = trackId;
        ctx.document.perform ("トラックの移動"_ju, [id] (collab::Project& p)
        {
            const int i = p.indexOfTrack (id);
            if (i > 0) std::swap (p.tracks[(size_t) i], p.tracks[(size_t) i - 1]);
        });
    });
    m.addItem ("下へ移動"_ju, index >= 0 && index + 1 < (int) project.tracks.size(), false, [this]
    {
        auto id = trackId;
        ctx.document.perform ("トラックの移動"_ju, [id] (collab::Project& p)
        {
            const int i = p.indexOfTrack (id);
            if (i >= 0 && i + 1 < (int) p.tracks.size()) std::swap (p.tracks[(size_t) i], p.tracks[(size_t) i + 1]);
        });
    });
    // エフェクト（外部プラグイン）
    if (auto* t = ctx.document.getProject().findTrack (trackId))
    {
        juce::PopupMenu fx;

        for (auto& e : t->effects)
        {
            juce::PopupMenu one;
            const auto effectId = e.id;
            one.addItem ("画面を開く"_ju, [this, effectId] { if (ctx.openPluginEditor) ctx.openPluginEditor (trackId, effectId); });
            one.addItem ("バイパス"_ju, true, e.bypass, [this, effectId] { ctx.toggleEffectBypass (trackId, effectId); });
            one.addItem ("削除"_ju, [this, effectId] { ctx.removeEffect (trackId, effectId); });
            fx.addSubMenu (AppContext::effectName (e), one);
        }

        fx.addSeparator();
        fx.addSubMenu ("追加"_ju, ctx.addEffectMenu (trackId));
        m.addSeparator();
        m.addItem ("EQ…"_ju, [this] { if (ctx.openChannelStrip) ctx.openChannelStrip (trackId, false); });
        m.addItem ("Compressor…"_ju, [this] { if (ctx.openChannelStrip) ctx.openChannelStrip (trackId, true); });

        m.addSubMenu ("出力先・センド"_ju, ctx.routingMenu (trackId));

        if (t->type == collab::TrackType::audio)
            m.addSubMenu ("テイクのつなぎ（クロスフェード）"_ju, ctx.crossfadeMenu (trackId));
        m.addSubMenu ("エフェクト"_ju, fx);

        if (t->type == collab::TrackType::midi || ! t->effects.empty())
            m.addItem ("バウンス（オーディオに書き出す）"_ju, [this] { ctx.bounceTrack (trackId); });
    }

    m.addSeparator();
    m.addItem ("トラックを削除"_ju, [this]
    {
        auto id = trackId;
        auto* t = ctx.document.getProject().findTrack (id);
        auto name = t != nullptr ? toJuce (t->name) : juce::String();

        Dialogs::confirm ("トラックの削除"_ju, "「"_ju + name + "」を削除しますか？（元に戻すで復元できます）"_ju, "削除"_ju, [c = &ctx, id]
        {
            c->document.perform ("トラックの削除"_ju, [id] (collab::Project& p)
            {
                p.tracks.erase (std::remove_if (p.tracks.begin(), p.tracks.end(), [&] (auto& x) { return x.id == id; }),
                                p.tracks.end());
            });
        });
    });

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int result)
    {
        if (result >= 100 && result < 110)
        {
            auto hex = toStd (Theme::trackColourHex (result - 100));
            editTrack ("トラックの色"_ju, [hex] (collab::Track& t) { t.color = hex; });
        }
    });
}
