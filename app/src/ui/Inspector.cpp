#include "Inspector.h"

#include "InstrumentPanel.h"
#include "Theme.h"
#include "plugins/PluginHost.h"

namespace
{
    constexpr int titleHeight = 36, sectionHeight = 26, rowHeight = 30, stripHeight = 640;
}

//==============================================================================
class Inspector::Content  : public juce::Component
{
public:
    explicit Content (AppContext& c) : ctx (c), strip (c)
    {
        for (auto* b : { &inputButton, &inputMode, &outputButton, &outputMode, &instrumentButton, &adjustButton })
        {
            b->setWantsKeyboardFocus (false);
            addAndMakeVisible (b);
        }

        inputButton.setTooltip ("入力"_ju);
        inputButton.onClick = [this]
        {
            if (auto* t = track(); t != nullptr && t->type == collab::TrackType::midi)
                showMidiInputMenu();
            else
                showInputMenu();
        };
        inputMode.setTooltip ("入力のモノ / ステレオ"_ju);
        inputMode.onClick = [this] { toggleInputChannels(); };

        outputButton.setTooltip ("出力先"_ju);
        outputButton.onClick = [this] { ctx.outputMenu (trackId).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&outputButton)); };
        outputMode.setTooltip ("出力のモノ / ステレオ"_ju);
        outputMode.onClick = [this]
        {
            editTrack ("出力のモノ / ステレオ"_ju, [] (collab::Track& t) { t.outputChannels = t.outputChannels == 1 ? 2 : 1; });
        };

        instrumentButton.setTooltip ("音源"_ju);
        instrumentButton.onClick = [this] { showInstrumentMenu(); };
        adjustButton.setTooltip ("音源の調整"_ju);
        adjustButton.onClick = [this]
        {
            auto* t = track();

            if (t == nullptr || ! t->instrument)
                return;

            if (t->instrument->kind == collab::Instrument::Kind::builtin)
                InstrumentPanel::show (ctx, trackId, adjustButton);
            else if (ctx.openPluginEditor)
                ctx.openPluginEditor (trackId, {});
        };

        addAndMakeVisible (strip);
    }

    void setTrack (const std::string& id)
    {
        trackId = id;
        strip.setTrack (id);
        update();
    }

    int preferredHeight() const
    {
        auto* t = track();

        if (t == nullptr)
            return 200;

        int h = titleHeight + sectionHeight + rowHeight * (t->type == collab::TrackType::bus ? 1 : 2) + 8;

        if (t->type == collab::TrackType::midi)
            h += sectionHeight + rowHeight * 2 + 8;

        return h + sectionHeight + stripHeight;
    }

    void update()
    {
        auto* t = track();

        for (auto* c : getChildren())
            c->setVisible (t != nullptr);

        if (t == nullptr)
            return repaint();

        const bool audio = t->type == collab::TrackType::audio;
        const bool midi = t->type == collab::TrackType::midi;
        inputButton.setVisible (audio || midi);
        inputMode.setVisible (audio);

        if (midi)
        {
            // MIDI の入力: どの MIDI 鍵盤（機器）から受けるか
            const auto choice = ctx.engine.getTrackMidiInput (trackId);
            inputButton.setButtonText (choice.isEmpty() ? "すべての MIDI 入力"_ju : choice == "-" ? "なし"_ju : choice);
            inputButton.setTooltip ("MIDI の入力（鍵盤など）"_ju);
        }
        instrumentButton.setVisible (midi);
        adjustButton.setVisible (midi);

        if (audio)
        {
            const auto in = ctx.engine.getTrackInput (trackId);
            juce::String label = "なし"_ju;

            for (auto& c : ctx.inputChoices (trackId))
                if (c.left == in.device && c.right == in.deviceRight)
                    label = c.label;

            inputButton.setButtonText (label + (in.monitor ? "（モニター）"_ju : juce::String()));
            inputMode.setButtonText (t->inputChannels == 1 ? "モノ"_ju : "ステレオ"_ju);
        }

        outputButton.setButtonText (ctx.outputName (*t));
        outputMode.setButtonText (t->outputChannels == 1 ? "モノ"_ju : "ステレオ"_ju);

        if (midi)
        {
            juce::String name = "音源なし"_ju;

            if (t->instrument)
            {
                if (t->instrument->kind == collab::Instrument::Kind::builtin)
                {
                    auto* m = ctx.library.find (t->instrument->id, t->instrument->version);
                    name = m != nullptr ? toJuce (m->displayName) : toJuce (t->instrument->id);
                }
                else
                {
                    name = toJuce (t->instrument->plugin.name);
                }
            }

            const auto problem = ctx.engine.getInstrumentProblem (trackId);
            instrumentButton.setButtonText (name);
            instrumentButton.setColour (juce::TextButton::textColourOffId, problem.isEmpty() ? Theme::text : Theme::warning);
            instrumentButton.setTooltip (problem.isEmpty() ? "音源"_ju : problem);
            adjustButton.setButtonText (t->instrument && t->instrument->kind == collab::Instrument::Kind::external
                                            ? "プラグインの画面を開く"_ju : "音色・音量の調整…"_ju);
            adjustButton.setEnabled (t->instrument.has_value());
        }

        resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panel);
        auto* t = track();

        if (t == nullptr)
        {
            g.setColour (Theme::textDim);
            g.setFont (juce::FontOptions (15.0f));
            g.drawFittedText ("トラックを選ぶと、ここに\n入出力・音源・チャンネルの設定が出ます"_ju,
                              getLocalBounds().reduced (12).withHeight (80), juce::Justification::centred, 3);
            return;
        }

        // トラックの名前（色の帯）
        auto title = juce::Rectangle<int> (0, 0, getWidth(), titleHeight).reduced (6, 4);
        const auto colour = Theme::parseColour (t->color);
        g.setColour (colour);
        g.fillRoundedRectangle (title.toFloat(), 4.0f);
        g.setColour (colour.getPerceivedBrightness() > 0.55f ? juce::Colours::black : juce::Colours::white);
        g.setFont (juce::FontOptions (16.5f, juce::Font::bold));
        g.drawText (toJuce (t->name), title.reduced (8, 0), juce::Justification::centredLeft, true);

        for (auto& [area, text] : sections)
            Theme::drawSectionHeader (g, area, text);

        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (14.5f));

        for (auto& [area, text] : rowLabels)
            g.drawText (text, area, juce::Justification::centredLeft);
    }

    void resized() override
    {
        sections.clear();
        rowLabels.clear();
        auto* t = track();

        if (t == nullptr)
            return;

        auto area = getLocalBounds();
        area.removeFromTop (titleHeight);

        auto row = [&] (const juce::String& label, juce::Button& main, juce::Button* mode)
        {
            auto r = area.removeFromTop (rowHeight).reduced (8, 3);
            rowLabels.push_back ({ r.removeFromLeft (38), label });

            if (mode != nullptr)
            {
                mode->setBounds (r.removeFromRight (64));
                r.removeFromRight (4);
            }

            main.setBounds (r);
        };

        sections.push_back ({ area.removeFromTop (sectionHeight), "入出力"_ju });

        if (t->type == collab::TrackType::audio)
            row ("入力"_ju, inputButton, &inputMode);
        else if (t->type == collab::TrackType::midi)
            row ("入力"_ju, inputButton, nullptr);

        row ("出力"_ju, outputButton, &outputMode);
        area.removeFromTop (8);

        if (t->type == collab::TrackType::midi)
        {
            sections.push_back ({ area.removeFromTop (sectionHeight), "音源"_ju });
            instrumentButton.setBounds (area.removeFromTop (rowHeight).reduced (8, 3));
            adjustButton.setBounds (area.removeFromTop (rowHeight).reduced (8, 3));
            area.removeFromTop (8);
        }

        sections.push_back ({ area.removeFromTop (sectionHeight), "チャンネル"_ju });
        strip.setBounds (area.removeFromTop (stripHeight));
    }

