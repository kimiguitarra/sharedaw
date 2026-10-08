#include "SamplerPanel.h"

#include "BuiltinEffectEditor.h"
#include "Dialogs.h"
#include "audio/AudioFiles.h"
#include "collab/ClipEditing.h"
#include "collab/GmDrumMap.h"

namespace
{
    constexpr int numPads = collab::kSamplerPads;

    juce::String trimZeros (double v, int decimals)
    {
        auto s = juce::String (v, decimals);
        return s.containsChar ('.') ? s.trimCharactersAtEnd ("0").trimCharactersAtEnd (".") : s;
    }

    juce::String signedDb (double v)   { return (v > 0.05 ? "+" : "") + juce::String (v, 1) + " dB"; }
}

//==============================================================================
/** 選んだパッドの値を回すつまみ（ドラッグで回す、ダブルクリックで既定値、数字をクリックで打ち込む）。 */
struct SamplerPanel::Knob  : public juce::Component
{
    Knob (SamplerPanel& o, juce::String name, double min, double max, double def, double skewMid,
          std::function<double (const collab::SamplerPad&)> g, std::function<void (collab::SamplerPad&, double)> s,
          std::function<juce::String (double)> f)
        : owner (o), label (std::move (name)), defaultValue (def), get (std::move (g)), set (std::move (s)), format (std::move (f))
    {
        slider.setLookAndFeel (&knobLook());
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        slider.setRange (min, max, 0.0);

        if (skewMid > min && skewMid < max)
            slider.setSkewFactorFromMidPoint (skewMid);

        slider.setDoubleClickReturnValue (true, def);
        slider.setMouseDragSensitivity (220);
        slider.onDragStart = [this] { owner.mergeId = juce::Uuid().toString(); };
        slider.onDragEnd = [this] { owner.ctx.document.endMerge(); owner.mergeId = {}; };
        slider.onValueChange = [this] { apply (slider.getValue()); };
        addAndMakeVisible (slider);

        valueLabel.setJustificationType (juce::Justification::centred);
        valueLabel.setFont (juce::FontOptions (13.0f));
        valueLabel.setColour (juce::Label::textColourId, Theme::text);
        valueLabel.setColour (juce::Label::backgroundWhenEditingColourId, Theme::field);
        valueLabel.setColour (juce::Label::outlineWhenEditingColourId, Theme::accent);
        valueLabel.setEditable (true, true, false);
        valueLabel.onTextChange = [this, min, max]
        {
            const auto text = valueLabel.getText().toUpperCase();
            double v = text.retainCharacters ("0123456789.-").getDoubleValue();

            if (text.containsChar ('K'))
                v *= 1000.0;

            if (text.startsWithChar ('L'))
                v = -v / 100.0;
            else if (text.startsWithChar ('R'))
                v = v / 100.0;
            else if (text.trim() == "C")
                v = 0.0;

            if (text.containsAnyOf ("0123456789C"))
                slider.setValue (juce::jlimit (min, max, v), juce::sendNotificationSync);

            refresh();
        };
        addAndMakeVisible (valueLabel);
    }

    ~Knob() override    { slider.setLookAndFeel (nullptr); }

    void apply (double v)
    {
        owner.editSelected (label, [this, v] (collab::SamplerPad& p) { set (p, v); }, owner.mergeId);
        valueLabel.setText (format (v), juce::dontSendNotification);
    }

