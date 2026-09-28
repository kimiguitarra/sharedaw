#include "Theme.h"

namespace Theme
{

namespace
{
    const juce::uint32 palette[] = { 0xffFF6B9E, 0xff5EC8FF, 0xff7CE38B, 0xffFFB547, 0xffC38BFF,
                                     0xff3DDCC4, 0xffFF8A65, 0xffB4E05A, 0xff8C9EFF, 0xffFFE066 };
}

juce::Colour trackColour (int index)
{
    return juce::Colour (palette[(size_t) juce::jmax (0, index) % std::size (palette)]);
}

juce::String trackColourHex (int index)
{
    return "#" + trackColour (index).toDisplayString (false).toUpperCase();
}

juce::Colour parseColour (const std::string& hex, juce::Colour fallback)
{
    auto s = toJuce (hex).trimCharactersAtStart ("#");

    if (s.length() != 6 || ! s.containsOnly ("0123456789abcdefABCDEF"))
        return fallback;

    return juce::Colour ((juce::uint32) (0xff000000u | (juce::uint32) s.getHexValue32()));
}

juce::Colour personColour (const juce::String& key)
{
    const juce::uint32 colours[] = { 0xffFF6FB5, 0xff5EE7FF, 0xffFFB547, 0xff7CE38B, 0xffA78BFA, 0xffFF8A65, 0xff60A5FA, 0xff3DDCC4 };
    return juce::Colour (colours[(size_t) (key.hashCode64() & 0x7fffffff) % std::size (colours)]);
}

void drawCard (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour stripe)
{
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillRoundedRectangle (area.translated (0.0f, 2.0f), cornerRadius + 2.0f);
    g.setColour (panel);
    g.fillRoundedRectangle (area, cornerRadius + 2.0f);

    juce::Path top;
    top.addRoundedRectangle (area.getX(), area.getY(), area.getWidth(), 5.0f, cornerRadius + 2.0f, cornerRadius + 2.0f, true, true, false, false);
    g.setColour (stripe);
    g.fillPath (top);

    g.setColour (stripe.withAlpha (0.25f));
    g.drawRoundedRectangle (area.reduced (0.5f), cornerRadius + 2.0f, 1.0f);
}

void drawAvatar (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& name)
{
    const auto c = personColour (name);
    g.setColour (c);
    g.fillEllipse (area);
    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.setFont (juce::FontOptions (area.getHeight() * 0.55f, juce::Font::bold));
    g.drawText (name.isNotEmpty() ? name.substring (0, 1).toUpperCase() : juce::String ("?"), area, juce::Justification::centred);
}

void drawPill (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour fill, const juce::String& label, float fontSize)
{
    g.setColour (fill);
    g.fillRoundedRectangle (area, area.getHeight() * 0.5f);
    g.setColour (fill.getPerceivedBrightness() > 0.55f ? juce::Colour (0xff1a1b2e) : juce::Colours::white);
    g.setFont (juce::FontOptions (fontSize, juce::Font::bold));
    g.drawText (label, area, juce::Justification::centred);
}

LookAndFeel::LookAndFeel()
{
    setColourScheme ({ panel, background, panelLight, gridBar, text, accent, juce::Colours::black, panelLight, text });

    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::TextButton::buttonColourId, panelLight);
    setColour (juce::TextButton::buttonOnColourId, accent.darker (0.15f));
    setColour (juce::TextButton::textColourOnId, juce::Colour (0xff15162a));
    setColour (juce::ComboBox::backgroundColourId, panelLight);
    setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.35f));
    setColour (juce::Label::textColourId, text);
    setColour (juce::Slider::thumbColourId, accent);
    setColour (juce::Slider::trackColourId, accent.withAlpha (0.55f));
    setColour (juce::Slider::backgroundColourId, background);
    setColour (juce::ScrollBar::thumbColourId, gridBar);
    setColour (juce::TooltipWindow::backgroundColourId, panelLight);
    setColour (juce::TextEditor::backgroundColourId, background);
    setColour (juce::TextEditor::outlineColourId, gridBeat);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::ProgressBar::backgroundColourId, background);
    setColour (juce::ProgressBar::foregroundColourId, pink);
    setColour (juce::ToggleButton::tickColourId, juce::Colour (0xff15162a));
    setColour (juce::ListBox::backgroundColourId, background);
    setColour (juce::AlertWindow::backgroundColourId, panel);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, juce::Colours::transparentBlack);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& backgroundColour,
                                        bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f, 1.0f);
    auto c = backgroundColour.withMultipliedSaturation (b.isEnabled() ? 1.0f : 0.4f)
                             .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f);

    if (down)
        c = c.darker (0.25f);
    else if (highlighted)
        c = c.brighter (0.18f);

    const float radius = juce::jmin (cornerRadius, r.getHeight() * 0.5f);

    // 少しだけ立体的に（上が明るいグラデーション）
    g.setGradientFill (juce::ColourGradient (c.brighter (0.12f), 0.0f, r.getY(), c.darker (0.08f), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);

    if (! down)
    {
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + radius, r.getRight() - radius);
    }
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f, 1.0f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, juce::jmin (cornerRadius, r.getHeight() * 0.5f));

    if (box.hasKeyboardFocus (true))
    {
        g.setColour (accent);
        g.drawRoundedRectangle (r, juce::jmin (cornerRadius, r.getHeight() * 0.5f), 1.2f);
    }

    juce::Path arrow;
    const float cx = (float) width - 14.0f, cy = (float) height * 0.5f;
    arrow.startNewSubPath (cx - 4.0f, cy - 2.0f);
    arrow.lineTo (cx, cy + 2.5f);
    arrow.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (accent);
    g.strokePath (arrow, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void LookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height), cornerRadius - 2.0f);
}

void LookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    if (! editor.isEnabled())
        return;

    const bool focused = editor.hasKeyboardFocus (true) && ! editor.isReadOnly();
    g.setColour (editor.findColour (focused ? juce::TextEditor::focusedOutlineColourId : juce::TextEditor::outlineColourId));
    g.drawRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f),
                            cornerRadius - 2.0f, focused ? 1.6f : 1.0f);
}

void LookAndFeel::drawProgressBar (juce::Graphics& g, juce::ProgressBar& bar, int width, int height, double progress, const juce::String& label)
{
    auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    const float radius = r.getHeight() * 0.5f;
    g.setColour (bar.findColour (juce::ProgressBar::backgroundColourId));
    g.fillRoundedRectangle (r, radius);

    if (progress >= 0.0 && progress <= 1.0)
    {
        auto fill = r.withWidth (juce::jmax (r.getHeight(), r.getWidth() * (float) progress));
        g.setGradientFill (juce::ColourGradient (pink, fill.getX(), 0.0f, accent, r.getRight(), 0.0f, false));
        g.fillRoundedRectangle (fill, radius);
    }
    else
    {
        // 不定: ストライプが流れる
        const float phase = (float) (juce::Time::getMillisecondCounter() % 1000) / 1000.0f;
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addRoundedRectangle (r, radius);
        g.reduceClipRegion (clip);

        for (float x = -40.0f + phase * 40.0f; x < r.getWidth(); x += 40.0f)
        {
            juce::Path stripe;
            stripe.startNewSubPath (x, r.getBottom());
            stripe.lineTo (x + 20.0f, r.getBottom());
            stripe.lineTo (x + 20.0f + r.getHeight(), r.getY());
            stripe.lineTo (x + r.getHeight(), r.getY());
            stripe.closeSubPath();
            g.setColour (pink.withAlpha (0.8f));
            g.fillPath (stripe);
        }
    }

    if (label.isNotEmpty())
    {
        g.setColour (text);
        g.setFont (juce::FontOptions ((float) height * 0.6f, juce::Font::bold));
        g.drawText (label, r, juce::Justification::centred);
    }
}

void LookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& alert, const juce::Rectangle<int>& textArea, juce::TextLayout& layout)
{
    auto bounds = alert.getLocalBounds().toFloat();
    g.setColour (panel);
    g.fillRoundedRectangle (bounds, 12.0f);

    // 種類ごとの色の帯と丸いアイコン（「!」「?」「i」）
    juce::Colour colour = accent;
    juce::String glyph = "i";

    switch (alert.getAlertType())
    {
        case juce::MessageBoxIconType::WarningIcon:  colour = orange; glyph = "!"; break;
        case juce::MessageBoxIconType::QuestionIcon: colour = purple; glyph = "?"; break;
        case juce::MessageBoxIconType::NoIcon:       colour = pink;   glyph = {};  break;
        case juce::MessageBoxIconType::InfoIcon:
        default:                                     break;
    }

    juce::Path stripe;
    stripe.addRoundedRectangle (bounds.getX(), bounds.getY(), bounds.getWidth(), 6.0f, 12.0f, 12.0f, true, true, false, false);
    g.setColour (colour);
    g.fillPath (stripe);

    if (glyph.isNotEmpty())
    {
        auto icon = juce::Rectangle<float> (18.0f, 26.0f, 44.0f, 44.0f);
        g.setColour (colour);
        g.fillEllipse (icon);
        g.setColour (juce::Colour (0xff15162a));
        g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
        g.drawText (glyph, icon, juce::Justification::centred);
    }

    g.setColour (alert.findColour (juce::AlertWindow::textColourId));
    layout.draw (g, textArea.toFloat());
}

