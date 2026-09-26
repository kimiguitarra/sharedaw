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
