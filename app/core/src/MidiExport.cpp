#include "collab/MidiExport.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "collab/BuiltinInstruments.h"
#include "collab/ChordPlayback.h"
#include "collab/chord/Degree.h"

namespace collab
{

namespace
{
    struct Event
    {
        Tick tick = 0;
        int order = 0;                  // 同じ tick では小さいほど先（ノートオフ → メタ → プログラム → ノートオン）
        std::vector<std::uint8_t> bytes;
    };

    void putVarLen (std::vector<std::uint8_t>& out, std::uint32_t v)
    {
        std::uint8_t buf[5];
        int n = 0;
        buf[n++] = (std::uint8_t) (v & 0x7f);

        while ((v >>= 7) != 0)
            buf[n++] = (std::uint8_t) ((v & 0x7f) | 0x80);

        while (n > 0)
            out.push_back (buf[--n]);
    }

    void put32 (std::vector<std::uint8_t>& out, std::uint32_t v)
    {
        for (int s = 24; s >= 0; s -= 8)
            out.push_back ((std::uint8_t) ((v >> s) & 0xff));
    }

    Event meta (Tick tick, std::uint8_t type, const std::vector<std::uint8_t>& data)
    {
        Event e { tick, 1, { 0xff, type } };
        putVarLen (e.bytes, (std::uint32_t) data.size());
        e.bytes.insert (e.bytes.end(), data.begin(), data.end());
        return e;
    }

    Event textMeta (Tick tick, std::uint8_t type, const std::string& text)
    {
        return meta (tick, type, std::vector<std::uint8_t> (text.begin(), text.end()));
    }

    void addNote (std::vector<Event>& events, int channel, Tick tick, Tick length, int pitch, int velocity)
    {
        const auto ch = (std::uint8_t) (channel & 0x0f);
        const auto p = (std::uint8_t) std::clamp (pitch, 0, 127);
        events.push_back ({ tick, 3, { (std::uint8_t) (0x90 | ch), p, (std::uint8_t) std::clamp (velocity, 1, 127) } });
        events.push_back ({ tick + std::max<Tick> (1, length), 0, { (std::uint8_t) (0x80 | ch), p, 0x40 } });
    }

    std::vector<std::uint8_t> trackChunk (std::vector<Event> events)
    {
        std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b)
        {
            return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
        });

        std::vector<std::uint8_t> data;
        Tick last = 0;

        for (auto& e : events)
        {
            putVarLen (data, (std::uint32_t) std::max<Tick> (0, e.tick - last));
            data.insert (data.end(), e.bytes.begin(), e.bytes.end());
            last = std::max (last, e.tick);
        }

        // トラックの終わり
        data.insert (data.end(), { 0x00, 0xff, 0x2f, 0x00 });

        std::vector<std::uint8_t> chunk { 'M', 'T', 'r', 'k' };
        put32 (chunk, (std::uint32_t) data.size());
        chunk.insert (chunk.end(), data.begin(), data.end());
        return chunk;
    }

    /** 内蔵音源の GM プログラム番号（0 始まり）。分からなければ -1。 */
    int gmProgramFor (const Track& t)
    {
        if (! t.instrument || t.instrument->kind != Instrument::Kind::builtin)
            return -1;

        const auto& id = t.instrument->id;

        if (id == builtin::piano)   return 0;    // Acoustic Grand Piano
        if (id == builtin::epiano)  return 4;    // Electric Piano 1

        if (id == builtin::bass)
        {
            const auto& p = t.instrument->params;
            const auto preset = p.is_object() && p.contains ("preset") && p["preset"].is_string() ? p["preset"].get<std::string>() : std::string();
            return preset == "upright" ? 32 : preset == "jazz-pick" ? 34 : 33;   // Acoustic / Electric (pick) / Electric (finger)
        }

        return -1;
    }

    bool isDrums (const Track& t)
    {
        return t.instrument && t.instrument->kind == Instrument::Kind::builtin && t.instrument->id == builtin::drums;
    }
}

