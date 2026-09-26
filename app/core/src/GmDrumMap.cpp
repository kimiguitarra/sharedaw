#include "collab/GmDrumMap.h"

namespace collab
{

std::string gmDrumName (int note)
{
    switch (note)
    {
        case 35: return "キック2";
        case 36: return "キック";
        case 37: return "サイドスティック";
        case 38: return "スネア";
        case 39: return "クラップ";
        case 40: return "スネア2";
        case 41: return "フロアタム2";
        case 42: return "ハイハット（クローズ）";
        case 43: return "フロアタム";
        case 44: return "ハイハット（ペダル）";
        case 45: return "ロータム";
        case 46: return "ハイハット（オープン）";
        case 47: return "ローミッドタム";
        case 48: return "ハイミッドタム";
        case 49: return "クラッシュ";
        case 50: return "ハイタム";
        case 51: return "ライド";
        case 52: return "チャイナ";
        case 53: return "ライドベル";
        case 54: return "タンバリン";
        case 55: return "スプラッシュ";
        case 56: return "カウベル";
        case 57: return "クラッシュ2";
        case 58: return "ビブラスラップ";
        case 59: return "ライド2";
        case 60: return "ハイボンゴ";
        case 61: return "ローボンゴ";
        case 62: return "コンガ（ミュート）";
        case 63: return "コンガ（オープン）";
        case 64: return "ローコンガ";
        case 65: return "ハイティンバレス";
        case 66: return "ローティンバレス";
        case 67: return "ハイアゴゴ";
        case 68: return "ローアゴゴ";
        case 69: return "カバサ";
        case 70: return "マラカス";
        case 71: return "ホイッスル（短）";
        case 72: return "ホイッスル（長）";
        case 73: return "ギロ（短）";
        case 74: return "ギロ（長）";
        case 75: return "クラベス";
        case 76: return "ハイウッドブロック";
        case 77: return "ローウッドブロック";
        case 78: return "クイーカ（ミュート）";
        case 79: return "クイーカ（オープン）";
        case 80: return "トライアングル（ミュート）";
        case 81: return "トライアングル（オープン）";
        default: return {};
    }
}

std::string midiNoteName (int note)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    if (note < 0 || note > 127)
        return {};

    return std::string (names[note % 12]) + std::to_string (note / 12 - 1);
}

} // namespace collab
