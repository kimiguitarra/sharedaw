#pragma once

#include "ui/AppContext.h"
#include "ui/Theme.h"

/** クリックで入力、ホイールで増減できる値（テンポ・拍子・ループ範囲）。 */
struct ValueLabel  : public juce::Label
{
    std::function<void (int direction)> onWheel;

    /** 位置（小節. 拍. tick）: マウスの下の部分（0 = 小節、1 = 拍、2 = tick）ごとに増減する。 */
    std::function<void (int direction, int part)> onWheelPart;

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const float d = std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX;

        if (isBeingEdited() || d == 0.0f)
            return juce::Label::mouseWheelMove (e, w);

        if (onWheelPart != nullptr)
            onWheelPart (d > 0 ? 1 : -1, partAt (e.x));
        else if (onWheel != nullptr)
            onWheel (d > 0 ? 1 : -1);
        else
            juce::Label::mouseWheelMove (e, w);
    }

    /** x の位置の文字が、ドットで区切った何番目の部分か。 */
    int partAt (int x) const
    {
        const auto text = getText();
        const auto font = getFont();
        const auto area = getBorderSize().subtractedFrom (getLocalBounds());
        const float width = juce::GlyphArrangement::getStringWidth (font, text);
        const auto just = getJustificationType();
        float left = (float) area.getX();

        if (just.testFlags (juce::Justification::horizontallyCentred))
            left += ((float) area.getWidth() - width) * 0.5f;
        else if (just.testFlags (juce::Justification::right))
            left += (float) area.getWidth() - width;

        int part = 0;

        for (int i = 0; i < text.length(); ++i)
        {
            if (text[i] == '.')
            {
                // ドットの右端までは前の部分
                if ((float) x < left + juce::GlyphArrangement::getStringWidth (font, text.substring (0, i + 1)))
                    return part;

                ++part;
            }
        }

        return part;
    }
};

/** 旗のアイコン（左右のロケーター）。 */
struct FlagIcon  : public juce::Component,
                   public juce::SettableTooltipClient
{
    explicit FlagIcon (bool isLeft) : left (isLeft) {}

    void paint (juce::Graphics& g) override
    {
        g.setColour (Theme::accent);
        g.fillPath (Theme::iconPath (left ? "flagL" : "flagR", getLocalBounds().toFloat().reduced (2.0f)));
    }

    bool left;
};

/**
    選んでいるオーディオクリップの値（音量・ピッチ、あればフェード）。クリックで入力、ホイールで増減（Shift で細かく）。
    クリップを選んでいないときは隠れる。上のツールバーと下の波形の画面の両方で使う。
*/
class AudioClipFields  : public juce::Component,
                         private juce::ChangeListener
{
public:
    AudioClipFields (AppContext&, bool withFades);
    ~AudioClipFields() override;

    /** 選んでいるオーディオクリップがあるか（ないときは何も出さない）。 */
    bool hasClip() const noexcept                   { return shown; }
    int preferredWidth() const;
    void resized() override;

    /** 出る・消えるが変わったとき（並べ直す）。 */
    std::function<void()> onShownChanged;

private:
    AppContext& ctx;
    const bool withFades;
    bool shown = false;
    juce::Label title;
    ValueLabel gainLabel, pitchLabel, fadeInLabel, fadeOutLabel, bpmLabel;
    Theme::IconButton tempoButton { "note" };   // 曲のテンポに合わせる（Cubase の ♩）
    juce::String wheelMergeId;
    juce::uint32 lastWheelTime = 0;

    std::optional<std::pair<std::string, collab::AudioClip>> selected() const;
    void refresh();
    void edit (const juce::String& description, std::function<void (collab::AudioClip&)> fn, const juce::String& mergeId = {});
    void toggleTempoSync();
    juce::String nextWheelMergeId();
    void changeListenerCallback (juce::ChangeBroadcaster*) override   { refresh(); }
};

/**
    画面上部のツールバー（Cubase のプロジェクトウィンドウのツールバー）:
    ツール、クオンタイズ値・スナップ・自動スクロール、メトロノーム、曲のテンポ・拍子・キー。
*/
class ToolBar  : public juce::Component,
                 private juce::ChangeListener
{
public:
    explicit ToolBar (AppContext&);
    ~ToolBar() override;

    /** 定期的に呼ぶ（再生位置のテンポ・拍子・キー）。 */
    void update();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    /** ツール（選択・鉛筆・はさみ）のボタン。アイコンだけを描く。 */
    struct ToolButton  : public juce::Button
    {
        explicit ToolButton (EditTool t) : juce::Button ({}), tool (t) {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        EditTool tool;
    };

    AppContext& ctx;
    ToolButton selectTool { EditTool::select }, pencilTool { EditTool::pencil }, splitTool { EditTool::split };
    juce::ComboBox quantiseBox;
    Theme::IconButton snapButton { "snap" }, autoScrollButton { "follow" }, metronomeButton { "metronome" };
    std::vector<juce::Rectangle<int>> groups;   // ガラスのまとまり（ツール、クオンタイズ、メトロノーム…）
    juce::Slider metronomeVolume { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    ValueLabel bpmLabel, meterLabel;
    juce::Label keyLabel;

    // 選んでいるオーディオクリップの音量・ピッチ・フェード（Cubase の情報ライン）
    AudioClipFields clipFields { ctx, true };
    juce::String wheelMergeId;
    juce::uint32 lastWheelTime = 0;
    collab::Tick lastTick = -1;

    collab::Tick playheadTick() const;
    void setTempoAtPlayhead (double bpm, const juce::String& mergeId = {});
    void setMeterAtPlayhead (int numerator, int denominator);
    void refreshTempo();


    void changeListenerCallback (juce::ChangeBroadcaster*) override;
};

/**
    画面下部のトランスポート（Cubase と同じ並び）:
    左に左右のロケーター（旗）、中央にサイクル・停止・再生・録音、その右に現在の位置（小節. 拍. tick）。
*/
class TransportBar  : public juce::Component,
                      private juce::ChangeListener
{
public:
    explicit TransportBar (AppContext&);
    ~TransportBar() override;

    /** 定期的に呼んで位置表示を更新する。 */
    void updatePosition (double tick, double seconds, bool playing);

    std::function<void()> onMixer;   // 右下のミキサーのボタン
    std::function<void()> onPianoFull;   // その左: ピアノロールを全画面に

    void setPianoFullScreen (bool on)    { pianoButton.setToggleState (on, juce::dontSendNotification); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AppContext& ctx;
    FlagIcon loopStartFlag { true }, loopEndFlag { false };
    ValueLabel loopStartLabel, loopEndLabel;
    Theme::IconButton loopButton { "loop" }, stopButton { "stop" }, playButton { "play" }, recordButton { "record" };
    Theme::IconButton mixerButton { "mixer" }, pianoButton { "piano" };
    std::vector<juce::Rectangle<int>> groups;
    ValueLabel barBeatLabel;
    bool wasPlaying = false;

    juce::String formatPosition (collab::Tick) const;
    std::optional<collab::Tick> parsePosition (const juce::String&) const;
    void setLoopEdge (bool start, collab::Tick);

    /** 位置を part（0 = 小節、1 = 拍、2 = クオンタイズ値）の単位で direction だけ動かす。 */
    collab::Tick stepPosition (collab::Tick from, int direction, int part) const;
    void refreshLoop();

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
};
