#pragma once

#include "ui/AppContext.h"
#include "collab/Sampler.h"

/**
    内蔵サンプラーの画面: 16 個のパッド（4 × 4、左下がパッド 1）。
    パッドにオーディオファイルをドロップ（または右クリック → 読み込む）すると、曲のオーディオとして保存して割り当てる。
    曲と一緒に同期されるので、ほかの人も MIDI を打ち込めば同じ音を鳴らせる。パッドを押すと試し弾き。
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

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int x, int y) override     { dropPad = padAt ({ x, y }); repaint(); }
    void fileDragMove (const juce::StringArray&, int x, int y) override      { dropPad = padAt ({ x, y }); repaint(); }
    void fileDragExit (const juce::StringArray&) override                    { dropPad = -1; repaint(); }
    void filesDropped (const juce::StringArray&, int x, int y) override;

private:
    AppContext& ctx;
    std::string trackId;
    juce::Slider volume;
    juce::Label volumeLabel { {}, "音量"_ju };
    juce::String mergeId;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> padsArea;
    int dropPad = -1, pressedPad = -1;

    const collab::Track* track() const;
    std::vector<collab::SamplerPad> pads() const;
    juce::Rectangle<int> padBounds (int index) const;
    int padAt (juce::Point<int>) const;

    void editPads (const juce::String& description, std::function<void (std::vector<collab::SamplerPad>&)>);
    void assignFiles (int firstPad, const juce::StringArray& files);
    void showPadMenu (int index);
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
};
