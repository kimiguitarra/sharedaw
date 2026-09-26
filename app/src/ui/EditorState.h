#pragma once

#include "Common.h"
#include "collab/Grid.h"

/** 横軸（tick ⇔ ピクセル）。タイムラインとピアノロールでそれぞれ持つ。 */
struct TimeAxis
{
    double pixelsPerQuarter = 40.0;
    double scrollTick = 0.0;

    double tickToX (double tick) const noexcept    { return (tick - scrollTick) * pixelsPerQuarter / collab::kPpq; }
    double xToTick (double x) const noexcept       { return x * collab::kPpq / pixelsPerQuarter + scrollTick; }
    double pixelsPerTick() const noexcept          { return pixelsPerQuarter / collab::kPpq; }

    /** マウス位置を中心に拡大・縮小する。 */
    void zoomAround (double x, double factor, double minPpq = 4.0, double maxPpq = 800.0)
    {
        const double tickAtX = xToTick (x);
        pixelsPerQuarter = juce::jlimit (minPpq, maxPpq, pixelsPerQuarter * factor);
        scrollTick = juce::jmax (0.0, tickAtX - x * collab::kPpq / pixelsPerQuarter);
    }
};

/** マウスで使う道具（Cubase のツールに相当）。 */
enum class EditTool
{
    select,   // 選択・移動・長さ変更（既定）
    pencil    // オブジェクト（テンポ・拍子・コード・クリップ・ノート）を置く
};

/**
    操作モード。キー割り当てやマウス操作の細部をモードごとに変えられるようにしておく。
    Studio One モードは、いまは Cubase モードと同じ（友人のレビュー後に作り込む）。
*/
enum class OperationMode
{
    cubase,
    studioOne
};

/** 操作モードごとの振る舞い（各部品はモードを直接見ずにここを参照する）。 */
struct EditBehaviour
{
    juce::KeyPress selectToolKey, pencilToolKey;   // ツールの切り替え（テンキー）
    juce::KeyPress mixerKey;                       // ミキサーの表示
    juce::KeyPress loopToSelectionKey;             // 選択範囲をループ範囲にする
    juce::KeyPress stopKey, toStartKey, recordKey, loopKey;   // テンキーのトランスポート
    juce::KeyPress zoomInKey, zoomOutKey;          // 横方向の拡大・縮小
    bool pencilClickOnNoteDeletes = true;          // 鉛筆で既存のノートをクリックすると消す（Cubase のキーエディター）

    static EditBehaviour forMode (OperationMode mode)
    {
        EditBehaviour b;
        b.selectToolKey = juce::KeyPress (juce::KeyPress::numberPad1);
        b.pencilToolKey = juce::KeyPress (juce::KeyPress::numberPad2);
        b.mixerKey = juce::KeyPress (juce::KeyPress::F3Key);
        b.loopToSelectionKey = juce::KeyPress ('p');
        b.stopKey = juce::KeyPress (juce::KeyPress::numberPad0);
        b.toStartKey = juce::KeyPress (juce::KeyPress::numberPadDecimalPoint);
        b.recordKey = juce::KeyPress (juce::KeyPress::numberPadMultiply);
        b.loopKey = juce::KeyPress (juce::KeyPress::numberPadDivide);
        b.zoomInKey = juce::KeyPress ('h');
        b.zoomOutKey = juce::KeyPress ('g');

        switch (mode)
        {
            case OperationMode::cubase:
            case OperationMode::studioOne:   // TODO: Studio One の操作感に合わせる
                break;
        }

        return b;
    }
};

/** 画面の表示状態（選択、ズーム、グリッド、ループなど）。プロジェクト JSON には保存しない。 */
struct EditorState  : public juce::ChangeBroadcaster
{
    EditTool tool = EditTool::select;
    OperationMode mode = OperationMode::cubase;
    EditBehaviour behaviour() const        { return EditBehaviour::forMode (mode); }
    bool pencil() const noexcept           { return tool == EditTool::pencil; }

    TimeAxis timeline;
    TimeAxis pianoRoll { 120.0, 0.0 };

    collab::Grid grid { 16, false, true };      // ピアノロールのグリッド
    collab::Grid timelineGrid { 4, false, true };

    std::string selectedTrackId;
    std::string selectedClipId;
    std::string selectedChordId;
    std::string selectedTempoId;
    std::string selectedMeterId;

    bool loopEnabled = false;
    collab::Tick loopStart = 0;
    collab::Tick loopEnd = collab::kPpq * 16;

    bool metronomeEnabled = false;
    float metronomeVolumeDb = -6.0f;
    int countInBars = 1;           // 録音のカウントイン（0〜2 小節）

    double playheadTick = 0.0;

    void changed()      { sendChangeMessage(); }
};
