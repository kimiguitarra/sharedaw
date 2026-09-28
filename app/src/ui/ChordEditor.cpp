#include "ChordEditor.h"

#include "Theme.h"

namespace
{
    const char* typeLabels[] = { "maj", "min", "dim", "sus4", "sus2", "aug" };
    const char* tensionLabels[] = { "7", "j7", "b9", "9", "#9", "11", "b5/#11", "#5/b13", "6/13" };

    constexpr int rowH = 24;
    constexpr int fifths[] = { 0, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10, 5 };   // 五度圏の順（C が上）

    juce::String noteName (int pc, const std::optional<collab::chord::Key>& key)
    {
        return toJuce (collab::chord::spellPitch (pc, key.value_or (collab::chord::Key { 5, false })));   // キー未設定はフラット系
    }

    void styleListButton (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff23262b));
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd9dce1));
        b.setColour (juce::TextButton::textColourOffId, Theme::text);
        b.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        b.setClickingTogglesState (false);
    }
}

//==============================================================================
/** 五度圏（外側がメジャー、内側が平行調のマイナー）。クリックでそのコード。 */
class ChordEditor::CircleView  : public juce::Component
{
public:
    explicit CircleView (ChordEditor& o) : owner (o) {}

    void paint (juce::Graphics& g) override
    {
        const auto centre = getLocalBounds().toFloat().getCentre();
        const float outer = juce::jmin ((float) getWidth(), (float) getHeight()) * 0.5f - 6.0f;
        const float middle = outer * 0.68f, inner = outer * 0.38f;

        // キーの主要なコード（Ⅰ・Ⅳ・Ⅴ とその平行調）の位置
        int keyIndex = -1;

        if (owner.key)
        {
            const int majorTonic = owner.key->minor ? (owner.key->tonic + 3) % 12 : owner.key->tonic;

            for (int i = 0; i < 12; ++i)
                if (fifths[i] == majorTonic)
                    keyIndex = i;
        }

        const int currentRoot = collab::chord::pitchClass (owner.current.root);
        const bool currentMinor = owner.current.quality.rfind ("m", 0) == 0 && owner.current.quality.rfind ("maj", 0) != 0;

        for (int ring = 0; ring < 2; ++ring)
        {
            const float r1 = ring == 0 ? middle : inner, r2 = ring == 0 ? outer : middle;

            for (int i = 0; i < 12; ++i)
            {
                const float a1 = juce::MathConstants<float>::twoPi * ((float) i - 0.5f) / 12.0f;
                const float a2 = juce::MathConstants<float>::twoPi * ((float) i + 0.5f) / 12.0f;
                juce::Path p;
                p.addPieSegment (centre.x - r2, centre.y - r2, r2 * 2, r2 * 2, a1, a2, r1 / r2);

                const int pc = ring == 0 ? fifths[i] : (fifths[i] + 9) % 12;
                const bool inKey = keyIndex >= 0 && (std::abs (i - keyIndex) <= 1 || std::abs (i - keyIndex) == 11);
                const bool isCurrent = ! owner.currentNoChord && pc == currentRoot && (ring == 1) == currentMinor;
                const bool hovered = hover == ring * 12 + i;

                auto fill = inKey ? juce::Colour (0xff4a5a38) : juce::Colour (0xff2a2e34);

                if (hovered)   fill = fill.brighter (0.25f);
                if (isCurrent) fill = Theme::accent.darker (0.2f);

                g.setColour (fill);
                g.fillPath (p);
                g.setColour (Theme::background);
                g.strokePath (p, juce::PathStrokeType (1.5f));

                const float rm = (r1 + r2) * 0.5f;
                const auto pos = centre.getPointOnCircumference (rm, juce::MathConstants<float>::twoPi * (float) i / 12.0f);
                g.setColour (isCurrent ? juce::Colours::black : Theme::text);
                g.setFont (juce::FontOptions (ring == 0 ? 15.0f : 12.5f, juce::Font::bold));
                g.drawText (noteName (pc, owner.key) + (ring == 1 ? "m" : ""),
                            juce::Rectangle<float> (60.0f, 20.0f).withCentre (pos), juce::Justification::centred);
            }
        }

        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (17.5f, juce::Font::bold));
        g.drawText (owner.currentNoChord ? juce::String ("X") : toJuce (collab::chord::format (owner.current)),
                    juce::Rectangle<float> (inner * 1.8f, 24.0f).withCentre (centre), juce::Justification::centred);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int h = sectorAt (e.position);