private:
    AppContext& ctx;
    std::string trackId;
    juce::TextButton inputButton, inputMode, outputButton, outputMode, instrumentButton, adjustButton;
    TrackChannelStrip strip;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> sections, rowLabels;

    const collab::Track* track() const      { return ctx.document.getProject().findTrack (trackId); }

    void editTrack (const juce::String& description, std::function<void (collab::Track&)> fn)
    {
        auto id = trackId;
        ctx.document.perform (description, [id, fn] (collab::Project& p)
        {
            if (auto* t = p.findTrack (id))
                fn (*t);
        });
    }

    void toggleInputChannels()
    {
        editTrack ("入力のモノ / ステレオ"_ju, [] (collab::Track& t) { t.inputChannels = t.inputChannels == 1 ? 2 : 1; });

        // 割り当て済みの入力は、新しいモノ / ステレオの最初の選択肢に付け替える（待機・モニターはそのまま）
        auto in = ctx.engine.getTrackInput (trackId);

        if (in.device.isNotEmpty())
        {
            const auto choices = ctx.inputChoices (trackId);
            in.device = choices.empty() ? juce::String() : choices.front().left;
            in.deviceRight = choices.empty() ? juce::String() : choices.front().right;

            if (in.device.isEmpty())
                in.armed = in.monitor = false;

            ctx.engine.setTrackInput (trackId, in);
            ctx.state.changed();
        }
    }

    void showInputMenu()
    {
        const auto current = ctx.engine.getTrackInput (trackId);
        juce::PopupMenu m;

        m.addItem ("なし"_ju, true, current.device.isEmpty(), [this]
        {
            ctx.engine.setTrackInput (trackId, {});
            ctx.state.changed();
        });

        for (auto& c : ctx.inputChoices (trackId))
            m.addItem (c.label, true, current.device == c.left && current.deviceRight == c.right, [this, c]
            {
                auto in = ctx.engine.getTrackInput (trackId);
                in.device = c.left;
                in.deviceRight = c.right;
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

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&inputButton));
    }

    void showMidiInputMenu()
    {
        const auto current = ctx.engine.getTrackMidiInput (trackId);
        juce::PopupMenu m;
        auto set = [this] (juce::String v) { ctx.engine.setTrackMidiInput (trackId, v); ctx.state.changed(); };

        m.addItem ("すべての MIDI 入力"_ju, true, current.isEmpty(), [set] { set ({}); });
        m.addItem ("なし"_ju, true, current == "-", [set] { set ("-"); });
        m.addSeparator();
        bool any = false;

        for (auto& in : ctx.engine.getMidiInputs())
        {
            if (! in.enabled)
                continue;

            any = true;
            m.addItem (in.name, true, current == in.name, [set, name = in.name] { set (name); });
        }

        if (! any)
            m.addItem ("MIDI 鍵盤が有効になっていません（設定 → オーディオ・MIDI の設定）"_ju, false, false, nullptr);

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&inputButton));
    }

    void showInstrumentMenu()
    {
        auto* t = track();

        if (t == nullptr || t->type != collab::TrackType::midi)
            return;

        juce::PopupMenu builtins;

        for (auto [id, name] : { std::pair (collab::builtin::drums, "ドラム"_ju), std::pair (collab::builtin::bass, "ベース"_ju),
                                 std::pair (collab::builtin::piano, "ピアノ"_ju), std::pair (collab::builtin::epiano, "エレピ"_ju) })
            builtins.addItem (name, [this, id = std::string (id)] { ctx.setBuiltinInstrument (trackId, id); });

        juce::PopupMenu plugins;

        for (auto& d : PluginHost::list (ctx.engine.getEngine(), true))
            plugins.addItem (d.name + " (" + d.manufacturerName + ")", [this, d] { ctx.setExternalInstrument (trackId, d); });

        if (plugins.getNumItems() == 0)
            plugins.addItem ("プラグインがありません（設定 → プラグイン… でスキャン）"_ju, false, false, nullptr);

        juce::PopupMenu m;
        m.addSubMenu ("内蔵音源に変更"_ju, builtins);
        m.addSubMenu ("外部プラグインに変更"_ju, plugins);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&instrumentButton));
    }
};

//==============================================================================
Inspector::Inspector (AppContext& c) : ctx (c)
{
    content = std::make_unique<Content> (ctx);
    viewport.setViewedComponent (content.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);

    ctx.document.addChangeListener (this);
    ctx.state.addChangeListener (this);
    content->setTrack (ctx.state.selectedTrackId);
}

Inspector::~Inspector()
{
    ctx.document.removeChangeListener (this);
    ctx.state.removeChangeListener (this);
}

void Inspector::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
    g.setColour (Theme::background);
    g.fillRect (getWidth() - 2, 0, 2, getHeight());
}

void Inspector::resized()
{
    viewport.setBounds (getLocalBounds().withTrimmedRight (2));
    content->setSize (viewport.getMaximumVisibleWidth(), juce::jmax (viewport.getHeight(), content->preferredHeight()));
}

void Inspector::changeListenerCallback (juce::ChangeBroadcaster*)
{
    content->setTrack (ctx.state.selectedTrackId);
    const int h = juce::jmax (viewport.getHeight(), content->preferredHeight());

    if (content->getHeight() != h)
        content->setSize (viewport.getMaximumVisibleWidth(), h);
}
