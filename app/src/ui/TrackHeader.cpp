#include "TrackHeader.h"

#include "Dialogs.h"
#include "InstrumentPanel.h"
#include "Theme.h"
#include "sync/SyncManager.h"
#include "collab/Render.h"
#include "plugins/PluginHost.h"

TrackHeader::TrackHeader (AppContext& c, const std::string& id)
    : ctx (c), trackId (id)
{
    nameLabel.setEditable (false, true);
    nameLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
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

    instrumentButton.setTooltip ("音源の調整"_ju);
    instrumentButton.onClick = [this]
    {
        select();

        if (isAudioTrack())
            showInputMenu();
        else
            showInstrumentMenu();
    };

    // 録音待機（オーディオトラックのみ）
    armButton.setButtonText ("●"_ju);
    armButton.setTooltip ("録音待機（入力はその下のボタンで選ぶ）"_ju);
    armButton.setClickingTogglesState (false);
    armButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe57373));
    armButton.setColour (juce::TextButton::buttonOnColourId, Theme::red);
    armButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    armButton.onClick = [this]
    {
        select();
        auto in = ctx.engine.getTrackInput (trackId);

        if (in.device.isEmpty())
        {
            const auto inputs = ctx.engine.getAudioInputs();

            if (inputs.isEmpty())
                return Dialogs::showInfo ("録音待機"_ju, "録音できる入力がありません。オーディオ設定で入力デバイスを選んでください。"_ju);

            in.device = inputs[0];
        }

        in.armed = ! in.armed;
        ctx.engine.setTrackInput (trackId, in);
        ctx.state.changed();   // 他のトラックの表示も更新（入力は 1 つのトラックにだけ割り当てる）
    };
    addChildComponent (armButton);
    addAndMakeVisible (instrumentButton);

    for (auto* b : { &muteButton, &soloButton })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (b);
    }

    muteButton.setTooltip ("ミュート"_ju);
    soloButton.setTooltip ("ソロ"_ju);
    muteButton.setColour (juce::TextButton::buttonOnColourId, Theme::orange);
    muteButton.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff15162a));
    soloButton.setColour (juce::TextButton::buttonOnColourId, Theme::selection);
    soloButton.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff15162a));
    muteButton.onClick = [this] { editTrack ("ミュート"_ju, [] (collab::Track& t) { t.mute = ! t.mute; }); };
    soloButton.onClick = [this] { editTrack ("ソロ"_ju, [] (collab::Track& t) { t.solo = ! t.solo; }); };

    volumeSlider.setRange (-60.0, 6.0, 0.1);
    volumeSlider.setSkewFactorFromMidPoint (-12.0);
    volumeSlider.setDoubleClickReturnValue (true, 0.0);
    volumeSlider.setTextValueSuffix (" dB");
    volumeSlider.setPopupDisplayEnabled (true, true, nullptr);
    volumeSlider.setTooltip ("音量（ダブルクリックで 0 dB）"_ju);

    panSlider.setRange (-1.0, 1.0, 0.01);
    panSlider.setDoubleClickReturnValue (true, 0.0);
    panSlider.setPopupDisplayEnabled (true, true, nullptr);
    panSlider.setTooltip ("パン（ダブルクリックで中央）"_ju);

    for (auto* s : { &volumeSlider, &panSlider })
    {
        s->onDragStart = [this] { dragMergeId = juce::Uuid().toString(); };
        s->onDragEnd = [this] { ctx.document.endMerge(); dragMergeId = {}; };
        addAndMakeVisible (s);
    }

    volumeSlider.onValueChange = [this]
    {
        const double v = volumeSlider.getValue();
        editTrack ("音量"_ju, [v] (collab::Track& t) { t.volumeDb = v; }, dragMergeId);
    };

    panSlider.onValueChange = [this]
    {
        const double v = panSlider.getValue();
        editTrack ("パン"_ju, [v] (collab::Track& t) { t.pan = v; }, dragMergeId);
    };

    update();
}

