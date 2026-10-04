#pragma once

#include "Common.h"

class ProjectDocument;

/**
    オーディオファイルの一覧（ファイル → オーディオファイル…、ツールバーのボタン）。
    曲の audio フォルダのファイルを、どのトラックで使っているかと一緒に並べ、使っていないもの（失敗した録音など）をまとめて消せる。
    消したファイルはゴミ箱へ移す（元に戻すでクリップを戻したときは、ゴミ箱から戻せば音が出る）。
*/
class AudioFilesPanel  : public juce::Component,
                         private juce::ListBoxModel
{
public:
    explicit AudioFilesPanel (ProjectDocument&);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** 使っていないファイル（曲のどのクリップ・バウンスからも使われていないもの）。 */
    static juce::Array<juce::File> unusedFiles (const ProjectDocument&);

private:
    struct Row
    {
        juce::File file;
        juce::int64 size = 0;
        juce::String usedBy;   // 使っているトラック（空なら使っていない）
    };

    ProjectDocument& document;
    std::vector<Row> rows;
    juce::ListBox list;
    juce::Label summary;
    juce::TextButton deleteButton, revealButton, closeButton;

    void refresh();
    void deleteUnused();

    int getNumRows() override                                   { return (int) rows.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
};
