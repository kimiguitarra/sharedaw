#include "MidiImport.h"

#include "collab/BuiltinInstruments.h"
#include "collab/Uuid.h"

namespace MidiImport
{

std::string Part::builtinInstrument() const
{
    // GM: チャンネル 10 はドラム、プログラム 33〜40 はベース、5・6 はエレピ
    if (channel == 10)
        return collab::builtin::drums;

    if (program >= 32 && program <= 39)
        return collab::builtin::bass;

    if (program == 4 || program == 5)
        return collab::builtin::epiano;

    return collab::builtin::piano;
}

collab::Tick Result::endTick() const
{
    collab::Tick end = 0;

    for (auto& p : parts)
        end = std::max (end, p.endTick);

    return end;
}

bool isMidiFile (const juce::File& f)
{
    return f.hasFileExtension ("mid;midi;smf");
}

Result read (const juce::File& file)
{
    Result result;
    juce::FileInputStream in (file);
    juce::MidiFile mf;

    if (! in.openedOk() || ! mf.readFrom (in))
    {
        result.error = "MIDI ファイルとして読み込めません"_ju;
        return result;
    }

    const int tpq = mf.getTimeFormat();

    if (tpq <= 0)
    {
        result.error = "SMPTE 形式の MIDI ファイルには対応していません"_ju;
        return result;
    }

    const double scale = (double) collab::kPpq / (double) tpq;
    auto toTick = [scale] (double t) { return (collab::Tick) std::llround (t * scale); };
    double firstTempoTime = 1e30, firstMeterTime = 1e30;

    for (int ti = 0; ti < mf.getNumTracks(); ++ti)
    {
        juce::MidiMessageSequence seq (*mf.getTrack (ti));
        seq.updateMatchedPairs();

        std::string trackName;
        std::map<int, Part> byChannel;

        for (int i = 0; i < seq.getNumEvents(); ++i)
        {
            auto* ev = seq.getEventPointer (i);
            const auto& m = ev->message;
            const double time = m.getTimeStamp();

            if (m.isTrackNameEvent() && trackName.empty())
                trackName = m.getTextFromTextMetaEvent().trim().toStdString();

            if (m.isTempoMetaEvent() && time < firstTempoTime && m.getTempoSecondsPerQuarterNote() > 0.0)
            {
                firstTempoTime = time;
                result.bpm = juce::jlimit (10.0, 999.0, 60.0 / m.getTempoSecondsPerQuarterNote());
            }

            if (m.isTimeSignatureMetaEvent() && time < firstMeterTime)
            {
                int num = 4, den = 4;
                m.getTimeSignatureInfo (num, den);
                firstMeterTime = time;
                result.meter = std::make_pair (juce::jlimit (1, 64, num), den);
            }

            if (m.isProgramChange())
            {
                auto& part = byChannel[m.getChannel()];

                if (part.program < 0)
                    part.program = m.getProgramChangeNumber();
            }

            if (m.isPitchWheel())
                byChannel[m.getChannel()].pitchBends.push_back ({ toTick (time), m.getPitchWheelValue() - 8192 });

            if (m.isNoteOn() && m.getVelocity() > 0)
            {
                auto& part = byChannel[m.getChannel()];
                const auto start = toTick (time);
                const double offTime = ev->noteOffObject != nullptr ? ev->noteOffObject->message.getTimeStamp()
                                                                    : time + tpq / 4.0;
                const auto length = std::max<collab::Tick> (10, toTick (offTime) - start);

                collab::Note n;
                n.id = collab::generateUuid();
                n.tick = start;
                n.lengthTick = length;
                n.pitch = m.getNoteNumber();
                n.velocity = juce::jlimit (1, 127, (int) m.getVelocity());
                part.notes.push_back (n);
                part.endTick = std::max (part.endTick, start + length);
            }
        }

        for (auto& [channel, part] : byChannel)
        {
            if (part.notes.empty())
                continue;

            part.channel = channel;
            part.name = ! trackName.empty() ? trackName
                                            : file.getFileNameWithoutExtension().toStdString() + (channel == 10 ? " Drums" : "");
            result.parts.push_back (std::move (part));
        }
    }

    if (result.parts.empty())
        result.error = "ノートがありません"_ju;

    return result;
}

} // namespace MidiImport