        if (h != hover)
        {
            hover = h;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hover = -1;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int s = sectorAt (e.position);

        if (s < 0)
            return;

        const bool minor = s >= 12;
        const int pc = minor ? (fifths[s - 12] + 9) % 12 : fifths[s];
        owner.setChord ({ toStd (noteName (pc, owner.key)), minor ? "m" : "maj", {}, std::nullopt });
    }

private:
    ChordEditor& owner;
    int hover = -1;

    /** 0〜11 = 外側（メジャー）、12〜23 = 内側（マイナー）、-1 = なし */
    int sectorAt (juce::Point<float> p) const
    {
        const auto centre = getLocalBounds().toFloat().getCentre();
        const float outer = juce::jmin ((float) getWidth(), (float) getHeight()) * 0.5f - 6.0f;
        const float middle = outer * 0.68f, inner = outer * 0.38f;
        const float d = p.getDistanceFrom (centre);

        if (d < inner || d > outer)
            return -1;

        float angle = std::atan2 (p.x - centre.x, centre.y - p.y);   // 上が 0、時計回り

        if (angle < 0)
            angle += juce::MathConstants<float>::twoPi;

        const int i = (int) std::floor (angle / juce::MathConstants<float>::twoPi * 12.0f + 0.5f) % 12;
        return d >= middle ? i : 12 + i;
    }
};

//==============================================================================
ChordEditor::Columns ChordEditor::columnsFor (const collab::chord::Chord& c)
{
    Columns col;
    col.root = std::max (0, collab::chord::pitchClass (c.root));
    const auto& q = c.quality;

    if      (q == "maj" || q == "add9") { col.type = Type::maj; }
    else if (q == "7")    { col.type = Type::maj;  col.seventh = Seventh::dominant; }
    else if (q == "maj7") { col.type = Type::maj;  col.seventh = Seventh::major; }
    else if (q == "6")    { col.type = Type::maj;  col.six13 = true; }
    else if (q == "m")    { col.type = Type::min; }
    else if (q == "m7")   { col.type = Type::min;  col.seventh = Seventh::dominant; }
    else if (q == "mM7")  { col.type = Type::min;  col.seventh = Seventh::major; }
    else if (q == "m6")   { col.type = Type::min;  col.six13 = true; }
    else if (q == "m7b5") { col.type = Type::min;  col.seventh = Seventh::dominant; col.b5s11 = true; }
    else if (q == "dim")  { col.type = Type::dim; }
    else if (q == "dim7") { col.type = Type::dim;  col.six13 = true; }
    else if (q == "aug")  { col.type = Type::aug; }
    else if (q == "sus2") { col.type = Type::sus2; }
    else if (q == "sus4") { col.type = Type::sus4; }
    else if (q == "7sus4") { col.type = Type::sus4; col.seventh = Seventh::dominant; }

    if (q == "add9")
        col.n9 = true;

    for (auto& t : c.tensions)
    {
        if (t == "b9")  col.b9 = true;
        if (t == "9")   col.n9 = true;
        if (t == "#9")  col.s9 = true;
        if (t == "11")  col.n11 = true;
        if (t == "#11") col.b5s11 = true;
        if (t == "b13") col.s5b13 = true;
        if (t == "13")  col.six13 = true;
    }

    if (c.bass)
        col.bass = collab::chord::pitchClass (*c.bass);

    return col;
}