    void refresh()
    {
        const double v = get (owner.selectedPad());

        if (! slider.isMouseButtonDown())
            slider.setValue (v, juce::dontSendNotification);

        if (! valueLabel.isBeingEdited())
            valueLabel.setText (format (v), juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawText (label, getLocalBounds().removeFromTop (16), juce::Justification::centred);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (16);
        valueLabel.setBounds (r.removeFromBottom (20));
        slider.setBounds (r);
    }

    SamplerPanel& owner;
    juce::String label;
    double defaultValue;
    std::function<double (const collab::SamplerPad&)> get;
    std::function<void (collab::SamplerPad&, double)> set;
    std::function<juce::String (double)> format;
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Label valueLabel;
};

//==============================================================================
SamplerPanel::SamplerPanel (AppContext& c, std::string id) : ctx (c), trackId (std::move (id))
{
    volume.setSliderStyle (juce::Slider::LinearHorizontal);
    volume.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
    volume.setRange (-40.0, 12.0, 0.1);
    volume.setTextValueSuffix (" dB");
    volume.setDoubleClickReturnValue (true, 0.0);
    volume.onDragStart = [this] { mergeId = juce::Uuid().toString(); };
    volume.onDragEnd = [this] { ctx.document.endMerge(); mergeId = {}; };
    volume.onValueChange = [this]
    {
        const double v = volume.getValue();
        ctx.editTrack (trackId, "サンプラーの音量"_ju, [v] (collab::Track& t)
        {
            if (t.instrument)
                t.instrument->params["volumeDb"] = v;
        }, mergeId);
    };
    addAndMakeVisible (volume);
    addAndMakeVisible (volumeLabel);

    // パッドの名前（ピアノロールの左にも出る）
    nameEditor.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    nameEditor.setTextToShowWhenEmpty ("名前"_ju, Theme::textDim);
    nameEditor.setTooltip ("パッドの名前"_ju);
    auto commitName = [this]
    {
        const auto name = toStd (nameEditor.getText().trim());

        if (name != selectedPad().name)
            editSelected ("パッドの名前"_ju, [name] (collab::SamplerPad& p) { p.name = name; });
    };
    nameEditor.onReturnKey = [this, commitName] { commitName(); unfocusAllComponents(); };
    nameEditor.onFocusLost = commitName;
    nameEditor.onEscapeKey = [this] { refreshEditor(); unfocusAllComponents(); };
    addAndMakeVisible (nameEditor);

    loadButton.onClick = [this] { chooseFiles (selected); };
    clearButton.onClick = [this]
    {
        editSelected ("パッドを空にする"_ju, [] (collab::SamplerPad& p)
        {
            const int note = p.note;
            p = {};
            p.note = note;
        });
    };
    addAndMakeVisible (loadButton);
    addAndMakeVisible (clearButton);

    const auto pan = [] (double v)
    {
        const int n = juce::roundToInt (std::abs (v) * 100.0);
        return n == 0 ? juce::String ("C") : (v < 0 ? "L" : "R") + juce::String (n);
    };
    const auto hz = [] (double v) { return v >= 1000.0 ? trimZeros (v / 1000.0, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; };
    const auto semitones = [] (double v) { return (v > 0.05 ? "+" : "") + trimZeros (v, 1); };

    auto add = [this] (juce::String name, double min, double max, double def, double skewMid,
                       std::function<double (const collab::SamplerPad&)> g, std::function<void (collab::SamplerPad&, double)> s,
                       std::function<juce::String (double)> f)
    {
        knobs.push_back (std::make_unique<Knob> (*this, std::move (name), min, max, def, skewMid, std::move (g), std::move (s), std::move (f)));
        addAndMakeVisible (*knobs.back());
    };

    add ("音量"_ju, -30.0, 12.0, 0.0, 0.0, [] (auto& p) { return p.gainDb; }, [] (auto& p, double v) { p.gainDb = v; }, signedDb);
    add ("パン"_ju, -1.0, 1.0, 0.0, 0.0, [] (auto& p) { return p.pan; }, [] (auto& p, double v) { p.pan = v; }, pan);
    add ("ピッチ"_ju, -12.0, 12.0, 0.0, 0.0, [] (auto& p) { return p.tuneSemitones; },
         [] (auto& p, double v) { p.tuneSemitones = std::round (v); }, semitones);
    add ("LOW", -15.0, 15.0, 0.0, 0.0, [] (auto& p) { return p.eqLowDb; }, [] (auto& p, double v) { p.eqLowDb = v; }, signedDb);
    add ("MID", -15.0, 15.0, 0.0, 0.0, [] (auto& p) { return p.eqMidDb; }, [] (auto& p, double v) { p.eqMidDb = v; }, signedDb);
    add ("MID FREQ", 200.0, 8000.0, 1000.0, 1000.0, [] (auto& p) { return p.eqMidHz; },
         [] (auto& p, double v) { p.eqMidHz = std::round (v); }, hz);
    add ("HIGH", -15.0, 15.0, 0.0, 0.0, [] (auto& p) { return p.eqHighDb; }, [] (auto& p, double v) { p.eqHighDb = v; }, signedDb);
    add ("サチュレーション"_ju, 0.0, 24.0, 0.0, 0.0, [] (auto& p) { return p.driveDb; }, [] (auto& p, double v) { p.driveDb = v; },
         [] (double v) { return v < 0.05 ? "オフ"_ju : juce::String (v, 1) + " dB"; });

    tempoButton.setTooltip ("曲のテンポに合わせる"_ju);
    tempoButton.setClickingTogglesState (false);
    tempoButton.onClick = [this] { toggleTempo(); };
    addAndMakeVisible (tempoButton);
    tempoLabel.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    tempoLabel.setColour (juce::Label::textColourId, Theme::textDim);
    tempoLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (tempoLabel);

    bpmLabel.setJustificationType (juce::Justification::centredLeft);
    bpmLabel.setFont (juce::FontOptions (14.0f));
    bpmLabel.setColour (juce::Label::backgroundWhenEditingColourId, Theme::field);
    bpmLabel.setEditable (true, true, false);
    bpmLabel.setTooltip ("元のテンポ"_ju);
    bpmLabel.onTextChange = [this]
    {
        if (const double bpm = bpmLabel.getText().retainCharacters ("0123456789.").getDoubleValue(); bpm > 0.0)
            editSelected ("元のテンポ"_ju, [bpm] (collab::SamplerPad& p) { p.sourceBpm = juce::jlimit (20.0, 400.0, bpm); });

        refreshEditor();
    };
    addChildComponent (bpmLabel);

    oneShotButton.onClick = [this]
    {
        const bool on = oneShotButton.getToggleState();
        editSelected ("パッドの鳴らし方"_ju, [on] (collab::SamplerPad& p) { p.oneShot = on; });
    };
    addAndMakeVisible (oneShotButton);

    chokeBox.addItem ("なし"_ju, 1);

    for (int group = 1; group <= 4; ++group)
        chokeBox.addItem (juce::String (group), group + 1);

    chokeBox.setTooltip ("同じ番号の音を止める"_ju);
    chokeBox.onChange = [this]
    {
        const int group = chokeBox.getSelectedId() - 1;

        if (group >= 0 && group != selectedPad().chokeGroup)
            editSelected ("パッドのチョーク"_ju, [group] (collab::SamplerPad& p) { p.chokeGroup = group; });
    };
    chokeLabel.setColour (juce::Label::textColourId, Theme::textDim);
    addAndMakeVisible (chokeBox);
    addAndMakeVisible (chokeLabel);

    ctx.document.addChangeListener (this);
    setSize (1120, 640);
    changeListenerCallback (nullptr);
}

SamplerPanel::~SamplerPanel()
{
    ctx.document.removeChangeListener (this);
}

void SamplerPanel::setTrack (std::string id)
{
    trackId = std::move (id);
    changeListenerCallback (nullptr);
}

const collab::Track* SamplerPanel::track() const
{
    return ctx.document.getProject().findTrack (trackId);
}

std::vector<collab::SamplerPad> SamplerPanel::pads() const
{
    auto* t = track();
    return collab::samplerPads (t != nullptr && t->instrument ? t->instrument->params : nlohmann::json::object());
}

juce::String SamplerPanel::getTitle() const
{
    auto* t = track();
    return (t != nullptr ? toJuce (t->name) + " - " : juce::String()) + "サンプラー"_ju;
}

void SamplerPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (auto* t = track(); t != nullptr && t->instrument)
        volume.setValue (t->instrument->params.value ("volumeDb", 0.0), juce::dontSendNotification);

    if (onTitleChanged)
        onTitleChanged();

    refreshEditor();
    repaint();
}

void SamplerPanel::refreshEditor()
{
    const auto pad = selectedPad();
    const bool filled = ! pad.audioHash.empty();

    if (! nameEditor.hasKeyboardFocus (true))
        nameEditor.setText (toJuce (pad.name), false);

    for (auto& k : knobs)
    {
        k->refresh();
        k->setEnabled (filled);
    }

    tempoButton.setToggleState (pad.sourceBpm > 0.0, juce::dontSendNotification);
    tempoButton.setEnabled (filled);
    bpmLabel.setVisible (filled && pad.sourceBpm > 0.0);

    if (! bpmLabel.isBeingEdited())
        bpmLabel.setText (trimZeros (pad.sourceBpm, 2) + " BPM", juce::dontSendNotification);

    oneShotButton.setToggleState (pad.oneShot, juce::dontSendNotification);
    oneShotButton.setEnabled (filled);
    chokeBox.setSelectedId (pad.chokeGroup + 1, juce::dontSendNotification);
    chokeBox.setEnabled (filled);
    nameEditor.setEnabled (filled);
    clearButton.setEnabled (filled);
}

//==============================================================================
void SamplerPanel::resized()
{
    auto area = getLocalBounds().reduced (14);
    auto top = area.removeFromTop (28);
    volumeLabel.setBounds (top.removeFromLeft (44));
    volume.setBounds (top.removeFromLeft (280));
    area.removeFromTop (10);

    padsArea = area.removeFromTop (juce::jlimit (90, 140, area.getHeight() / 4));
    keysArea = area.removeFromTop (78);
    area.removeFromTop (14);
    editorArea = area;

    auto e = editorArea.reduced (12, 10);
    auto row = e.removeFromTop (30);
    nameEditor.setBounds (row.removeFromLeft (260));
    row.removeFromLeft (10);
    loadButton.setBounds (row.removeFromLeft (90));
    row.removeFromLeft (6);
    clearButton.setBounds (row.removeFromLeft (90));
    row.removeFromLeft (20);
    oneShotButton.setBounds (row.removeFromLeft (150));
    row.removeFromLeft (10);
    chokeLabel.setBounds (row.removeFromLeft (64));
    chokeBox.setBounds (row.removeFromLeft (80));

    e.removeFromTop (10);
    auto knobRow = e.removeFromBottom (104);
    e.removeFromBottom (8);
    waveArea = e;

    // つまみ: 音量・パン・ピッチ、テンポ合わせ、EQ、サチュレーション
    const int knobW = 84;
    auto placeKnob = [&] (int i) { knobs[(size_t) i]->setBounds (knobRow.removeFromLeft (knobW)); };

    for (int i = 0; i < 3; ++i)
        placeKnob (i);

    auto tempo = knobRow.removeFromLeft (110);
    tempoLabel.setBounds (tempo.removeFromTop (16).withTrimmedLeft (-2));
    tempo.removeFromTop (6);
    tempoButton.setBounds (tempo.removeFromTop (36).removeFromLeft (36));
    bpmLabel.setBounds (tempo.removeFromTop (24));
    knobRow.removeFromLeft (10);

    for (int i = 3; i < (int) knobs.size(); ++i)
    {
        placeKnob (i);

        if (i == 6)
            knobRow.removeFromLeft (16);
    }
}

juce::Rectangle<int> SamplerPanel::keyBounds (int index) const
{
    const float w = (float) keysArea.getWidth() / (float) numPads;
    const int x1 = keysArea.getX() + juce::roundToInt (w * (float) index);
    const int x2 = keysArea.getX() + juce::roundToInt (w * (float) (index + 1));
    return { x1, keysArea.getY(), x2 - x1, keysArea.getHeight() };
}

juce::Rectangle<int> SamplerPanel::padBounds (int index) const
{
    const auto key = keyBounds (index);
    return juce::Rectangle<int> (key.getX(), padsArea.getY(), key.getWidth(), padsArea.getHeight()).reduced (3, 0);
}

int SamplerPanel::padAt (juce::Point<int> p) const
{
    if (! padsArea.contains (p) && ! keysArea.contains (p))
        return -1;

    for (int i = 0; i < numPads; ++i)
        if (keyBounds (i).getX() <= p.x && p.x < keyBounds (i).getRight())
            return i;

    return -1;
}

int SamplerPanel::dropTarget (juce::Point<int> p) const
{
    if (const int i = padAt (p); i >= 0)
        return i;

    return editorArea.contains (p) ? selected : -1;
}

//==============================================================================
void SamplerPanel::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
    paintPads (g);
    paintKeys (g);

