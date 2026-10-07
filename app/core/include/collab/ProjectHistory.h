#pragma once

#include "Project.h"

namespace collab
{

/**
    元に戻す（Undo）用の差分。曲全体の写しではなく、変わったトラックの前の状態だけを持つ
    （大きな曲で編集のたびに全体を写すと、履歴だけで何百 MB にもなり、重くなったり落ちたりした）。
    トラック以外（テンポ・コード・マスターなど）は小さいので、そのまま持つ。
*/
struct ProjectDelta
{
    Project shell;                      // before からトラックを抜いたもの
    std::vector<std::string> order;     // before のトラックの並び
    std::vector<Track> tracks;          // before のうち、after と違う（または after にない）トラック
};

/** before → after の変更を戻すための差分。 */
ProjectDelta makeDelta (const Project& before, const Project& after);

/** 同じ（before はもう使わないので、写さずに中身を移す。編集のたびの写しを減らす）。 */
ProjectDelta makeDelta (Project&& before, const Project& after);

/**
    同じ操作を続けて行ったとき（ドラッグ中など、元に戻すは 1 回にまとめる）、さらに before → after の変更を足す。
    最初に持っていなかったトラックが変わったら、その前の状態を足す。
*/
void extendDelta (ProjectDelta&, const Project& before, const Project& after);

/** current（差分を作ったときの after）を、差分を作ったときの before に戻した曲。 */
Project applyDelta (const ProjectDelta&, const Project& current);

}