void TrackHeader::editTrack (const juce::String& description, std::function<void (collab::Track&)> fn, const juce::String& mergeId)
{
    auto id = trackId;
    ctx.document.perform (description, [id, fn] (collab::Project& p)
    {
        if (auto* t = p.findTrack (id))
            fn (*t);
    }, mergeId);
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
    volumeSlider.setValue (t->volumeDb, juce::dontSendNotification);
    panSlider.setValue (t->pan, juce::dontSendNotification);

    juce::String instName = t->type == collab::TrackType::audio ? "オーディオ"_ju
                          : t->type == collab::TrackType::bus ? "バス（出力: "_ju + ctx.outputName (*t) + "）"_ju
                                                              : "音源なし"_ju;

    if (t->instrument)
    {
        if (t->instrument->kind == collab::Instrument::Kind::builtin)
        {
            auto* m = ctx.library.find (t->instrument->id, t->instrument->version);
            instName = m != nullptr ? toJuce (m->displayName) : toJuce (t->instrument->id);
        }
        else
        {
            instName = toJuce (t->instrument->plugin.name);
        }
    }

    if (t->type == collab::TrackType::audio)
    {
        const auto in = ctx.engine.getTrackInput (trackId);
        instName = in.device.isEmpty() ? "入力: なし"_ju : "入力: "_ju + in.device + (in.monitor ? "（モニター）"_ju : juce::String());
        armButton.setToggleState (in.armed, juce::dontSendNotification);
    }

    if (armButton.isVisible() != (t->type == collab::TrackType::audio))
    {
        armButton.setVisible (t->type == collab::TrackType::audio);
        resized();
    }
    instrumentButton.setButtonText (instName);
    instrumentButton.setTooltip (t->type == collab::TrackType::audio ? "録音の入力とモニタリング"_ju : "音源の調整"_ju);
    problem = ctx.engine.getInstrumentProblem (trackId);
    instrumentButton.setColour (juce::TextButton::textColourOffId, problem.isEmpty() ? Theme::text : Theme::warning);

    if (problem.isNotEmpty())
        instrumentButton.setTooltip (problem);

    repaint();
}

