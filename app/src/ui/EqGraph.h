#pragma once

#include "ui/AppContext.h"

/**
    グラフィカル EQ（Cubase のチャンネル設定の EQ 画面のように、スペクトラムの上で EQ カーブを直接いじる）。
    ノード（LC・L・LM・M・H・HC）をドラッグ: 左右で周波数、上下でゲイン。ホイールで Q（ピーキング）、
    ダブルクリックでそのバンドを既定値に戻す。ローカット・ハイカットは端までドラッグするとオフ。
*/
class EqGraph  : public juce::Component,
                 private juce::Timer
{
public:
    explicit EqGraph (AppContext&);
    ~EqGraph() override;

    /** 表示する EQ（ドキュメントが変わったら呼ぶ）。 */
    void setEq (const collab::ChannelEq&);

    /** 編集を適用する。merge が true ならドラッグ中の一連の変更を 1 つの取り消し単位にまとめる。 */
    std::function<void (const juce::String& description, std::function<void (collab::ChannelEq&)>, bool merge)> onEdit;
    std::function<void()> onEditEnd;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    enum Band { lowCut, low, lowMid, mid, high, highCut, numBands };

    AppContext& ctx;
    collab::ChannelEq eq;

    static constexpr int fftOrder = 12, fftSize = 1 << fftOrder;
    static constexpr int numPoints = 200;       // 表示する点（20 Hz〜20 kHz を対数で等分）
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> fftData;
    std::vector<double> binPower;               // ビンごとの平滑化したパワー（時間方向）
    std::vector<float> shownDb;                 // 表示する点ごとのレベル（傾き補正・周波数方向の平滑化の後）
    double spectrumRate = 48000.0;
    bool hasSpectrum = false;

    void updateShownSpectrum();

    int hoverBand = -1, dragBand = -1;
    bool merging = false;

    juce::Rectangle<float> plotArea() const;
    float xForFreq (double hz) const;
    double freqForX (float x) const;
    float yForDb (double db) const;
    double dbForY (float y) const;

    juce::Point<float> nodePosition (int band) const;
    int bandAt (juce::Point<float>) const;
    static juce::String bandName (int band);
    static juce::Colour bandColour (int band);
    juce::String describe (int band) const;

    void timerCallback() override;
};
