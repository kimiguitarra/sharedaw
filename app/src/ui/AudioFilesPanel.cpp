#include "AudioFilesPanel.h"
#include "collab/Sampler.h"

#include <set>

#include "Theme.h"
#include "ProjectDocument.h"
#include "audio/AudioFiles.h"

namespace
{
    juce::String megabytes (juce::int64 bytes)
    {
        return juce::String ((double) bytes / (1024.0 * 1024.0), 1) + " MB";
    }

    /** 曲で使っている実体（ハッシュ → 使っているトラックの名前）。 */
    std::map<std::string, juce::StringArray> usage (const ProjectDocument& doc)
    {
        std::map<std::string, juce::StringArray> result;

        for (auto& t : doc.getProject().tracks)
        {
            if (t.render)
                result[t.render->audioHash].addIfNotAlreadyThere (toJuce (t.name) + "（バウンス）"_ju);

            for (auto& c : t.audioClips)
                result[c.audioHash].addIfNotAlreadyThere (toJuce (t.name));

            if (t.instrument && collab::isSampler (*t.instrument))
                for (auto& pad : collab::samplerPads (t.instrument->params))
                    if (! pad.audioHash.empty())
                        result[pad.audioHash].addIfNotAlreadyThere (toJuce (t.name) + "（サンプラー）"_ju);
        }

        return result;
    }

    /** ファイルを使っているトラック（ファイル名のハッシュの頭で探す）。 */
    juce::String usedBy (const juce::File& f, const std::map<std::string, juce::StringArray>& used)
    {
        const auto key = AudioFiles::hashKeyOf (f);

        if (key.empty())
            return {};

        for (auto& [hash, tracks] : used)
            if (hash.compare (0, key.size(), key) == 0)
                return tracks.joinIntoString ("、"_ju);

        return {};
    }
}

juce::Array<juce::File> AudioFilesPanel::unusedFiles (const ProjectDocument& doc)
{
    juce::Array<juce::File> result;

    if (! doc.hasLocation())
        return result;

    const auto used = usage (doc);

    for (auto& f : doc.getProjectDir().getChildFile ("audio").findChildFiles (juce::File::findFiles, false, "*.wav"))
        if (usedBy (f, used).isEmpty())
            result.add (f);

    return result;
}

AudioFilesPanel::AudioFilesPanel (ProjectDocument& d) : document (d)
{
    list.setModel (this);
    list.setRowHeight (30);
    list.setColour (juce::ListBox::backgroundColourId, Theme::field);
    addAndMakeVisible (list);

    summary.setFont (juce::FontOptions (15.0f));
    summary.setColour (juce::Label::textColourId, Theme::textDim);
    addAndMakeVisible (summary);

    deleteButton.onClick = [this] { deleteUnused(); };
    revealButton.setButtonText ("フォルダを表示"_ju);
    revealButton.onClick = [this] { document.getProjectDir().getChildFile ("audio").revealToUser(); };
    closeButton.setButtonText ("閉じる"_ju);
    closeButton.onClick = [this]
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState (0);
    };

    for (auto* b : { &deleteButton, &revealButton, &closeButton })
        addAndMakeVisible (b);

    refresh();
    setSize (760, 520);
}

void AudioFilesPanel::refresh()
{
    rows.clear();

    if (document.hasLocation())
    {
        const auto used = usage (document);
        auto files = document.getProjectDir().getChildFile ("audio").findChildFiles (juce::File::findFiles, false, "*.wav");

        for (auto& f : files)
            rows.push_back ({ f, f.getSize(), usedBy (f, used) });

        // 使っていないものを上に、あとは名前の順
        std::sort (rows.begin(), rows.end(), [] (const Row& a, const Row& b)
        {
            if (a.usedBy.isEmpty() != b.usedBy.isEmpty())
                return a.usedBy.isEmpty();

            return a.file.getFileName().compareNatural (b.file.getFileName()) < 0;
        });
    }

    juce::int64 total = 0, unusedBytes = 0;
    int unused = 0;

    for (auto& r : rows)
    {
        total += r.size;

        if (r.usedBy.isEmpty())
        {
            ++unused;
            unusedBytes += r.size;
        }
    }

    summary.setText (juce::String ((int) rows.size()) + " 個・"_ju + megabytes (total)
                       + (unused > 0 ? "（使っていないもの "_ju + juce::String (unused) + " 個・"_ju + megabytes (unusedBytes) + "）"_ju
                                     : "（すべて使っています）"_ju),
                     juce::dontSendNotification);

    deleteButton.setButtonText ("使っていないファイルを削除（"_ju + juce::String (unused) + " 個）"_ju);
    deleteButton.setEnabled (unused > 0);
    list.updateContent();
    list.repaint();
}

void AudioFilesPanel::deleteUnused()
{
    const auto files = unusedFiles (document);

    if (files.isEmpty())
        return;

    juce::int64 bytes = 0;

    for (auto& f : files)
        bytes += f.getSize();

    auto options = juce::MessageBoxOptions()
                     .withIconType (juce::MessageBoxIconType::QuestionIcon)
                     .withTitle ("使っていないファイルを削除"_ju)
                     .withMessage (juce::String (files.size()) + " 個（"_ju + megabytes (bytes) + "）をゴミ箱に移します。\n\n"_ju
                                   + "どのトラックでも使っていない録音・読み込んだオーディオです。"_ju
                                   + "消したクリップを「元に戻す」で戻したときは、ゴミ箱から戻すと音が出ます。"_ju)
                     .withButton ("削除する"_ju)
                     .withButton ("やめる"_ju)
                     .withAssociatedComponent (this);

    juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer<AudioFilesPanel> (this), files] (int result)
    {
        if (result != 1)
            return;

        for (auto& f : files)
            if (! f.moveToTrash())
                f.deleteFile();

        if (safe != nullptr)
            safe->refresh();
    });
}

void AudioFilesPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= (int) rows.size())
        return;

    const auto& r = rows[(size_t) row];
    auto area = juce::Rectangle<int> (0, 0, width, height).reduced (10, 0);

    if (selected)
        g.fillAll (Theme::selection.withAlpha (0.25f));

    const bool unused = r.usedBy.isEmpty();
    g.setFont (juce::FontOptions (14.0f));
    g.setColour (unused ? Theme::warning : Theme::textDim);
    g.drawText (unused ? "使っていない"_ju : r.usedBy, area.removeFromRight (220), juce::Justification::centredRight, true);
    g.setColour (Theme::textDim);
    g.drawText (megabytes (r.size), area.removeFromRight (90), juce::Justification::centredRight);
    g.setColour (unused ? Theme::textDim : Theme::text);
    g.setFont (juce::FontOptions (15.0f));
    g.drawText (r.file.getFileName(), area, juce::Justification::centredLeft, true);
}

void AudioFilesPanel::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
}

void AudioFilesPanel::resized()
{
    auto area = getLocalBounds().reduced (14, 12);
    summary.setBounds (area.removeFromTop (24));
    area.removeFromTop (6);

    auto buttons = area.removeFromBottom (34);
    closeButton.setBounds (buttons.removeFromRight (100));
    buttons.removeFromRight (8);
    revealButton.setBounds (buttons.removeFromRight (140));
    deleteButton.setBounds (buttons.removeFromLeft (300));
    area.removeFromBottom (8);
    list.setBounds (area);
}
