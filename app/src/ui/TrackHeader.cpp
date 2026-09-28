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

    instrumentButton.setTooltip ("音源の調整"_ju);
    instrumentButton.onClick = [this]
    {
        select();

        if (isAudioTrack())
            showInputMenu();
        else
            showInstrumentMenu();
    };

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

        // MIDI トラック: MIDI キーボードで弾いたものを録音する（録音待機にできる MIDI トラックは 1 つ）
        if (! isAudioTrack())
        {
            const bool arm = ctx.state.midiArmedTrackId != trackId;
            ctx.state.midiArmedTrackId = arm ? trackId : std::string();
            ctx.state.changed();

            const auto inputs = ctx.engine.getMidiInputs();

            if (arm && std::none_of (inputs.begin(), inputs.end(), [] (auto& m) { return m.enabled; }))
                Dialogs::showInfo ("録音待機"_ju, "MIDI キーボードが見つかりません。つないでから、設定 → オーディオ・MIDI の設定で有効にしてください。"_ju);

            return;
        }

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
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    muteButton.setTooltip ("ミュート"_ju);
    soloButton.setTooltip ("ソロ"_ju);
    muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe57373));
    soloButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffffd54f).darker (0.2f));
    muteButton.onClick = [this] { editTrack ("ミュート"_ju, [] (collab::Track& t) { t.mute = ! t.mute; }); };
    soloButton.onClick = [this] { editTrack ("ソロ"_ju, [] (collab::Track& t) { t.solo = ! t.solo; }); };

    volumeSlider.setRange (-60.0, 6.0, 0.1);
    volumeSlider.setSkewFactorFromMidPoint (-12.0);
    volumeSlider.setDoubleClickReturnValue (true, 0.0);
    volumeSlider.setTooltip ("音量（ダブルクリックで 0 dB）"_ju);

    panSlider.setRange (-1.0, 1.0, 0.01);
    panSlider.setDoubleClickReturnValue (true, 0.0);
    panSlider.setTooltip ("パン（ダブルクリックで中央）"_ju);

    // 値は横に数字で出す（クリックで打ち込める）
    for (auto* l : { &volumeValue, &panValue })
    {
        l->setEditable (true, true, true);
        l->setJustificationType (juce::Justification::centred);
        l->setFont (juce::FontOptions (15.0f));
        l->setColour (juce::Label::textColourId, Theme::text);
        l->setColour (juce::Label::backgroundColourId, juce::Colours::black.withAlpha (0.25f));
        l->setColour (juce::Label::backgroundWhenEditingColourId, juce::Colour (0xff0e0f12));
        l->setColour (juce::Label::textWhenEditingColourId, Theme::text);
        l->setColour (juce::Label::outlineWhenEditingColourId, Theme::accent);
        addAndMakeVisible (l);
    }

    volumeValue.setTooltip ("音量（dB）。クリックして数値を入力"_ju);
    panValue.setTooltip ("パン。クリックして入力（L30・C・R20）"_ju);
    volumeValue.onTextChange = [this]
    {
        if (auto db = ValueText::parseDb (volumeValue.getText(), -60.0, 6.0))
            editTrack ("音量"_ju, [v = *db] (collab::Track& t) { t.volumeDb = v; });

        update();
    };
    panValue.onTextChange = [this]
    {
        if (auto v = ValueText::parsePan (panValue.getText()))
            editTrack ("パン"_ju, [v = *v] (collab::Track& t) { t.pan = v; });

        update();
    };

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

    if (! volumeValue.isBeingEdited())
        volumeValue.setText (ValueText::formatDb (t->volumeDb), juce::dontSendNotification);

    if (! panValue.isBeingEdited())
        panValue.setText (ValueText::formatPan (t->pan), juce::dontSendNotification);

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
        armButton.setTooltip ("録音待機（入力はその下のボタンで選ぶ）"_ju);
    }
    else if (t->type == collab::TrackType::midi)
    {
        armButton.setToggleState (ctx.state.midiArmedTrackId == trackId, juce::dontSendNotification);
        armButton.setTooltip ("録音待機（MIDI キーボードで弾いたものをこのトラックに録音）"_ju);
    }

    const bool canArm = t->type == collab::TrackType::audio || t->type == collab::TrackType::midi;

    if (armButton.isVisible() != canArm)
    {
        armButton.setVisible (canArm);
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

    // 他の人がアップして新しくなったトラックは青、競合は橙をうっすら重ねて、ダウンロードを促す
    if (ctx.sync.isLinked())
    {
        const auto st = ctx.sync.scopeState (trackId);

        if (st.conflict || st.theirs)
        {
            g.setColour ((st.conflict ? Theme::warning : Theme::accent).withAlpha (0.16f));
            g.fillAll();
        }
    }

    if (t != nullptr)
    {
        g.setColour (Theme::parseColour (t->color));
        g.fillRect (0, 0, 5, getHeight());
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
            g.setFont (juce::FontOptions (13.5f));
            g.drawText (renderBadge, getLocalBounds().withTrimmedLeft (10).withTrimmedRight (10).removeFromBottom (32).removeFromTop (12),
                        juce::Justification::centredRight, true);
        }
    }

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
    badgeArea = badgeShown ? top.removeFromRight (juce::jmin (70, top.getWidth() / 2)).reduced (0, 2) : juce::Rectangle<int>();

    if (badgeShown)
        top.removeFromRight (4);

    nameLabel.setBounds (top);

    instrumentButton.setVisible (showInstrument);
    for (auto* c : std::initializer_list<juce::Component*> { &volumeSlider, &panSlider, &volumeValue, &panValue })
        c->setVisible (showSliders);

    area.removeFromTop (3);

    if (showInstrument)
        instrumentButton.setBounds (area.removeFromTop (20));

    if (showSliders)
    {
        // 音量 [スライダー][-6.0]  パン [スライダー][L30]
        auto sliderRow = area.removeFromBottom (20);
        auto volArea = sliderRow.removeFromLeft ((int) (sliderRow.getWidth() * 0.6f));
        volumeValue.setBounds (volArea.removeFromRight (44));
        volumeSlider.setBounds (volArea.withTrimmedRight (2));
        sliderRow.removeFromLeft (6);
        panValue.setBounds (sliderRow.removeFromRight (38));
        panSlider.setBounds (sliderRow.withTrimmedRight (2));
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
        m.addItem ("EQ…"_ju, [this] { if (ctx.openChannelStrip) ctx.openChannelStrip (trackId, false); });
        m.addItem ("Compressor…"_ju, [this] { if (ctx.openChannelStrip) ctx.openChannelStrip (trackId, true); });

        m.addSubMenu ("出力先・センド"_ju, ctx.routingMenu (trackId));
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
