#include "TrackHeader.h"

#include "Dialogs.h"
#include "InstrumentPanel.h"
#include "Theme.h"
#include "sync/SyncManager.h"

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

    instrumentButton.setTooltip ("音源の調整"_ju);
    instrumentButton.onClick = [this]
    {
        select();
        InstrumentPanel::show (ctx, trackId, instrumentButton);
    };
    addAndMakeVisible (instrumentButton);

    for (auto* b : { &muteButton, &soloButton })
    {
        b->setClickingTogglesState (false);
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

    nameLabel.setText (toJuce (t->name), juce::dontSendNotification);
    muteButton.setToggleState (t->mute, juce::dontSendNotification);
    soloButton.setToggleState (t->solo, juce::dontSendNotification);
    volumeSlider.setValue (t->volumeDb, juce::dontSendNotification);
    panSlider.setValue (t->pan, juce::dontSendNotification);

    juce::String instName = t->type == collab::TrackType::audio ? "オーディオ"_ju : "音源なし"_ju;

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

    instrumentButton.setButtonText (instName);
    instrumentButton.setEnabled (t->type == collab::TrackType::midi);
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
        g.setColour (Theme::parseColour (t->color));
        g.fillRect (0, 0, 5, getHeight());
    }

    g.setColour (Theme::background);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    // 同期中: ロックと未 push の変更（§4.2, §4.4）
    if (ctx.sync.isLinked())
    {
        juce::String badge;
        auto colour = Theme::textDim;

        if (ctx.sync.isLockedByMe (trackId))           { badge = "ロック中（自分）"_ju; colour = Theme::accent; }
        else if (auto lock = ctx.sync.getLock (trackId)) { badge = lock->displayName + " が編集中"_ju; colour = Theme::warning; }
        else if (! ctx.sync.canEdit (trackId))          { badge = "読み取り専用"_ju; }

        if (ctx.sync.hasLocalChanges (trackId))
            badge = (badge.isEmpty() ? juce::String() : badge + "  ") + "● 未 push"_ju;

        g.setColour (colour);
        g.setFont (juce::FontOptions (10.5f));
        g.drawText (badge, getLocalBounds().withTrimmedLeft (10).withTrimmedRight (60).removeFromTop (14).translated (0, 1),
                    juce::Justification::centredRight, true);
    }

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
    auto area = getLocalBounds().reduced (10, 4);
    area.removeFromLeft (0);

    auto top = area.removeFromTop (22);
    soloButton.setBounds (top.removeFromRight (24));
    top.removeFromRight (3);
    muteButton.setBounds (top.removeFromRight (24));
    top.removeFromRight (4);
    nameLabel.setBounds (top);

    area.removeFromTop (3);
    instrumentButton.setBounds (area.removeFromTop (20));

    auto sliderRow = area.removeFromBottom (18);
    sliderRow.removeFromLeft (26);
    auto volArea = sliderRow.removeFromLeft ((int) (sliderRow.getWidth() * 0.62f));
    volumeSlider.setBounds (volArea);
    sliderRow.removeFromLeft (28);
    panSlider.setBounds (sliderRow);
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

    if (e.mods.isPopupMenu())
        showMenu();
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
