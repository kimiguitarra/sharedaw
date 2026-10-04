#pragma once

#include <array>
#include <atomic>

#include "Common.h"

/**
    MIDI キーボードで弾いた音を、選択中のトラックの音源だけに通す門。

    Tracktion は MIDI の入力先（ターゲット）を変えるたびに再生の処理の組み立て（グラフ）を作り直すので、
    再生中に選択トラックを切り替えると、その瞬間に音が途切れることがある（プツっと鳴る）。
    そこで MIDI の入力はすべての MIDI トラックにつないだままにし、どのトラックの音源で鳴らすかはここで決める。
    キーボードから来たかどうかは、メッセージの送り元（MPESourceID。入力機器ごとに決まる）で見分ける。
*/
class LiveMidiGate
{
public:
    /** 入力機器の送り元。EngineBridge が入力の一覧を作るときに登録する。 */
    static void setLiveSources (const std::vector<te::MPESourceID>& ids)
    {
        auto& s = sources();

        for (size_t i = 0; i < s.size(); ++i)
            s[i].store (i < ids.size() ? ids[i] : noSource, std::memory_order_relaxed);
    }

    static constexpr te::MPESourceID noSource = 0;
    static constexpr te::MPESourceID allSources = 0xffffffffu;

    /** 通す送り元: allSources ならすべての入力機器、noSource なら通さない（既定）、それ以外はその機器だけ。 */
    void setAllowed (te::MPESourceID id)        { allowed.store (id, std::memory_order_relaxed); }

    /** 音の処理のスレッドから呼ぶ: 通さないキーボードの音を取り除く（音を止めるメッセージは通す。鳴りっぱなしにしない）。 */
    void filter (te::MidiMessageArray& midi) const
    {
        const auto ok = allowed.load (std::memory_order_relaxed);

        if (ok == allSources || midi.isEmpty())
            return;

        for (int i = midi.size(); --i >= 0;)
        {
            const auto& m = midi[i];

            if (! isLive (m.mpeSourceID) || m.mpeSourceID == ok)
                continue;

            // ピッチベンドを中央に戻すのも通す（弾いている途中で選択を変えても、前のトラックが曲がったままにならない）
            const bool stopsSound = m.isNoteOff() || m.isAllNotesOff() || m.isAllSoundOff()
                                     || m.isSustainPedalOff() || (m.isPitchWheel() && m.getPitchWheelValue() == 8192);

            if (! stopsSound)
                midi.remove (i);
        }
    }

private:
    std::atomic<te::MPESourceID> allowed { noSource };

    static std::array<std::atomic<te::MPESourceID>, 32>& sources()
    {
        static std::array<std::atomic<te::MPESourceID>, 32> s {};
        return s;
    }

    static bool isLive (te::MPESourceID id)
    {
        if (id == noSource)
            return false;

        for (auto& s : sources())
            if (s.load (std::memory_order_relaxed) == id)
                return true;

        return false;
    }
};
