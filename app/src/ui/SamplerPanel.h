#pragma once

#include "ui/AppContext.h"
#include "ui/Theme.h"
#include "collab/Sampler.h"

/**
    内蔵サンプラーの画面: 白鍵 16 個の鍵盤と、その上に鍵ごとのパッド。下は選んだパッドの編集
    （名前・使う範囲・音量・パン・ピッチ・テンポ合わせ・EQ・サチュレーション・鳴らし方）。
    パッドにオーディオファイルをドロップすると、曲のオーディオとして保存して割り当てる。
    曲と一緒に同期されるので、ほかの人も MIDI を打ち込めば同じ音が鳴る。パッド・鍵を押すと試し弾き。
*/
class SamplerPanel  : public juce::Component,
                      public juce::FileDragAndDropTarget,
                      private juce::ChangeListener
{
public:
    SamplerPanel (AppContext&, std::string trackId);
    ~SamplerPanel() override;

    void setTrack (std::string trackId);
    juce::String getTitle() const;
    std::function<void()> onTitleChanged;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int x, int y) override     { dropPad = dropTarget ({ x, y }); repaint(); }
    void fileDragMove (const juce::StringArray&, int x, int y) override      { dropPad = dropTarget ({ x, y }); repaint(); }
    void fileDragExit (const juce::StringArray&) override                    { dropPad = -1; repaint(); }
    void filesDropped (const juce::StringArray&, int x, int y) override;

private:
    struct Knob;

    AppContext& ctx;
    std::string trackId;
    int selected = 0;   // 編集するパッド

    juce::Slider volume;
    juce::Label volumeLabel { {}, "音量"_ju };
    juce::String mergeId;
    std::unique_ptr<juce::FileChooser> chooser;

    // 選んだパッドの編集
    juce::TextEditor nameEditor;
    juce::TextButton loadButton { "読み込む"_ju }, clearButton { "空にする"_ju };
    std::vector<std::unique_ptr<Knob>> knobs;
    Theme::IconButton tempoButton { "note" };
    juce::Label bpmLabel, tempoLabel { {}, "テンポ"_ju };
    juce::ToggleButton oneShotButton { "最後まで鳴らす"_ju };
    juce::ComboBox chokeBox;
    juce::Label chokeLabel { {}, "チョーク"_ju };

    juce::Rectangle<int> padsArea, keysArea, editorArea, waveArea;
    int dropPad = -1, pressedPad = -1;

    // 使う範囲のドラッグ（離したときに 1 回で書き込む）
    enum class TrimDrag { none, start, end };
    TrimDrag trimDrag = TrimDrag::none;
    collab::SampleCount dragStart = 0, dragEnd = 0;

    const collab::Track* track() const;
    std::vector<collab::SamplerPad> pads() const;
    collab::SamplerPad selectedPad() const     { return pads()[(size_t) selected]; }

    juce::Rectangle<int> padBounds (int index) const;
    juce::Rectangle<int> keyBounds (int index) const;
    int padAt (juce::Point<int>) const;
    int dropTarget (juce::Point<int>) const;

    collab::SampleCount sourceLength (const collab::SamplerPad&) const;
    std::pair<collab::SampleCount, collab::SampleCount> trimRange (const collab::SamplerPad&) const;
    float sampleToX (collab::SampleCount, collab::SampleCount length) const;
    collab::SampleCount xToSample (float x, collab::SampleCount length) const;

    void selectPad (int index, bool play, int velocity = 100);
    void editPads (const juce::String& description, std::function<void (std::vector<collab::SamplerPad>&)>, const juce::String& merge = {});
    void editSelected (const juce::String& description, std::function<void (collab::SamplerPad&)>, const juce::String& merge = {});
    void assignFiles (int firstPad, const juce::StringArray& files);
    void chooseFiles (int index);
    void showPadMenu (int index);
    void toggleTempo();
    void refreshEditor();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void paintPads (juce::Graphics&);
    void paintKeys (juce::Graphics&);
    void paintWave (juce::Graphics&);
};
