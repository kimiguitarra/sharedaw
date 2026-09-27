#include "Theme.h"

namespace Theme
{

namespace
{
    const juce::uint32 palette[] = { 0xffE57373, 0xff64B5F6, 0xff81C784, 0xffFFB74D, 0xffBA68C8,
                                     0xff4DB6AC, 0xffF06292, 0xffAED581, 0xff9575CD, 0xffFFD54F };
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

LookAndFeel::LookAndFeel()
{
    setColourScheme ({ panel, background, panelLight, gridBar, text, accent, juce::Colours::black, panelLight, text });

    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::TextButton::buttonColourId, panelLight);
    setColour (juce::TextButton::buttonOnColourId, accent.darker (0.3f));
    setColour (juce::ComboBox::backgroundColourId, panelLight);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::Label::textColourId, text);
    setColour (juce::Slider::thumbColourId, accent);
    setColour (juce::Slider::trackColourId, accent.withAlpha (0.5f));
    setColour (juce::Slider::backgroundColourId, background);
    setColour (juce::ScrollBar::thumbColourId, gridBar);
    setColour (juce::TooltipWindow::backgroundColourId, panelLight);
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

juce::Path glueToolIcon (juce::Rectangle<float> area)
{
    // のりのチューブ
    juce::Path p;
    p.addRoundedRectangle (1.0f, 6.0f, 10.0f, 9.0f, 2.0f);
    p.startNewSubPath (11.0f, 8.0f);
    p.lineTo (14.0f, 9.0f);
    p.lineTo (14.0f, 12.0f);
    p.lineTo (11.0f, 13.0f);
    p.startNewSubPath (3.0f, 6.0f);
    p.lineTo (3.0f, 2.0f);
    p.lineTo (9.0f, 2.0f);
    p.lineTo (9.0f, 6.0f);
    p.applyTransform (p.getTransformToScaleToFit (area, true));
    return p;
}

juce::Path eraseToolIcon (juce::Rectangle<float> area)
{
    // 消しゴム（斜めの直方体）
    juce::Path p;
    p.startNewSubPath (1.0f, 11.0f);
    p.lineTo (9.0f, 3.0f);
    p.lineTo (15.0f, 9.0f);
    p.lineTo (9.0f, 15.0f);
    p.lineTo (5.0f, 15.0f);
    p.closeSubPath();
    p.startNewSubPath (5.0f, 7.0f);
    p.lineTo (11.0f, 13.0f);
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

const juce::MouseCursor& glueCursor()
{
    static const juce::MouseCursor cursor = makeToolCursor (glueToolIcon, 20, 12);
    return cursor;
}

const juce::MouseCursor& eraseCursor()
{
    static const juce::MouseCursor cursor = makeToolCursor (eraseToolIcon, 4, 16);
    return cursor;
}

const juce::MouseCursor& toolCursor (EditTool tool)
{
    static const juce::MouseCursor normal;

    switch (tool)
    {
        case EditTool::pencil: return pencilCursor();
        case EditTool::split:  return splitCursor();
        case EditTool::glue:   return glueCursor();
        case EditTool::erase:  return eraseCursor();
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