    g.setColour (Theme::panelLight.darker (0.03f));
    g.fillRoundedRectangle (editorArea.toFloat(), 10.0f);
    g.setColour (dropPad >= 0 && dropPad == selected && editorArea.contains (getMouseXYRelative()) ? Theme::selection : Theme::overlay (0.12f));
    g.drawRoundedRectangle (editorArea.toFloat().reduced (0.5f), 10.0f, 1.0f);
    paintWave (g);
}

void SamplerPanel::paintPads (juce::Graphics& g)
{
    auto* t = track();
    const auto colour = t != nullptr ? Theme::parseColour (t->color) : Theme::accent;
    const auto list = pads();

    for (int i = 0; i < numPads; ++i)
    {
        const auto& pad = list[(size_t) i];
        const auto r = padBounds (i).toFloat();
        const bool filled = ! pad.audioHash.empty();

        g.setColour (filled ? colour.withAlpha (i == pressedPad ? 0.6f : 0.3f) : Theme::panelLight.darker (0.06f));
        g.fillRoundedRectangle (r, 7.0f);

        const bool sel = i == selected;
        g.setColour (i == dropPad ? Theme::selection : sel ? Theme::accent : filled ? colour.darker (0.2f) : Theme::overlay (0.16f));
        g.drawRoundedRectangle (r.reduced (sel || i == dropPad ? 1.0f : 0.5f), 7.0f, sel || i == dropPad ? 2.2f : 1.0f);

        auto inner = r.reduced (6.0f, 5.0f);

        if (! filled)
        {
            // 空き: 真ん中に ＋
            g.setColour (Theme::overlay (0.3f));
            const auto c = inner.getCentre();
            g.fillRect (juce::Rectangle<float> (14.0f, 2.0f).withCentre (c));
            g.fillRect (juce::Rectangle<float> (2.0f, 14.0f).withCentre (c));
            continue;
        }

        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawFittedText (toJuce (pad.name), inner.removeFromTop (32.0f).toNearestInt(), juce::Justification::topLeft, 2, 0.8f);

        if (auto* thumb = ctx.audioCache.getThumbnail (ctx.document.getProjectDir(), pad.audioHash); thumb != nullptr && thumb->getTotalLength() > 0.0)
        {
            const auto [from, to] = trimRange (pad);
            AudioFiles::drawWaveform (g, *thumb, inner.reduced (0.0f, 2.0f), (double) from / collab::kSampleRate, (double) to / collab::kSampleRate,
                                      juce::Decibels::decibelsToGain ((float) pad.gainDb), Theme::clipWave (colour));
        }
        else if (! ctx.document.hasLocation() || ! AudioFiles::fileForHash (ctx.document.getProjectDir(), pad.audioHash).existsAsFile())
        {
            g.setColour (Theme::warning);
            g.setFont (juce::FontOptions (11.5f));
            g.drawFittedText ("未ダウンロード"_ju, inner.toNearestInt(), juce::Justification::centred, 2);
        }
    }
}

