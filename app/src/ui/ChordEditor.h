#pragma once

#include "Common.h"
#include "collab/chord/Chord.h"
#include "collab/chord/Degree.h"

/**
    コードエディタ（§3.8）。ルート・コードタイプ・テンション・ベース音をボタンで選ぶか、
    テキスト（例: G7(9,13)）を入力する。どちらを操作しても、もう一方に反映される。
    キーが決まっていれば、ディグリー（例: 6m7、4、b7、1/3）でも入力でき、キーに合わせたコードになる。
*/
class ChordEditor  : public juce::Component
{
public:
    struct Result
    {
        bool noChord = false;
        std::optional<collab::chord::Chord> chord;
        juce::String text;
    };

    ChordEditor (const juce::String& initialText, std::optional<collab::chord::Key> key, std::function<void (Result)> onOk,
                 std::function<void()> onDelete = {});

    /** ダイアログとして開く（非同期）。key はその位置のキー（未設定なら std::nullopt）。 */
    static void show (const juce::String& initialText, std::optional<collab::chord::Key> key,
                      std::function<void (Result)> onOk, std::function<void()> onDelete = {});

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::TextEditor textEditor;
    juce::Label preview;
    juce::OwnedArray<juce::TextButton> rootButtons, qualityButtons, tensionButtons, degreeButtons;
    std::optional<collab::chord::Key> key;
    juce::ComboBox bassBox;
    juce::TextButton noChordButton, okButton, cancelButton, deleteButton;

    std::function<void (Result)> onOk;
    std::function<void()> onDelete;

    collab::chord::Chord current { "C", "maj", {}, std::nullopt };
    bool currentNoChord = false;
    bool valid = true;
    bool updating = false;

    void textChanged();
    juce::String previewText() const;
    void setFromStructure();
    void refreshButtons();
    void close();
};