collab::chord::Chord ChordEditor::chordFor (const Columns& col, const std::optional<collab::chord::Key>& key)
{
    collab::chord::Chord c;
    c.root = toStd (noteName (col.root, key));
    const bool has7 = col.seventh != Seventh::none;
    bool sixthUsed = false, b5Used = false;

    switch (col.type)
    {
        case Type::maj:
            c.quality = col.seventh == Seventh::dominant ? "7" : col.seventh == Seventh::major ? "maj7" : col.six13 ? "6" : "maj";
            sixthUsed = ! has7 && col.six13;
            break;
        case Type::min:
            if (col.seventh == Seventh::dominant && col.b5s11) { c.quality = "m7b5"; b5Used = true; }
            else c.quality = col.seventh == Seventh::dominant ? "m7" : col.seventh == Seventh::major ? "mM7" : col.six13 ? "m6" : "m";
            sixthUsed = ! has7 && col.six13;
            break;
        case Type::dim:
            c.quality = col.seventh != Seventh::none ? "m7b5" : col.six13 ? "dim7" : "dim";
            sixthUsed = ! has7 && col.six13;
            break;
        case Type::sus4:  c.quality = has7 ? "7sus4" : "sus4"; break;
        case Type::sus2:  c.quality = "sus2"; break;
        case Type::aug:   c.quality = "aug"; break;
    }

    // テンション（allTensions の順）
    if (col.b9)                    c.tensions.push_back ("b9");
    if (col.n9)                    c.tensions.push_back ("9");
    if (col.s9)                    c.tensions.push_back ("#9");
    if (col.n11)                   c.tensions.push_back ("11");
    if (col.b5s11 && ! b5Used)     c.tensions.push_back ("#11");
    if (col.s5b13)                 c.tensions.push_back ("b13");
    if (col.six13 && ! sixthUsed)  c.tensions.push_back ("13");

    // 7th なしの 9 だけなら add9
    if (c.quality == "maj" && c.tensions == std::vector<std::string> { "9" })
    {
        c.quality = "add9";
        c.tensions.clear();
    }

    if (col.bass >= 0 && col.bass != col.root)
        c.bass = toStd (noteName (col.bass, key));

    return c;
}