void SamplerPanel::paintKeys (juce::Graphics& g)
{
    // 白鍵だけを使う。黒鍵は位置の目安として薄く描く（押せない）
    for (int i = 0; i < numPads; ++i)
    {
        auto r = keyBounds (i).toFloat().reduced (1.0f, 0.0f);
        g.setColour (i == pressedPad ? Theme::accent.withAlpha (0.35f) : i == selected ? Theme::accent.withAlpha (0.12f) : juce::Colours::white);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (Theme::overlay (0.25f));
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);

        const int note = collab::samplerPadNote (i);

        if (note % 12 == 0)
        {
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.setFont (juce::FontOptions (11.5f));
            g.drawText (toJuce (collab::midiNoteName (note)), r.removeFromBottom (18.0f), juce::Justification::centred);
        }
    }

    for (int i = 0; i + 1 < numPads; ++i)
    {
        const int pc = collab::samplerPadNote (i) % 12;

        if (pc == 4 || pc == 11)   // E-F・B-C の間には黒鍵がない
            continue;

        const auto a = keyBounds (i);
        const float w = (float) a.getWidth() * 0.56f;
        const juce::Rectangle<float> black ((float) a.getRight() - w / 2.0f, (float) a.getY(), w, (float) a.getHeight() * 0.56f);
        g.setColour (juce::Colour (0xff5a5d63).withAlpha (0.55f));
        g.fillRoundedRectangle (black, 3.0f);
    }
}

