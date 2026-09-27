#pragma once

#include "Common.h"
#include "collab/chord/Chord.h"
#include "collab/chord/Degree.h"

/**
    コードエディタ（§3.8）。Cubase のコードエディターと同じく、
    「エディター」タブ: ルート・タイプ・テンション・ベース音の 4 列から選ぶ
    「五度圏」タブ: 五度圏の円からクリックで選ぶ（キーの主要なコードを明るく表示）
    上の欄にテキスト（例: G7(9,13)）でも入力でき、キーが決まっていればディグリー（例: 6、4M7、57、b7）でも入力できる。
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
    ~ChordEditor() override;

    /** ダイアログとして開く（非同期）。key はその位置のキー（未設定なら std::nullopt）。 */
    static void show (const juce::String& initialText, std::optional<collab::chord::Key> key,
                      std::function<void (Result)> onOk, std::function<void()> onDelete = {});

    void paint (juce::Graphics&) override;
    void resized() override;

    /** 列の選択（コードとの相互変換。テスト用に公開）。 */
    enum class Type { maj, min, dim, sus4, sus2, aug };
    enum class Seventh { none, dominant, major };

    struct Columns
    {
        int root = 0;
        Type type = Type::maj;
        Seventh seventh = Seventh::none;
        bool b9 = false, n9 = false, s9 = false, n11 = false, b5s11 = false, s5b13 = false, six13 = false;
        int bass = -1;   // -1 = なし
    };

    static Columns columnsFor (const collab::chord::Chord&);
    static collab::chord::Chord chordFor (const Columns&, const std::optional<collab::chord::Key>&);

private:
    class CircleView;

    juce::TextEditor textEditor;
    juce::Label preview;
    juce::TextButton editorTab, circleTab;
    juce::OwnedArray<juce::TextButton> degreeButtons, rootButtons, typeButtons, tensionButtons, bassButtons;
    std::unique_ptr<CircleView> circle;
    juce::TextButton noChordButton, okButton, cancelButton, deleteButton;
    std::optional<collab::chord::Key> key;

    std::function<void (Result)> onOk;
    std::function<void()> onDelete;

    collab::chord::Chord current { "C", "maj", {}, std::nullopt };
    bool currentNoChord = false;
    bool valid = true;
    bool updating = false;
    bool showCircle = false;

    void textChanged();
    juce::String previewText() const;
    void setChord (const collab::chord::Chord&);
    void editColumns (std::function<void (Columns&)>);
    void setFromStructure();
    void refreshButtons();
    void setPage (bool circlePage);
    void close();
};