//==============================================================================
ChordEditor::ChordEditor (const juce::String& initialText, std::optional<collab::chord::Key> k,
                          std::function<void (Result)> ok, std::function<void()> del)
    : key (k), onOk (std::move (ok)), onDelete (std::move (del))
{
    textEditor.setFont (juce::FontOptions (20.0f));
    textEditor.setJustification (juce::Justification::centredLeft);
    textEditor.setTextToShowWhenEmpty (key ? "例: 6、4M7、57、b7、1/3（ディグリー）や G7(9,13)、Am7/G、X"_ju
                                           : "例: G7(9,13)、Am7/G、X"_ju, Theme::textDim);
    textEditor.onTextChange = [this] { textChanged(); };
    textEditor.onReturnKey = [this] { okButton.triggerClick(); };
    textEditor.onEscapeKey = [this] { close(); };
    addAndMakeVisible (textEditor);

    preview.setFont (juce::FontOptions (14.5f));
    addAndMakeVisible (preview);

    // ディグリーのボタン（キーが決まっているとき）: 音階の四和音
    if (key)
    {
        for (int d = 1; d <= 7; ++d)
        {
            const auto text = collab::chord::degreeToChordText (std::to_string (d), *key);
            const auto parsed = text ? collab::chord::parse (*text) : collab::chord::ParseResult();

            if (! parsed.chord)
                continue;

            auto* b = degreeButtons.add (new juce::TextButton (toJuce (collab::chord::degreeName (*parsed.chord, *key))));
            b->setTooltip (toJuce (collab::chord::format (*parsed.chord)));
            b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff3a3f2e));
            b->onClick = [this, chord = *parsed.chord] { setChord (chord); };
            addAndMakeVisible (b);
        }
    }

    // タブ
    for (auto* b : { &editorTab, &circleTab })
    {
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonOnColourId, Theme::panelLight);
        addAndMakeVisible (b);
    }

    editorTab.setButtonText ("エディター"_ju);
    circleTab.setButtonText ("五度圏"_ju);
    editorTab.onClick = [this] { setPage (false); };
    circleTab.onClick = [this] { setPage (true); };

    // エディター: ルート・タイプ・テンション・ベース
    for (int i = 0; i < 12; ++i)
    {
        auto* b = rootButtons.add (new juce::TextButton (noteName (i, key)));
        styleListButton (*b);
        b->onClick = [this, i] { editColumns ([i] (Columns& c) { c.root = i; }); };
        addAndMakeVisible (b);
    }

    for (int i = 0; i < 6; ++i)
    {
        auto* b = typeButtons.add (new juce::TextButton (typeLabels[i]));
        styleListButton (*b);
        b->onClick = [this, i] { editColumns ([i] (Columns& c) { c.type = (Type) i; }); };
        addAndMakeVisible (b);
    }

    for (int i = 0; i < 9; ++i)
    {
        auto* b = tensionButtons.add (new juce::TextButton (tensionLabels[i]));
        styleListButton (*b);
        b->onClick = [this, i]
        {
            editColumns ([i] (Columns& c)
            {
                switch (i)
                {
                    case 0: c.seventh = c.seventh == Seventh::dominant ? Seventh::none : Seventh::dominant; break;
                    case 1: c.seventh = c.seventh == Seventh::major ? Seventh::none : Seventh::major; break;
                    case 2: c.b9 = ! c.b9; break;
                    case 3: c.n9 = ! c.n9; break;
                    case 4: c.s9 = ! c.s9; break;
                    case 5: c.n11 = ! c.n11; break;
                    case 6: c.b5s11 = ! c.b5s11; break;
                    case 7: c.s5b13 = ! c.s5b13; break;
                    default: c.six13 = ! c.six13; break;
                }
            });
        };
        addAndMakeVisible (b);
    }

    for (int i = -1; i < 12; ++i)
    {
        auto* b = bassButtons.add (new juce::TextButton (i < 0 ? "なし"_ju : noteName (i, key)));
        styleListButton (*b);
        b->onClick = [this, i] { editColumns ([i] (Columns& c) { c.bass = i; }); };
        addAndMakeVisible (b);
    }

    circle = std::make_unique<CircleView> (*this);
    addChildComponent (*circle);

    noChordButton.setButtonText ("X（ノーコード）"_ju);
    noChordButton.setTooltip ("ノーコード"_ju);
    noChordButton.onClick = [this]
    {
        currentNoChord = true;
        setFromStructure();
    };
    addAndMakeVisible (noChordButton);

    okButton.setButtonText ("OK");
    okButton.onClick = [this]
    {
        if (! valid)
            return;

        Result r;
        r.noChord = currentNoChord;

        if (! currentNoChord)
            r.chord = current;

        r.text = currentNoChord ? juce::String ("X") : toJuce (collab::chord::format (current));
        auto cb = onOk;
        close();

        if (cb)
            cb (r);
    };
    addAndMakeVisible (okButton);

    cancelButton.setButtonText ("キャンセル"_ju);
    cancelButton.onClick = [this] { close(); };
    addAndMakeVisible (cancelButton);

    deleteButton.setButtonText ("削除"_ju);
    deleteButton.setVisible (onDelete != nullptr);
    deleteButton.onClick = [this]
    {
        auto cb = onDelete;
        close();

        if (cb)
            cb();
    };
    addChildComponent (deleteButton);

    // 空のコード（新しく置いたもの）はキーの Ⅰ（なければ C）から始める
    auto initial = initialText;

    if (initial.isEmpty())
        initial = key ? toJuce (collab::chord::keyName (*key)) : juce::String ("C");

    textEditor.setText (initial, juce::sendNotification);
    textChanged();
    setPage (false);
    setSize (560, key ? 500 : 466);
}