void SamplerPanel::paintWave (juce::Graphics& g)
{
    const auto pad = selectedPad();
    const auto r = waveArea.toFloat();
    g.setColour (Theme::field);
    g.fillRoundedRectangle (r, 6.0f);

    if (pad.audioHash.empty())
    {
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("オーディオをドロップ"_ju, r, juce::Justification::centred);
        return;
    }

    auto* thumb = ctx.audioCache.getThumbnail (ctx.document.getProjectDir(), pad.audioHash);
    const auto length = sourceLength (pad);

    if (thumb == nullptr || length <= 0)
        return;

    auto* t = track();
    const auto colour = t != nullptr ? Theme::parseColour (t->color) : Theme::accent;
    AudioFiles::drawWaveform (g, *thumb, r.reduced (0.0f, 4.0f), 0.0, (double) length / collab::kSampleRate,
                              juce::Decibels::decibelsToGain ((float) pad.gainDb), Theme::clipWave (colour));

    // 使う範囲の外は暗く、端に取っ手
    auto [from, to] = trimRange (pad);

    if (trimDrag != TrimDrag::none)
        std::tie (from, to) = std::make_pair (dragStart, dragEnd);

    const float x1 = sampleToX (from, length), x2 = sampleToX (to, length);
    g.setColour (Theme::panel.withAlpha (0.7f));
    g.fillRect (juce::Rectangle<float> (r.getX(), r.getY(), x1 - r.getX(), r.getHeight()));
    g.fillRect (juce::Rectangle<float> (x2, r.getY(), r.getRight() - x2, r.getHeight()));

    for (float x : { x1, x2 })
    {
        g.setColour (Theme::accent);
        g.fillRect (juce::Rectangle<float> (x - 1.0f, r.getY(), 2.0f, r.getHeight()));
        g.fillRoundedRectangle (juce::Rectangle<float> (10.0f, 16.0f).withCentre ({ x, r.getY() + 10.0f }), 3.0f);
    }
}

