#include "InstrumentPanel.h"

#include "Theme.h"

namespace
{
    constexpr int rowHeight = 26;
    constexpr int panelWidth = 640;
}

InstrumentPanel::InstrumentPanel (AppContext& c, const std::string& id)
    : ctx (c), trackId (id)
{
    auto* t = ctx.document.getProject().findTrack (trackId);

    if (t != nullptr && t->instrument && t->instrument->kind == collab::Instrument::Kind::builtin)
        manifest = ctx.library.find (t->instrument->id, t->instrument->version);

    title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    addAndMakeVisible (title);

    // 新しい版があれば更新できるようにする（版を固定しているのは、共同作業の相手と同じ音にするため）
    if (auto* latest = manifest != nullptr ? ctx.library.findLatest (manifest->id) : nullptr; latest != nullptr && latest != manifest)
    {
        upgradeButton.setButtonText ("新しい版 "_ju + toJuce (latest->version) + " に更新"_ju);
        upgradeButton.setTooltip ("音源を新しい版に切り替える（音色が変わります。相手も新しいアプリが必要です）"_ju);
        upgradeButton.onClick = [this, version = latest->version]
        {
            ctx.document.perform ("音源の版を更新"_ju, [track = trackId, version] (collab::Project& p)
            {
                if (auto* tr = p.findTrack (track); tr != nullptr && tr->instrument)
                    tr->instrument->version = version;
            });

            if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
                box->dismiss();
        };
        addAndMakeVisible (upgradeButton);
    }

    setupSlider (volume, -30.0, 12.0, 0.1, 0.0, " dB");
    setupSlider (pan, -1.0, 1.0, 0.01, 0.0, {});
    setupSlider (tone, -12.0, 12.0, 0.1, 0.0, " dB");
    tone.setTooltip ("高域の強さ（3kHz 以上のシェルフ）"_ju);

    volume.onValueChange = [this] { auto v = volume.getValue(); setParam ("音源の音量"_ju, [v] (nlohmann::json& p) { p["volumeDb"] = v; }, true); };
    pan.onValueChange = [this] { auto v = pan.getValue(); setParam ("音源のパン"_ju, [v] (nlohmann::json& p) { p["pan"] = v; }, true); };
    tone.onValueChange = [this] { auto v = tone.getValue(); setParam ("音源のトーン"_ju, [v] (nlohmann::json& p) { p["tone"] = v; }, true); };

    for (auto* l : { &volumeLabel, &panLabel, &toneLabel, &kitLabel, &presetLabel })
        addAndMakeVisible (l);

    credits.setColour (juce::Label::textColourId, Theme::textDim);
    credits.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (credits);

    int height = 40 + 3 * rowHeight + 10;

    if (manifest != nullptr && ! manifest->presets.empty())
    {
        int presetId = 1;

        for (auto& preset : manifest->presets)
            presetBox.addItem (toJuce (preset.displayName), presetId++);

        presetBox.onChange = [this]
        {
            const int index = presetBox.getSelectedItemIndex();

            if (index < 0 || index >= (int) manifest->presets.size())
                return;

            auto key = manifest->presets[(size_t) index].key;
            setParam ("音色の変更"_ju, [key] (nlohmann::json& p) { p["preset"] = key; });
        };
        addAndMakeVisible (presetBox);
        height += rowHeight + 6;
    }
    else
    {
        presetLabel.setVisible (false);
    }

    if (manifest != nullptr && manifest->type == "drums")
    {
        int kitId = 1;

        for (auto& [kit, pieces] : manifest->kits)
            kitBox.addItem (toJuce (kit), kitId++);

        kitBox.onChange = [this]
        {
            auto kit = toStd (kitBox.getText());
            // キットを替えたら、パーツごとのサンプル差し替えは解除する（音量等は残す）
            setParam ("ドラムキットの変更"_ju, [kit] (nlohmann::json& p)
            {
                p["kit"] = kit;

                if (p.contains ("pieces") && p["pieces"].is_object())
                    for (auto it = p["pieces"].begin(); it != p["pieces"].end(); ++it)
                        it->erase ("sample");
            });
        };
        addAndMakeVisible (kitBox);
        height += rowHeight + 30;

        for (auto& piece : manifest->pieces)
        {
            PieceRow row;
            row.key = piece.key;
            row.name = std::make_unique<juce::Label> ("", toJuce (piece.displayName) + " (" + juce::String (piece.note) + ")");
            row.name->setFont (juce::FontOptions (12.0f));
            row.sample = std::make_unique<juce::ComboBox>();

            int sid = 1;
            for (auto& s : manifest->samples)
                row.sample->addItem (toJuce (s), sid++);

            row.volume = std::make_unique<juce::Slider>();
            row.pan = std::make_unique<juce::Slider>();
            row.tune = std::make_unique<juce::Slider>();
            setupSlider (*row.volume, -30.0, 12.0, 0.1, 0.0, " dB");
            setupSlider (*row.pan, -1.0, 1.0, 0.01, 0.0, {});
            setupSlider (*row.tune, -12.0, 12.0, 0.1, 0.0, " 半音"_ju);

            for (auto* sl : { row.volume.get(), row.pan.get(), row.tune.get() })
            {
                sl->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                sl->setPopupDisplayEnabled (true, true, this);
            }

            auto key = piece.key;
            row.sample->onChange = [this, key, box = row.sample.get()]
            {
                auto s = toStd (box->getText());
                setParam ("ドラムのサンプル差し替え"_ju, [key, s] (nlohmann::json& p) { p["pieces"][key]["sample"] = s; });
            };
            row.volume->onValueChange = [this, key, s = row.volume.get()]
            {
                auto v = s->getValue();
                setParam ("ドラムパーツの音量"_ju, [key, v] (nlohmann::json& p) { p["pieces"][key]["volumeDb"] = v; }, true);
            };
            row.pan->onValueChange = [this, key, s = row.pan.get()]
            {
                auto v = s->getValue();
                setParam ("ドラムパーツのパン"_ju, [key, v] (nlohmann::json& p) { p["pieces"][key]["pan"] = v; }, true);
            };
            row.tune->onValueChange = [this, key, s = row.tune.get()]
            {
                auto v = s->getValue();
                setParam ("ドラムパーツのチューニング"_ju, [key, v] (nlohmann::json& p) { p["pieces"][key]["tune"] = v; }, true);
            };

            for (juce::Component* comp : std::initializer_list<juce::Component*> { row.name.get(), row.sample.get(), row.volume.get(), row.pan.get(), row.tune.get() })
                addAndMakeVisible (comp);

            rows.push_back (std::move (row));
            height += rowHeight;
        }
    }

    height += 24;
    setSize (panelWidth, height);

    ctx.document.addChangeListener (this);
    refresh();
}