void TrackHeader::paint (juce::Graphics& g)
{
    auto* t = ctx.document.getProject().findTrack (trackId);
    const bool selected = ctx.state.selectedTrackId == trackId;

    g.fillAll (selected ? Theme::panelLight : Theme::panel);

    if (t != nullptr)
    {
        // トラックの色をうっすら重ねて、左に太めの色の帯
        const auto colour = Theme::parseColour (t->color);
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (selected ? 0.28f : 0.16f), 0.0f, 0.0f,
                                                 colour.withAlpha (0.0f), (float) getWidth() * 0.7f, 0.0f, false));
        g.fillAll();
        g.setColour (colour);
        g.fillRoundedRectangle (1.0f, 2.0f, 5.0f, (float) getHeight() - 4.0f, 2.5f);
    }

    g.setColour (Theme::background);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    // 同期中: ロックと未 push の変更（§4.2, §4.4）。名前の右の小さな丸いバッジ
    if (ctx.sync.isLinked() && ! badgeArea.isEmpty())
    {
        juce::String label;
        auto colour = Theme::panelLight;

        if (ctx.sync.isLockedByMe (trackId))             { label = "自分"_ju; colour = Theme::green; }
        else if (auto lock = ctx.sync.getLock (trackId)) { label = lock->displayName; colour = Theme::personColour (lock->displayName); }
        else if (! ctx.sync.canEdit (trackId))           { label = "閲覧"_ju; }

        const bool unpushed = ctx.sync.hasLocalChanges (trackId);
        auto r = badgeArea.toFloat().withSizeKeepingCentre ((float) badgeArea.getWidth(), 16.0f);

        if (label.isNotEmpty())
        {
            // 錠の印つきのバッジ
            g.setColour (colour);
            g.fillRoundedRectangle (r, 8.0f);
            const auto ink = colour.getPerceivedBrightness() > 0.55f ? juce::Colour (0xff15162a) : Theme::text;
            g.setColour (ink);
            auto lockIcon = r.removeFromLeft (14.0f).reduced (3.5f, 3.0f).translated (2.0f, 0.0f);
            g.fillRoundedRectangle (lockIcon.withTrimmedTop (lockIcon.getHeight() * 0.45f), 1.5f);
            juce::Path arc;
            arc.addCentredArc (lockIcon.getCentreX(), lockIcon.getY() + lockIcon.getHeight() * 0.45f, lockIcon.getWidth() * 0.32f,
                               lockIcon.getHeight() * 0.35f, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
            g.strokePath (arc, juce::PathStrokeType (1.3f));
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (label, r.withTrimmedRight (unpushed ? 12.0f : 4.0f), juce::Justification::centred, true);
        }

        if (unpushed)
        {
            // 未送信の変更: オレンジの点
            g.setColour (Theme::orange);
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ badgeArea.toFloat().getRight() - 6.0f, badgeArea.toFloat().getCentreY() }));
        }
    }

    // バウンスの状態（§3.7）
    if (t != nullptr)
    {
        juce::String renderBadge;

        if (ctx.engine.isPlayingRender (trackId))
            renderBadge = "バウンス音で再生"_ju;
        else
            switch (collab::renderStatus (*t, ctx.fingerprint (*t)))
            {
                case collab::RenderStatus::missing:  renderBadge = "要バウンス"_ju; break;
                case collab::RenderStatus::stale:    renderBadge = "バウンスが古い"_ju; break;
                case collab::RenderStatus::notNeeded:
                case collab::RenderStatus::upToDate: break;
            }

        if (renderBadge.isNotEmpty())
        {
            g.setColour (Theme::warning);
            g.setFont (juce::FontOptions (10.5f));
            g.drawText (renderBadge, getLocalBounds().withTrimmedLeft (10).withTrimmedRight (10).removeFromBottom (32).removeFromTop (12),
                        juce::Justification::centredRight, true);
        }
    }

    if (! volumeSlider.isVisible())
        return;

    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (11.0f));
    auto area = getLocalBounds().reduced (10, 4);
    auto sliderRow = area.removeFromBottom (18);
    g.drawText ("Vol", sliderRow.removeFromLeft (26), juce::Justification::centredLeft);
    sliderRow.removeFromLeft ((int) (sliderRow.getWidth() * 0.62f));
    g.drawText ("Pan", sliderRow.removeFromLeft (28), juce::Justification::centredLeft);
}

void TrackHeader::resized()
{
    // 低くしたときは、音量・パン → 音源のボタンの順に隠す
    auto area = getLocalBounds().reduced (10, 4);
    const bool showInstrument = getHeight() >= 52;
    const bool showSliders = getHeight() >= 68;

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

    // 同期中はロックのバッジの場所を空ける
    badgeShown = ctx.sync.isLinked() && top.getWidth() > 110;
    badgeArea = badgeShown ? top.removeFromRight (juce::jmin (62, top.getWidth() / 2)).reduced (0, 2) : juce::Rectangle<int>();

    if (badgeShown)
        top.removeFromRight (4);

    nameLabel.setBounds (top);

    instrumentButton.setVisible (showInstrument);
    volumeSlider.setVisible (showSliders);
    panSlider.setVisible (showSliders);

    area.removeFromTop (3);

    if (showInstrument)
        instrumentButton.setBounds (area.removeFromTop (20));

    if (showSliders)
    {
        auto sliderRow = area.removeFromBottom (18);
        sliderRow.removeFromLeft (26);
        auto volArea = sliderRow.removeFromLeft ((int) (sliderRow.getWidth() * 0.62f));
        volumeSlider.setBounds (volArea);
        sliderRow.removeFromLeft (28);
        panSlider.setBounds (sliderRow);
    }
}