void LookAndFeel::drawTickBox (juce::Graphics& g, juce::Component&, float x, float y, float w, float h, bool ticked, bool enabled,
                               bool highlighted, bool)
{
    auto r = juce::Rectangle<float> (x, y, w, h).reduced (1.0f);
    g.setColour (ticked ? accent.withMultipliedAlpha (enabled ? 1.0f : 0.5f) : (highlighted ? panelLight.brighter (0.2f) : panelLight));
    g.fillRoundedRectangle (r, 4.0f);

    if (ticked)
    {
        juce::Path tick;
        tick.startNewSubPath (r.getX() + r.getWidth() * 0.22f, r.getCentreY());
        tick.lineTo (r.getX() + r.getWidth() * 0.43f, r.getBottom() - r.getHeight() * 0.25f);
        tick.lineTo (r.getRight() - r.getWidth() * 0.2f, r.getY() + r.getHeight() * 0.25f);
        g.setColour (juce::Colour (0xff15162a));
        g.strokePath (tick, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

}

namespace Theme
{

juce::Path selectToolIcon (juce::Rectangle<float> area)
{
    // 左上を先端にした矢印
    juce::Path p;
    p.startNewSubPath (0.0f, 0.0f);
    p.lineTo (0.0f, 14.0f);
    p.lineTo (3.6f, 10.6f);
    p.lineTo (6.2f, 16.0f);
    p.lineTo (8.6f, 15.0f);
    p.lineTo (6.0f, 9.6f);
    p.lineTo (11.0f, 9.6f);
    p.closeSubPath();
    p.applyTransform (p.getTransformToScaleToFit (area, true));
    return p;
}

juce::Path pencilToolIcon (juce::Rectangle<float> area)
{
    // 左下を先端にした鉛筆（45 度）
    juce::Path p;
    p.startNewSubPath (0.0f, 16.0f);      // 先端
    p.lineTo (1.2f, 11.4f);
    p.lineTo (11.6f, 1.0f);
    p.lineTo (15.0f, 4.4f);
    p.lineTo (4.6f, 14.8f);
    p.closeSubPath();
    p.startNewSubPath (10.0f, 2.6f);      // 消しゴムとの境目
    p.lineTo (13.4f, 6.0f);
    p.applyTransform (p.getTransformToScaleToFit (area, true));
    return p;
}

juce::Path splitToolIcon (juce::Rectangle<float> area)
{
    // はさみ（2 つの輪と交差する刃）
    juce::Path p;
    p.addEllipse (0.5f, 10.5f, 5.0f, 5.0f);
    p.addEllipse (10.5f, 10.5f, 5.0f, 5.0f);
    p.startNewSubPath (4.5f, 11.0f);
    p.lineTo (12.0f, 0.5f);
    p.startNewSubPath (11.5f, 11.0f);
    p.lineTo (4.0f, 0.5f);
    p.applyTransform (p.getTransformToScaleToFit (area, true));
    return p;
}

static juce::MouseCursor makeToolCursor (juce::Path (*icon) (juce::Rectangle<float>), int hotX, int hotY)
{
    constexpr int size = 24;
    juce::Image image (juce::Image::ARGB, size, size, true);
    juce::Graphics g (image);
    auto path = icon ({ 2.0f, 2.0f, (float) size - 4.0f, (float) size - 4.0f });
    g.setColour (juce::Colours::black);
    g.strokePath (path, juce::PathStrokeType (3.0f));
    g.setColour (juce::Colours::white);
    g.strokePath (path, juce::PathStrokeType (1.4f));
    return juce::MouseCursor (image, hotX, hotY);
}

const juce::MouseCursor& splitCursor()
{
    static const juce::MouseCursor cursor = makeToolCursor (splitToolIcon, 12, 2);
    return cursor;
}

const juce::MouseCursor& toolCursor (EditTool tool)
{
    static const juce::MouseCursor normal;

    switch (tool)
    {
        case EditTool::pencil: return pencilCursor();
        case EditTool::split:  return splitCursor();
        case EditTool::select: break;
    }

    return normal;
}

const juce::MouseCursor& pencilCursor()
{
    static const juce::MouseCursor cursor = []
    {
        constexpr int size = 24;
        juce::Image image (juce::Image::ARGB, size, size, true);
        juce::Graphics g (image);
        auto icon = pencilToolIcon ({ 1.0f, 1.0f, (float) size - 4.0f, (float) size - 4.0f });
        g.setColour (juce::Colours::black);
        g.strokePath (icon, juce::PathStrokeType (2.5f));
        g.setColour (juce::Colours::white);
        g.fillPath (icon);
        g.setColour (juce::Colours::black);
        g.strokePath (icon, juce::PathStrokeType (1.0f));
        return juce::MouseCursor (image, 1, size - 3);
    }();

    return cursor;
}

}