ChordEditor::~ChordEditor() = default;

void ChordEditor::show (const juce::String& initialText, std::optional<collab::chord::Key> key,
                        std::function<void (Result)> onOk, std::function<void()> onDelete)
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new ChordEditor (initialText, key, std::move (onOk), std::move (onDelete)));
    o.dialogTitle = "コードの編集"_ju;
    o.dialogBackgroundColour = Theme::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;

    if (auto* w = o.launchAsync())
        if (auto* editor = dynamic_cast<ChordEditor*> (w->getContentComponent()))
        {
            editor->textEditor.grabKeyboardFocus();
            editor->textEditor.selectAll();
        }
}

void ChordEditor::close()
{
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
        dw->exitModalState (0);
}

void ChordEditor::setPage (bool circlePage)
{
    showCircle = circlePage;
    editorTab.setToggleState (! circlePage, juce::dontSendNotification);
    circleTab.setToggleState (circlePage, juce::dontSendNotification);

    for (auto* list : { &rootButtons, &typeButtons, &tensionButtons, &bassButtons })
        for (auto* b : *list)
            b->setVisible (! circlePage);

    circle->setVisible (circlePage);
    repaint();
}

void ChordEditor::textChanged()
{
    if (updating)
        return;

    // キーが決まっていれば、ディグリー（6 など）をそのキーのコードに変える
    auto input = toStd (textEditor.getText());
    std::optional<std::string> fromDegree;

    if (key)
        fromDegree = collab::chord::degreeToChordText (input, *key);

    auto r = collab::chord::parse (fromDegree ? *fromDegree : input);
    valid = r.ok();

    if (r.noChord)
    {
        currentNoChord = true;
        preview.setText ("→ X（ノーコード: この区間は発音しません）"_ju, juce::dontSendNotification);
        preview.setColour (juce::Label::textColourId, Theme::text);
    }
    else if (r.chord)
    {
        currentNoChord = false;
        current = *r.chord;
        preview.setText (previewText(), juce::dontSendNotification);
        preview.setColour (juce::Label::textColourId, Theme::text);
    }
    else
    {
        preview.setText (toJuce (r.error), juce::dontSendNotification);
        preview.setColour (juce::Label::textColourId, Theme::warning);
    }

    okButton.setEnabled (valid);
    refreshButtons();
}

juce::String ChordEditor::previewText() const
{
    auto s = "→ "_ju + toJuce (collab::chord::format (current));

    if (key)
        s += "　（キー "_ju + toJuce (collab::chord::keyName (*key)) + " の "_ju + toJuce (collab::chord::degreeName (current, *key)) + "）"_ju;

    return s;
}

void ChordEditor::setChord (const collab::chord::Chord& c)
{
    current = c;
    currentNoChord = false;
    setFromStructure();
}

void ChordEditor::editColumns (std::function<void (Columns&)> fn)
{
    auto col = currentNoChord ? Columns() : columnsFor (current);
    fn (col);
    setChord (chordFor (col, key));
}

void ChordEditor::setFromStructure()
{
    const juce::ScopedValueSetter<bool> svs (updating, true);
    textEditor.setText (currentNoChord ? juce::String ("X") : toJuce (collab::chord::format (current)), false);
    valid = true;
    okButton.setEnabled (true);
    preview.setColour (juce::Label::textColourId, Theme::text);
    preview.setText (currentNoChord ? "→ X（ノーコード: この区間は発音しません）"_ju : previewText(), juce::dontSendNotification);
    refreshButtons();
}