bool TrackHeader::isAudioTrack() const
{
    auto* t = ctx.document.getProject().findTrack (trackId);
    return t != nullptr && t->type == collab::TrackType::audio;
}

void TrackHeader::showInputMenu()
{
    const auto current = ctx.engine.getTrackInput (trackId);
    juce::PopupMenu m;

    m.addItem ("なし"_ju, true, current.device.isEmpty(), [this]
    {
        ctx.engine.setTrackInput (trackId, {});
        ctx.state.changed();
    });

    for (auto& name : ctx.engine.getAudioInputs())
        m.addItem (name, true, current.device == name, [this, name]
        {
            auto in = ctx.engine.getTrackInput (trackId);
            in.device = name;
            ctx.engine.setTrackInput (trackId, in);
            ctx.state.changed();
        });

    m.addSeparator();
    m.addItem ("ソフトウェアモニタリング（入力の音をこのトラックで鳴らす）"_ju, current.device.isNotEmpty(), current.monitor, [this]
    {
        auto in = ctx.engine.getTrackInput (trackId);
        in.monitor = ! in.monitor;
        ctx.engine.setTrackInput (trackId, in);
        ctx.state.changed();
    });
    m.addItem ("（オーディオインターフェースのダイレクトモニタリングがおすすめです）"_ju, false, false, nullptr);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&instrumentButton));
}

void TrackHeader::showInstrumentMenu()
{
    auto* t = ctx.document.getProject().findTrack (trackId);

    if (t == nullptr || t->type != collab::TrackType::midi)
        return;

    juce::PopupMenu m;
    const bool builtin = t->instrument && t->instrument->kind == collab::Instrument::Kind::builtin;
    const bool external = t->instrument && t->instrument->kind == collab::Instrument::Kind::external;

    if (builtin)
        m.addItem ("音源の調整…"_ju, [this] { InstrumentPanel::show (ctx, trackId, instrumentButton); });

    if (external)
        m.addItem ("プラグインの画面を開く"_ju, [this] { if (ctx.openPluginEditor) ctx.openPluginEditor (trackId, {}); });

    m.addSeparator();

    juce::PopupMenu builtins;
    for (auto [id, name] : { std::pair (collab::builtin::drums, "ドラム"_ju), std::pair (collab::builtin::bass, "ベース"_ju),
                             std::pair (collab::builtin::piano, "ピアノ"_ju), std::pair (collab::builtin::epiano, "エレピ"_ju) })
        builtins.addItem (name, [this, id = std::string (id)] { ctx.setBuiltinInstrument (trackId, id); });

    juce::PopupMenu plugins;
    for (auto& d : PluginHost::list (ctx.engine.getEngine(), true))
        plugins.addItem (d.name + " (" + d.manufacturerName + ")", [this, d] { ctx.setExternalInstrument (trackId, d); });

    if (plugins.getNumItems() == 0)
        plugins.addItem ("プラグインがありません（オプション → プラグイン… でスキャン）"_ju, false, false, nullptr);

    m.addSubMenu ("内蔵音源に変更"_ju, builtins);
    m.addSubMenu ("外部プラグインに変更"_ju, plugins);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&instrumentButton));
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
            fx.addSubMenu (toJuce (e.plugin.name), one);
        }

        fx.addSeparator();
        fx.addSubMenu ("追加"_ju, ctx.addEffectMenu (trackId));
        m.addSeparator();
        m.addItem ("EQ / Compressor…"_ju, [this] { if (ctx.openChannelStrip) ctx.openChannelStrip (trackId); });

        m.addSubMenu ("出力先・センド"_ju, ctx.routingMenu (trackId));
        m.addSubMenu ("エフェクト"_ju, fx);

        if (t->type == collab::TrackType::midi || ! t->effects.empty())
            m.addItem ("バウンス（オーディオに書き出す）"_ju, [this] { ctx.bounceTrack (trackId); });
    }

    if (ctx.addLockMenuItems)
    {
        m.addSeparator();
        ctx.addLockMenuItems (trackId, m);
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