InstrumentPanel::~InstrumentPanel()
{
    ctx.document.removeChangeListener (this);
}

void InstrumentPanel::show (AppContext& ctx, const std::string& trackId, juce::Component& target)
{
    auto* t = ctx.document.getProject().findTrack (trackId);

    if (t == nullptr || ! t->instrument)
        return;

    if (t->instrument->kind != collab::Instrument::Kind::builtin)
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::InfoIcon, "外部プラグイン"_ju,
                                                                              "外部プラグインは M2 で対応します。"_ju), nullptr);
        return;
    }

    juce::CallOutBox::launchAsynchronously (std::make_unique<InstrumentPanel> (ctx, trackId),
                                            target.getScreenBounds(), nullptr);
}

void InstrumentPanel::setupSlider (juce::Slider& s, double min, double max, double step, double reset, const juce::String& suffix)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    s.setRange (min, max, step);
    s.setDoubleClickReturnValue (true, reset);
    s.setTextValueSuffix (suffix);
    s.onDragStart = [this] { mergeId = juce::Uuid().toString(); };
    s.onDragEnd = [this] { ctx.document.endMerge(); mergeId = {}; };
    addAndMakeVisible (s);
}

nlohmann::json InstrumentPanel::currentParams() const
{
    auto* t = ctx.document.getProject().findTrack (trackId);
    return t != nullptr && t->instrument ? t->instrument->params : nlohmann::json::object();
}

void InstrumentPanel::setParam (const juce::String& description, std::function<void (nlohmann::json&)> fn, bool merge)
{
    auto id = trackId;
    ctx.document.perform (description, [id, fn] (collab::Project& p)
    {
        if (auto* t = p.findTrack (id); t != nullptr && t->instrument)
        {
            if (! t->instrument->params.is_object())
                t->instrument->params = nlohmann::json::object();

            fn (t->instrument->params);
        }
    }, merge ? mergeId : juce::String());
}