std::vector<std::uint8_t> writeMidiFile (const Project& project, const TempoMap& map, bool includeChordTrack)
{
    std::vector<std::vector<std::uint8_t>> chunks;

    // 1 本目: 曲名・テンポ・拍子・キー・マーカー
    {
        std::vector<Event> events;

        if (! project.name.empty())
            events.push_back (textMeta (0, 0x03, project.name));

        auto tempos = project.tempoTrack.events;
        std::stable_sort (tempos.begin(), tempos.end(), [] (auto& a, auto& b) { return a.tick < b.tick; });

        if (tempos.empty() || tempos.front().tick > 0)
            tempos.insert (tempos.begin(), TempoEvent { {}, 0, map.bpmAtTick (0) });

        for (auto& t : tempos)
        {
            const auto usPerQuarter = (std::uint32_t) std::llround (60'000'000.0 / std::max (1.0, t.bpm));
            events.push_back (meta (std::max<Tick> (0, t.tick), 0x51,
                                    { (std::uint8_t) (usPerQuarter >> 16), (std::uint8_t) (usPerQuarter >> 8), (std::uint8_t) usPerQuarter }));
        }

        auto meters = project.meterTrack.events;
        std::stable_sort (meters.begin(), meters.end(), [] (auto& a, auto& b) { return a.bar < b.bar; });

        if (meters.empty() || meters.front().bar > 1)
            meters.insert (meters.begin(), MeterEvent { {}, 1, 4, 4 });

        for (auto& m : meters)
        {
            int log2Denominator = 0;

            while ((1 << log2Denominator) < m.denominator && log2Denominator < 7)
                ++log2Denominator;

            events.push_back (meta (map.barToTick (std::max (1, m.bar)), 0x58,
                                    { (std::uint8_t) m.numerator, (std::uint8_t) log2Denominator, 24, 8 }));
        }

        for (auto& k : project.keyTrack.events)
        {
            const int sf = chord::keySignature ({ k.tonic, k.minor });   // 画面のキーの書き方（Gb など）と同じ向き
            events.push_back (meta (map.barToTick (std::max (1, k.bar)), 0x59, { (std::uint8_t) (std::int8_t) sf, (std::uint8_t) (k.minor ? 1 : 0) }));
        }

        for (auto& m : project.markerTrack.events)
            events.push_back (textMeta (m.tick, 0x06, m.name.empty() ? std::string ("Marker") : m.name));

        chunks.push_back (trackChunk (std::move (events)));
    }

    // MIDI トラック
    int nextChannel = 0;
    auto takeChannel = [&nextChannel]
    {
        if (nextChannel == 9)
            ++nextChannel;

        const int ch = nextChannel % 16;
        ++nextChannel;
        return ch == 9 ? 0 : ch;
    };

    for (auto& t : project.tracks)
    {
        if (t.type != TrackType::midi)
            continue;

        const int channel = isDrums (t) ? 9 : takeChannel();
        std::vector<Event> events;
        events.push_back (textMeta (0, 0x03, t.name));

        if (const int program = gmProgramFor (t); program >= 0)
            events.push_back ({ 0, 2, { (std::uint8_t) (0xc0 | channel), (std::uint8_t) program } });

        for (auto& clip : t.midiClips)
            for (auto& n : clip.notes)
            {
                // クリップの外のノートは鳴らないので書かない。クリップの端を越える分は切る
                if (n.tick < 0 || n.tick >= clip.lengthTick)
                    continue;

                const Tick length = std::min (n.lengthTick, clip.lengthTick - n.tick);
                addNote (events, channel, clip.startTick + n.tick, length, n.pitch, n.velocity);
            }

        chunks.push_back (trackChunk (std::move (events)));
    }

    // コードトラック
    if (includeChordTrack && ! project.chordTrack.events.empty())
    {
        const auto notes = renderChordTrack (project, map);

        if (! notes.empty())
        {
            const int channel = takeChannel();
            std::vector<Event> events;
            events.push_back (textMeta (0, 0x03, "Chords"));
            events.push_back ({ 0, 2, { (std::uint8_t) (0xc0 | channel), 0 } });

            for (auto& n : notes)
                addNote (events, channel, n.tick, n.lengthTick, n.pitch, n.velocity);

            chunks.push_back (trackChunk (std::move (events)));
        }
    }

    std::vector<std::uint8_t> file { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1 };
    file.push_back ((std::uint8_t) (chunks.size() >> 8));
    file.push_back ((std::uint8_t) (chunks.size() & 0xff));
    file.push_back ((std::uint8_t) (kPpq >> 8));
    file.push_back ((std::uint8_t) (kPpq & 0xff));

    for (auto& c : chunks)
        file.insert (file.end(), c.begin(), c.end());

    return file;
}

} // namespace collab