//==============================================================================
collab::SampleCount SamplerPanel::sourceLength (const collab::SamplerPad& pad) const
{
    return pad.audioHash.empty() ? 0 : (collab::SampleCount) ctx.audioCache.getLengthSamples (ctx.document.getProjectDir(), pad.audioHash);
}

std::pair<collab::SampleCount, collab::SampleCount> SamplerPanel::trimRange (const collab::SamplerPad& pad) const
{
    const auto length = sourceLength (pad);
    const auto from = juce::jlimit<collab::SampleCount> (0, juce::jmax<collab::SampleCount> (0, length - 1), pad.startSamples);
    const auto to = pad.endSamples > from ? juce::jmin (pad.endSamples, length) : length;
    return { from, to };
}

float SamplerPanel::sampleToX (collab::SampleCount s, collab::SampleCount length) const
{
    return (float) waveArea.getX() + (float) waveArea.getWidth() * (float) s / (float) juce::jmax<collab::SampleCount> (1, length);
}

collab::SampleCount SamplerPanel::xToSample (float x, collab::SampleCount length) const
{
    const float pos = juce::jlimit (0.0f, 1.0f, (x - (float) waveArea.getX()) / (float) juce::jmax (1, waveArea.getWidth()));
    return (collab::SampleCount) std::llround ((double) pos * (double) length);
}