void InstrumentPanel::refresh()
{
    if (manifest == nullptr)
    {
        title.setText ("内蔵音源が見つかりません"_ju, juce::dontSendNotification);
        return;
    }

    auto r = collab::resolveInstrumentParams (*manifest, currentParams());

    title.setText (toJuce (manifest->displayName) + "  (" + toJuce (manifest->id) + " " + toJuce (manifest->version) + ")",
                   juce::dontSendNotification);
    volume.setValue (r.volumeDb, juce::dontSendNotification);
    pan.setValue (r.pan, juce::dontSendNotification);
    tone.setValue (r.toneDb, juce::dontSendNotification);

    if (! manifest->credits.empty())
        credits.setText ("クレジット: "_ju + toJuce (manifest->credits.front()), juce::dontSendNotification);

    for (size_t i = 0; i < manifest->presets.size(); ++i)
        if (manifest->presets[i].key == r.preset)
            presetBox.setSelectedItemIndex ((int) i, juce::dontSendNotification);

    for (int i = 0; i < kitBox.getNumItems(); ++i)
        if (kitBox.getItemText (i) == toJuce (r.kit))
            kitBox.setSelectedItemIndex (i, juce::dontSendNotification);

    for (auto& row : rows)
    {
        auto it = r.pieces.find (row.key);

        if (it == r.pieces.end())
            continue;

        for (int i = 0; i < row.sample->getNumItems(); ++i)
            if (row.sample->getItemText (i) == toJuce (it->second.sample))
                row.sample->setSelectedItemIndex (i, juce::dontSendNotification);

        row.volume->setValue (it->second.volumeDb, juce::dontSendNotification);
        row.pan->setValue (it->second.pan, juce::dontSendNotification);
        row.tune->setValue (it->second.tuneSemitones, juce::dontSendNotification);
    }
}

void InstrumentPanel::paint (juce::Graphics& g)
{
    if (! rows.empty())
    {
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (11.0f));
        const int y = rows.front().name->getY() - 18;
        g.drawText ("パーツ"_ju, 8, y, 150, 16, juce::Justification::centredLeft);
        g.drawText ("サンプル"_ju, 160, y, 150, 16, juce::Justification::centredLeft);
        g.drawText ("音量"_ju, 320, y, 100, 16, juce::Justification::centredLeft);
        g.drawText ("パン"_ju, 430, y, 100, 16, juce::Justification::centredLeft);
        g.drawText ("チューニング"_ju, 530, y, 100, 16, juce::Justification::centredLeft);
    }
}

void InstrumentPanel::resized()
{
    auto area = getLocalBounds().reduced (8);
    auto titleRow = area.removeFromTop (28);

    if (upgradeButton.isVisible())
        upgradeButton.setBounds (titleRow.removeFromRight (170).reduced (0, 2));

    title.setBounds (titleRow);
    area.removeFromTop (4);

    auto place = [&] (juce::Label& l, juce::Component& c)
    {
        auto row = area.removeFromTop (rowHeight);
        l.setBounds (row.removeFromLeft (80));
        c.setBounds (row.removeFromLeft (360).reduced (0, 2));
    };

    if (presetBox.isVisible())
    {
        place (presetLabel, presetBox);
        area.removeFromTop (6);
    }

    place (volumeLabel, volume);
    place (panLabel, pan);
    place (toneLabel, tone);

    if (! rows.empty())
    {
        area.removeFromTop (6);
        place (kitLabel, kitBox);
        area.removeFromTop (22);

        for (auto& row : rows)
        {
            auto r = area.removeFromTop (rowHeight);
            row.name->setBounds (r.removeFromLeft (150));
            row.sample->setBounds (r.removeFromLeft (155).reduced (0, 2));
            r.removeFromLeft (5);
            row.volume->setBounds (r.removeFromLeft (105).reduced (0, 2));
            row.pan->setBounds (r.removeFromLeft (100).reduced (0, 2));
            row.tune->setBounds (r.reduced (0, 2));
        }
    }

    credits.setBounds (area.removeFromBottom (20));
}