void ChordEditor::refreshButtons()
{
    const juce::ScopedValueSetter<bool> svs (updating, true);
    const bool on = ! currentNoChord && valid;
    const auto col = columnsFor (current);

    for (int i = 0; i < rootButtons.size(); ++i)
        rootButtons[i]->setToggleState (on && col.root == i, juce::dontSendNotification);

    for (int i = 0; i < typeButtons.size(); ++i)
        typeButtons[i]->setToggleState (on && (int) col.type == i, juce::dontSendNotification);

    const bool tensionStates[] = { col.seventh == Seventh::dominant, col.seventh == Seventh::major, col.b9, col.n9, col.s9,
                                   col.n11, col.b5s11, col.s5b13, col.six13 };

    for (int i = 0; i < tensionButtons.size(); ++i)
        tensionButtons[i]->setToggleState (on && tensionStates[i], juce::dontSendNotification);

    for (int i = 0; i < bassButtons.size(); ++i)
        bassButtons[i]->setToggleState (on && col.bass == i - 1, juce::dontSendNotification);

    for (auto* b : degreeButtons)
        b->setToggleState (false, juce::dontSendNotification);

    noChordButton.setToggleState (currentNoChord, juce::dontSendNotification);
    circle->repaint();
}

void ChordEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);

    if (! degreeButtons.isEmpty())
    {
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (15.0f));
        g.drawText ("ディグリー"_ju, 10, degreeButtons[0]->getY(), 80, degreeButtons[0]->getHeight(), juce::Justification::centredLeft);
    }

    // 列の見出し
    if (! showCircle && ! rootButtons.isEmpty())
    {
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (14.0f));
        const int y = rootButtons[0]->getY() - 16;
        const juce::String titles[] = { "ルート"_ju, "タイプ"_ju, "テンション"_ju, "ベース"_ju };
        juce::Component* firsts[] = { rootButtons[0], typeButtons[0], tensionButtons[0], bassButtons[0] };

        for (int i = 0; i < 4; ++i)
            g.drawText (titles[i], firsts[i]->getX(), y, firsts[i]->getWidth(), 14, juce::Justification::centred);
    }
}

void ChordEditor::resized()
{
    auto area = getLocalBounds().reduced (10);
    textEditor.setBounds (area.removeFromTop (34));
    preview.setBounds (area.removeFromTop (22));
    area.removeFromTop (4);

    if (! degreeButtons.isEmpty())
    {
        auto row = area.removeFromTop (28).withTrimmedLeft (80);
        const int w = row.getWidth() / 7;

        for (auto* b : degreeButtons)
            b->setBounds (row.removeFromLeft (w).reduced (1));

        area.removeFromTop (6);
    }

    auto tabs = area.removeFromTop (26);
    editorTab.setBounds (tabs.removeFromLeft (110));
    tabs.removeFromLeft (4);
    circleTab.setBounds (tabs.removeFromLeft (110));

    auto bottom = area.removeFromBottom (30);
    okButton.setBounds (bottom.removeFromRight (100));
    bottom.removeFromRight (6);
    cancelButton.setBounds (bottom.removeFromRight (100));
    deleteButton.setBounds (bottom.removeFromLeft (80));
    bottom.removeFromLeft (8);
    noChordButton.setBounds (bottom.removeFromLeft (140));
    area.removeFromBottom (8);

    // ページ（4 列 / 五度圏）
    area.removeFromTop (20);
    circle->setBounds (area.withSizeKeepingCentre (juce::jmin (area.getWidth(), area.getHeight() + 16), area.getHeight()));

    const int colW = area.getWidth() / 4;
    juce::OwnedArray<juce::TextButton>* lists[] = { &rootButtons, &typeButtons, &tensionButtons, &bassButtons };

    for (auto* list : lists)
    {
        auto col = area.removeFromLeft (colW).reduced (6, 0);
        const int h = juce::jmin (rowH, col.getHeight() / juce::jmax (1, list->size()));

        for (auto* b : *list)
            b->setBounds (col.removeFromTop (h).reduced (0, 1));
    }
}