//==============================================================================
void SamplerPanel::mouseMove (const juce::MouseEvent& e)
{
    const auto pad = selectedPad();
    bool nearHandle = false;

    if (waveArea.contains (e.getPosition()) && ! pad.audioHash.empty())
    {
        const auto length = sourceLength (pad);
        const auto [from, to] = trimRange (pad);
        nearHandle = std::abs ((float) e.x - sampleToX (from, length)) < 8.0f || std::abs ((float) e.x - sampleToX (to, length)) < 8.0f;
    }

    setMouseCursor (nearHandle ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void SamplerPanel::mouseDown (const juce::MouseEvent& e)
{
    // 使う範囲の端をつかむ（近い方）
    if (waveArea.contains (e.getPosition()))
    {
        const auto pad = selectedPad();

        if (pad.audioHash.empty())
            return;

        const auto length = sourceLength (pad);
        std::tie (dragStart, dragEnd) = trimRange (pad);
        const float ds = std::abs ((float) e.x - sampleToX (dragStart, length)), de = std::abs ((float) e.x - sampleToX (dragEnd, length));
        trimDrag = ds <= de ? TrimDrag::start : TrimDrag::end;
        mouseDrag (e);
        return;
    }

    const int index = padAt (e.getPosition());

    if (index < 0)
        return;

    // 黒鍵の所（押せない）
    if (keysArea.contains (e.getPosition()) && e.y < keysArea.getY() + keysArea.getHeight() * 56 / 100)
    {
        const auto k = keyBounds (index);
        const int edge = juce::jmin (e.x - k.getX(), k.getRight() - e.x);

        if (edge < k.getWidth() * 28 / 100)
            return;
    }

    if (e.mods.isPopupMenu())
    {
        selectPad (index, false);
        return showPadMenu (index);
    }

    // 試し弾き（パッドは押した高さで強さを変える: 上ほど強い）
    int velocity = 100;

    if (padsArea.contains (e.getPosition()))
        velocity = juce::jlimit (30, 127, 127 - (int) (60.0f * (float) (e.y - padsArea.getY()) / (float) juce::jmax (1, padsArea.getHeight())));

    selectPad (index, true, velocity);
}

void SamplerPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (trimDrag == TrimDrag::none)
        return;

    const auto length = sourceLength (selectedPad());
    const auto minLength = (collab::SampleCount) (collab::kSampleRate / 100);
    const auto s = xToSample ((float) e.x, length);

    if (trimDrag == TrimDrag::start)
        dragStart = juce::jlimit<collab::SampleCount> (0, juce::jmax<collab::SampleCount> (0, dragEnd - minLength), s);
    else
        dragEnd = juce::jlimit<collab::SampleCount> (juce::jmin (length, dragStart + minLength), length, s);

    repaint (waveArea);
}

void SamplerPanel::mouseUp (const juce::MouseEvent&)
{
    if (trimDrag == TrimDrag::none)
        return;

    trimDrag = TrimDrag::none;
    const auto length = sourceLength (selectedPad());
    const auto start = dragStart, end = dragEnd >= length ? (collab::SampleCount) 0 : dragEnd;
    const auto pad = selectedPad();

    if (start != pad.startSamples || end != pad.endSamples)
        editSelected ("パッドの範囲"_ju, [start, end] (collab::SamplerPad& p) { p.startSamples = start; p.endSamples = end; });

    repaint();
}

void SamplerPanel::selectPad (int index, bool play, int velocity)
{
    if (nameEditor.hasKeyboardFocus (true))
        unfocusAllComponents();   // 名前の入力を確定してから切り替える

    selected = index;

    if (play)
    {
        ctx.engine.previewNote (trackId, collab::samplerPadNote (index), velocity);
        pressedPad = index;
        juce::Timer::callAfterDelay (150, [safe = juce::Component::SafePointer<SamplerPanel> (this)] { if (safe != nullptr) { safe->pressedPad = -1; safe->repaint(); } });
    }

    refreshEditor();
    repaint();
}

//==============================================================================
void SamplerPanel::editPads (const juce::String& description, std::function<void (std::vector<collab::SamplerPad>&)> fn, const juce::String& merge)
{
    ctx.editTrack (trackId, description, [fn] (collab::Track& t)
    {
        if (! t.instrument)
            return;

        auto list = collab::samplerPads (t.instrument->params);
        fn (list);
        t.instrument->params = collab::withSamplerPads (t.instrument->params, list);
    }, merge);
}

void SamplerPanel::editSelected (const juce::String& description, std::function<void (collab::SamplerPad&)> fn, const juce::String& merge)
{
    editPads (description, [index = selected, fn] (std::vector<collab::SamplerPad>& list) { fn (list[(size_t) index]); }, merge);
}

void SamplerPanel::toggleTempo()
{
    const auto pad = selectedPad();

    if (pad.sourceBpm > 0.0)
        return editSelected ("テンポ合わせ"_ju, [] (collab::SamplerPad& p) { p.sourceBpm = 0.0; });

    // 元のテンポは名前か長さから。違っていたら BPM の欄で直す
    const auto [from, to] = trimRange (pad);
    const double bpm = collab::guessSourceBpm (pad.name, (double) (to - from) / collab::kSampleRate, ctx.document.getTempoMap().bpmAtTick (0));
    editSelected ("テンポ合わせ"_ju, [bpm] (collab::SamplerPad& p) { p.sourceBpm = bpm; });
}

bool SamplerPanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;m4a"))
            return true;

    return false;
}

