#include "MasterImager.h"

#include "Theme.h"

MasterImager::MasterImager (AppContext& c) : ctx (c)
{
    left.resize ((size_t) windowSamples);
    right.resize ((size_t) windowSamples);
}

void MasterImager::update()
{
    if (! ctx.engine.getMasterStereo (left.data(), right.data(), windowSamples))
        return;

    double lr = 0.0, ll = 0.0, rr = 0.0, mid = 0.0, side = 0.0;
    dots.clear();

    for (int i = 0; i < windowSamples; ++i)
    {
        const double l = left[(size_t) i], r = right[(size_t) i];
        lr += l * r;
        ll += l * l;
        rr += r * r;
        const double m = (l + r) * 0.7071, s = (l - r) * 0.7071;
        mid += m * m;
        side += s * s;

        // 半円の点: 真上がモノ。大きさは dB で（-48 dB が中心、0 dB が外周）
        if (i % 2 == 0)
        {
            const double amp = std::sqrt (m * m + s * s);

            if (amp > 1.0e-4)
            {
                const double radius = juce::jlimit (0.0, 1.0, (juce::Decibels::gainToDecibels (amp) + 48.0) / 48.0);
                // 上半分に折り返す（-m は同じ向きの逆さま）。右がR寄り、左がL寄り
                const double x = (m >= 0.0 ? -s : s) / amp, y = std::abs (m) / amp;
                dots.push_back ({ (float) (x * radius), (float) (y * radius) });
            }
        }
    }

    hasSignal = ll + rr > 1.0e-7;
    const float corrNow = hasSignal ? (float) (lr / std::sqrt (juce::jmax (1.0e-12, ll * rr))) : 1.0f;
    const float widthNow = hasSignal ? (float) (side / juce::jmax (1.0e-12, mid + side)) : 0.0f;
    const float balanceNow = hasSignal ? (float) ((rr - ll) / (rr + ll)) : 0.0f;

    // 針はなめらかに動かす
    correlation += (corrNow - correlation) * 0.15f;
    width += (widthNow - width) * 0.15f;
    balance += (balanceNow - balance) * 0.15f;
    repaint();
}

void MasterImager::paint (juce::Graphics& g)
{
    const auto bg = juce::Colour (0xff141414), line = juce::Colour (0xff3a3d42), dim = juce::Colour (0xff8d939b);
    const auto accent = juce::Colour (0xff6ec6ff);
    auto area = getLocalBounds().toFloat();
    g.setColour (bg);
    g.fillRoundedRectangle (area, 6.0f);

    auto r = area.reduced (16.0f, 10.0f);
    g.setColour (dim);
    g.setFont (juce::FontOptions (16.5f, juce::Font::bold));
    g.drawText ("IMAGER", r.removeFromTop (22.0f), juce::Justification::centred);
    r.removeFromTop (6.0f);

    // 下: 相関メーターと広がり
    auto meters = r.removeFromBottom (92.0f);

    // 半円（Polar Sample）
    const float radius = juce::jmin (r.getWidth() * 0.5f, r.getHeight() - 16.0f);
    const juce::Point<float> centre (r.getCentreX(), r.getY() + radius + 4.0f);

    g.setColour (line);

    for (float k : { 0.25f, 0.5f, 0.75f, 1.0f })
    {
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, radius * k, radius * k, 0.0f, -juce::MathConstants<float>::halfPi,
                           juce::MathConstants<float>::halfPi, true);
        g.strokePath (arc, juce::PathStrokeType (1.0f));
    }

    // 目安の線: 真上（モノ）、斜め 45°（片側だけ = L・R）、横（逆相）
    for (float deg : { -90.0f, -45.0f, 0.0f, 45.0f, 90.0f })
    {
        const float a = juce::degreesToRadians (deg);
        g.drawLine ({ centre, centre + juce::Point<float> (std::sin (a), -std::cos (a)) * radius }, 1.0f);
    }

    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.setColour (dim);
    const auto labelAt = [&] (float deg, const juce::String& text)
    {
        const float a = juce::degreesToRadians (deg);
        const auto p = centre + juce::Point<float> (std::sin (a), -std::cos (a)) * (radius + 9.0f);
        g.drawText (text, juce::Rectangle<float> (28.0f, 14.0f).withCentre (p), juce::Justification::centred);
    };
    labelAt (-45.0f, "L");
    labelAt (45.0f, "R");
    labelAt (0.0f, "M");

    g.setColour (accent.withAlpha (0.55f));

    for (auto& d : dots)
        g.fillRect (centre.x + d.x * radius - 1.0f, centre.y - d.y * radius - 1.0f, 2.0f, 2.0f);

    // 相関メーター（-1〜+1）
    auto bar = meters.removeFromTop (18.0f);
    auto label = meters.removeFromTop (16.0f);
    g.setColour (juce::Colour (0xff0b0b0b));
    g.fillRoundedRectangle (bar, 3.0f);
    const float zeroX = bar.getCentreX();
    const float x = juce::jmap (juce::jlimit (-1.0f, 1.0f, correlation), -1.0f, 1.0f, bar.getX(), bar.getRight());
    g.setColour (correlation < 0.0f ? juce::Colour (0xffff6b5e) : juce::Colour (0xff7bd88f));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (zeroX, x), bar.getY() + 3.0f, juce::jmax (zeroX, x), bar.getBottom() - 3.0f));
    g.setColour (line);
    g.drawVerticalLine (juce::roundToInt (zeroX), bar.getY(), bar.getBottom());

    g.setColour (dim);
    g.setFont (juce::FontOptions (12.5f));
    g.drawText ("-1", label, juce::Justification::centredLeft);
    g.drawText ("0", label, juce::Justification::centred);
    g.drawText ("+1", label, juce::Justification::centredRight);

    meters.removeFromTop (8.0f);
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    auto numbers = meters.removeFromTop (20.0f);
    const auto third = numbers.getWidth() / 3.0f;
    g.drawText ("CORR " + juce::String (correlation, 2), numbers.removeFromLeft (third), juce::Justification::centredLeft);
    g.drawText ("WIDTH " + juce::String (juce::roundToInt (width * 100.0f)) + "%", numbers.removeFromLeft (third), juce::Justification::centred);
    const int bal = juce::roundToInt (balance * 100.0f);
    g.drawText ("BAL " + (bal == 0 ? juce::String ("C") : (bal < 0 ? "L" : "R") + juce::String (std::abs (bal))), numbers,
                juce::Justification::centredRight);
}
