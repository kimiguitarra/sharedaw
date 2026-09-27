#pragma once

#include <set>

#include "Common.h"
#include "collab/Grid.h"
#include "collab/TempoMap.h"

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
    select,   // 1: 選択・移動・長さの変更（既定）
    pencil,   // 2: オブジェクト（テンポ・拍子・コード・クリップ・ノート）を置く
    split,    // 3: はさみ（クリック位置でクリップ・ノートを分割）
    glue,     // 4: のり（クリックしたクリップ・ノートを次のものとつなげる）
    erase     // 5: 消しゴム（クリックしたものを消す）
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
    juce::KeyPress selectToolKey2, pencilToolKey2; // 同（キーボード上段の数字）
    juce::KeyPress splitToolKey, glueToolKey, eraseToolKey;   // 3 / 4 / 5（Cubase と同じ番号）
    juce::KeyPress mixerKey;                       // ミキサーの表示
    juce::KeyPress loopToSelectionKey;             // 選択範囲をループ範囲にする
    juce::KeyPress stopKey, toStartKey, recordKey, loopKey;   // テンキーのトランスポート
    juce::KeyPress zoomInKey, zoomOutKey;          // 横方向の拡大・縮小
    juce::KeyPress autoScrollKey;                  // 再生位置への自動スクロールの切り替え
    juce::KeyPress snapKey;                        // スナップ（クオンタイズ値に合わせる／フリー）の切り替え
    bool pencilClickOnNoteDeletes = true;          // 鉛筆で既存のノートをクリックすると消す（Cubase のキーエディター）

    static EditBehaviour forMode (OperationMode mode)
    {
        EditBehaviour b;
        b.selectToolKey = juce::KeyPress (juce::KeyPress::numberPad1);
        b.pencilToolKey = juce::KeyPress (juce::KeyPress::numberPad2);
        b.selectToolKey2 = juce::KeyPress ('1');
        b.pencilToolKey2 = juce::KeyPress ('2');
        b.splitToolKey = juce::KeyPress ('3');
        b.glueToolKey = juce::KeyPress ('4');
        b.eraseToolKey = juce::KeyPress ('5');
        b.mixerKey = juce::KeyPress (juce::KeyPress::F3Key);
        b.loopToSelectionKey = juce::KeyPress ('p');
        b.stopKey = juce::KeyPress (juce::KeyPress::numberPad0);
        b.toStartKey = juce::KeyPress (juce::KeyPress::numberPadDecimalPoint);
        b.recordKey = juce::KeyPress (juce::KeyPress::numberPadMultiply);
        b.loopKey = juce::KeyPress (juce::KeyPress::numberPadDivide);
        b.zoomInKey = juce::KeyPress ('h');
        b.zoomOutKey = juce::KeyPress ('g');
        b.autoScrollKey = juce::KeyPress ('f');
        b.snapKey = juce::KeyPress ('j');

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
    bool eraser() const noexcept           { return tool == EditTool::erase; }

    TimeAxis timeline;
    TimeAxis pianoRoll { 120.0, 0.0 };

    collab::Grid grid { 16, 1, true };      // クオンタイズ値（ピアノロールのグリッド、再生位置の移動）
    collab::Grid timelineGrid { 4, 1, true };

    /** スナップ（J）。オフのときはクオンタイズ値に合わせずフリーに動かす。 */
    bool snapEnabled() const noexcept      { return grid.enabled; }
    void setSnapEnabled (bool on)          { grid.enabled = on; timelineGrid.enabled = on; changed(); }

    /** クオンタイズ値を変える（スナップのオン・オフはそのまま）。 */
    void setQuantise (const collab::Grid& g)
    {
        const bool on = grid.enabled;
        grid = g;
        grid.enabled = on;
        changed();
    }

    int quantisePresetIndex() const
    {
        const auto presets = collab::Grid::presets();

        for (int i = 0; i < (int) presets.size(); ++i)
            if (presets[(size_t) i].sameValue (grid))
                return i;

        return -1;
    }

    /** 再生位置の移動先をクオンタイズ値に合わせる（Alt で一時的にフリー）。 */
    double snapCursor (double tick, const collab::TempoMap& map, const juce::ModifierKeys& mods) const
    {
        tick = juce::jmax (0.0, tick);
        return mods.isAltDown() ? tick : (double) grid.snap ((collab::Tick) std::llround (tick), map);
    }

    bool autoScroll = true;                     // 再生中に再生位置を追ってスクロールする（F）

    std::string selectedTrackId;
    std::string selectedClipId;             // 主に選んでいるクリップ（ピアノロールで開くもの）
    std::set<std::string> selectedClipIds;  // 選択中のクリップすべて（selectedClipId を含む）

    bool isClipSelected (const std::string& id) const   { return selectedClipIds.count (id) > 0 || (! id.empty() && id == selectedClipId); }

    /** クリップを 1 つだけ選ぶ（空なら選択なし）。 */
    void selectClip (const std::string& id)
    {
        selectedClipId = id;
        selectedClipIds.clear();

        if (! id.empty())
            selectedClipIds.insert (id);
    }

    /** Ctrl / Shift クリック: 選択に加える・外す。 */
    void toggleClip (const std::string& id)
    {
        if (selectedClipIds.erase (id) > 0)
        {
            if (selectedClipId == id)
                selectedClipId = selectedClipIds.empty() ? std::string() : *selectedClipIds.begin();
        }
        else
        {
            selectedClipIds.insert (id);
            selectedClipId = id;
        }
    }

    /** 選択中のクリップ（主のクリップも含む）。 */
    std::set<std::string> clipSelection() const
    {
        auto s = selectedClipIds;

        if (! selectedClipId.empty())
            s.insert (selectedClipId);

        return s;
    }
    std::string selectedChordId;
    std::string selectedTempoId;
    std::string selectedMeterId;
    std::string selectedMarkerId;

    bool loopEnabled = false;
    collab::Tick loopStart = 0;
    collab::Tick loopEnd = collab::kPpq * 16;

    float masterVolumeDb = 0.0f;   // マスター音量（この PC だけの設定）
    bool metronomeEnabled = false;
    float metronomeVolumeDb = -6.0f;
    int countInBars = 1;           // 録音のカウントイン（0〜2 小節）

    double playheadTick = 0.0;

    void changed()      { sendChangeMessage(); }
};