void SamplerPanel::filesDropped (const juce::StringArray& files, int x, int y)
{
    const int index = dropTarget ({ x, y });
    dropPad = -1;
    repaint();

    if (index >= 0)
    {
        selectPad (index, false);
        assignFiles (index, files);
    }
}

void SamplerPanel::chooseFiles (int index)
{
    chooser = std::make_unique<juce::FileChooser> ("オーディオを読み込む"_ju, juce::File(), AudioFiles::supportedWildcard());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this, index] (const juce::FileChooser& fc)
    {
        juce::StringArray paths;

        for (auto& f : fc.getResults())
            paths.add (f.getFullPathName());

        if (! paths.isEmpty())
            assignFiles (index, paths);
    });
}

void SamplerPanel::assignFiles (int firstPad, const juce::StringArray& files)
{
    if (! ctx.document.hasLocation())
        return Dialogs::showInfo ("サンプラー"_ju, "先に曲を保存してください。"_ju);

    // 複数のファイルは、そのパッドから順に入れる
    std::vector<std::pair<int, AudioFiles::Imported>> imported;
    juce::StringArray errors;
    int index = firstPad;

    for (auto& path : files)
    {
        if (index >= numPads)
            break;

        const juce::File f (path);
        AudioFiles::Imported im;

        if (auto r = AudioFiles::importFile (f, ctx.document.getProjectDir().getChildFile ("audio"), im, f.getFileNameWithoutExtension()); r.failed())
        {
            errors.add (r.getErrorMessage());
            continue;
        }

        im.displayName = f.getFileNameWithoutExtension();
        imported.push_back ({ index++, im });
    }

    if (! errors.isEmpty())
        Dialogs::showError ("読み込めないファイルがありました"_ju, errors.joinIntoString ("\n"));

    if (imported.empty())
        return;

    // 入れ替えたパッドは、範囲・テンポなど前の音に合わせた設定を戻す（名前は新しいファイルの名前）
    editPads ("サンプラーにオーディオを入れる"_ju, [imported] (std::vector<collab::SamplerPad>& list)
    {
        for (auto& [i, im] : imported)
        {
            auto& pad = list[(size_t) i];
            const auto keep = pad;
            pad = {};
            pad.note = keep.note;
            pad.gainDb = keep.gainDb;
            pad.pan = keep.pan;
            pad.oneShot = keep.oneShot;
            pad.chokeGroup = keep.chokeGroup;
            pad.audioHash = im.hash;
            pad.name = toStd (im.displayName);
        }
    });
}

void SamplerPanel::showPadMenu (int index)
{
    const auto pad = pads()[(size_t) index];
    juce::PopupMenu m;
    m.addItem ("オーディオを読み込む…"_ju, [this, index] { chooseFiles (index); });
    m.addItem ("名前を変更"_ju, ! pad.audioHash.empty(), false, [this]
    {
        nameEditor.grabKeyboardFocus();
        nameEditor.selectAll();
    });
    m.addSeparator();
    m.addItem ("空にする"_ju, ! pad.audioHash.empty(), false, [this] { clearButton.triggerClick(); });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}
