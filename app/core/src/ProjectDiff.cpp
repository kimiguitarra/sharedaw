#include "collab/ProjectDiff.h"
#include "collab/chord/Degree.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <string_view>
#include <unordered_map>

#include "collab/ChordPlayback.h"
#include "collab/Render.h"
#include "collab/TempoMap.h"
#include "collab/Uuid.h"

namespace collab
{

namespace
{
    std::string fmt (double v, int decimals = 1)
    {
        std::ostringstream s;
        s.imbue (std::locale::classic());
        s.setf (std::ios::fixed);
        s.precision (decimals);
        s << v;
        return s.str();
    }

    std::string barRange (const TempoMap& map, Tick from, Tick to)
    {
        const int a = map.tickToBar (from);
        const int b = map.tickToBar (std::max (from, to - 1));
        return a == b ? std::to_string (a) + "小節目" : std::to_string (a) + "〜" + std::to_string (b) + "小節目";
    }

    struct Range
    {
        Tick from = -1, to = -1;

        void add (Tick a, Tick b)
        {
            if (from < 0 || a < from) from = a;
            if (to < 0 || b > to)     to = b;
        }

        bool valid() const    { return from >= 0; }
    };

    std::string instrumentName (const std::optional<Instrument>& i)
    {
        if (! i)
            return "なし";

        if (i->kind == Instrument::Kind::builtin)
            return i->id + " " + i->version;

        return i->plugin.name + " (" + i->plugin.format + ")";
    }

    std::string jsonValueText (const nlohmann::json& j)
    {
        if (j.is_null())       return "なし";
        if (j.is_string())     return j.get<std::string>();
        if (j.is_number())     return fmt (j.get<double>(), 1);
        return j.dump();
    }

    /** 内蔵音源パラメータの差分を日本語にする。 */
    void diffParams (const nlohmann::json& a, const nlohmann::json& b, std::vector<std::string>& out)
    {
        static const std::map<std::string, std::string> topNames = {
            { "volumeDb", "音源の音量" }, { "pan", "音源のパン" }, { "tone", "トーン" }, { "kit", "キット" }, { "preset", "音色" }
        };

        static const std::map<std::string, std::string> pieceFields = {
            { "volumeDb", "音量" }, { "pan", "パン" }, { "tune", "チューニング" }
        };

        std::set<std::string> keys;
        for (auto& [k, v] : a.items()) keys.insert (k);
        for (auto& [k, v] : b.items()) keys.insert (k);

        for (auto& k : keys)
        {
            const auto va = a.contains (k) ? a[k] : nlohmann::json();
            const auto vb = b.contains (k) ? b[k] : nlohmann::json();

            if (va == vb)
                continue;

            if (k == "pieces" && (va.is_object() || va.is_null()) && (vb.is_object() || vb.is_null()))
            {
                const auto pa = va.is_object() ? va : nlohmann::json::object();
                const auto pb = vb.is_object() ? vb : nlohmann::json::object();
                std::set<std::string> pieces;
                for (auto& [pk, pv] : pa.items()) pieces.insert (pk);
                for (auto& [pk, pv] : pb.items()) pieces.insert (pk);

                for (auto& piece : pieces)
                {
                    const auto xa = pa.contains (piece) ? pa[piece] : nlohmann::json::object();
                    const auto xb = pb.contains (piece) ? pb[piece] : nlohmann::json::object();

                    if (xa == xb)
                        continue;

                    const auto name = drumPieceDisplayName (piece);
                    std::set<std::string> fields;
                    for (auto& [fk, fv] : xa.items()) fields.insert (fk);
                    for (auto& [fk, fv] : xb.items()) fields.insert (fk);

                    for (auto& f : fields)
                    {
                        const auto fa = xa.contains (f) ? xa[f] : nlohmann::json();
                        const auto fb = xb.contains (f) ? xb[f] : nlohmann::json();

                        if (fa == fb)
                            continue;

                        if (f == "sample")
                            out.push_back (name + "のサンプルを差し替え（" + jsonValueText (fa) + " → " + jsonValueText (fb) + "）");
                        else if (auto it = pieceFields.find (f); it != pieceFields.end())
                            out.push_back (name + "の" + it->second + "を変更（" + jsonValueText (fa) + " → " + jsonValueText (fb) + "）");
                        else
                            out.push_back (name + "の " + f + " を変更");
                    }
                }

                continue;
            }

            if (auto it = topNames.find (k); it != topNames.end())
                out.push_back (it->second + "を変更（" + jsonValueText (va) + " → " + jsonValueText (vb) + "）");
            else
                out.push_back ("音源の設定 " + k + " を変更");
        }
    }

    template <typename T>
    const T* findById (const std::vector<T>& v, const std::string& id)
    {
        for (auto& x : v)
            if (x.id == id)
                return &x;

        return nullptr;
    }

    void diffTrack (const Track* a, const Track* b, const TempoMap& mapA, const TempoMap& mapB, ProjectDiff& diff)
    {
        const auto& ref = b != nullptr ? *b : *a;
        auto add = [&] (Change::Category cat, std::string summary, Range r = {}, std::string part = "other", std::string itemId = {})
        {
            diff.changes.push_back ({ ref.id, ScopeKind::track, ref.name, cat, std::move (summary), r.from, r.to, std::move (part), std::move (itemId) });
        };

        if (a == nullptr)
        {
            Range r;
            for (auto& c : b->midiClips) r.add (c.startTick, c.endTick());
            add (Change::Category::track, "トラックを追加", r, "trackAdded");
            return;
        }

        if (b == nullptr)
        {
            add (Change::Category::track, "トラックを削除", {}, "trackRemoved");
            return;
        }

        const auto changesBefore = diff.changes.size();

        // トラックのプロパティ
        if (a->type != b->type)   add (Change::Category::track, "トラックの種類を変更", {}, "scope");
        if (a->name != b->name)   add (Change::Category::track, "名前を変更（" + a->name + " → " + b->name + "）", {}, "name");
        if (a->color != b->color) add (Change::Category::track, "色を変更", {}, "color");
        if (std::abs (a->volumeDb - b->volumeDb) > 1e-9)
            add (Change::Category::track, "音量を変更（" + fmt (a->volumeDb) + " → " + fmt (b->volumeDb) + " dB）", {}, "volume");
        if (std::abs (a->pan - b->pan) > 1e-9)
            add (Change::Category::track, "パンを変更（" + fmt (a->pan, 2) + " → " + fmt (b->pan, 2) + "）", {}, "pan");
        if (a->mute != b->mute)   add (Change::Category::track, b->mute ? "ミュート" : "ミュート解除", {}, "mute");
        if (a->solo != b->solo)   add (Change::Category::track, b->solo ? "ソロ" : "ソロ解除", {}, "solo");

        // 音源
        if (a->instrument != b->instrument)
        {
            const bool sameInstrument = a->instrument && b->instrument
                                          && a->instrument->kind == b->instrument->kind
                                          && a->instrument->id == b->instrument->id
                                          && a->instrument->version == b->instrument->version
                                          && a->instrument->plugin == b->instrument->plugin;

            if (sameInstrument && a->instrument->kind == Instrument::Kind::builtin)
            {
                std::vector<std::string> lines;
                diffParams (a->instrument->params, b->instrument->params, lines);

                for (auto& l : lines)
                    add (Change::Category::instrument, l, {}, "instrument");
            }
            else if (sameInstrument)
            {
                add (Change::Category::instrument, "プラグインの設定を変更", {}, "instrument");
            }
            else
            {
                add (Change::Category::instrument, "音源を変更（" + instrumentName (a->instrument) + " → " + instrumentName (b->instrument) + "）", {}, "instrument");
            }
        }

        if (a->effects != b->effects)
            add (Change::Category::instrument, "エフェクトを変更", {}, "effects");

        if (a->output != b->output)
            add (Change::Category::track, "出力先を変更", {}, "output");

        if (a->sends != b->sends)
            add (Change::Category::track, "センドを変更", {}, "sends");

        if (a->strip.eq != b->strip.eq)
            add (Change::Category::track, a->strip.eq.enabled != b->strip.eq.enabled ? (b->strip.eq.enabled ? "EQ をオン" : "EQ をオフ") : "EQ を変更", {}, "eq");

        if (a->strip.comp != b->strip.comp)
            add (Change::Category::track, a->strip.comp.enabled != b->strip.comp.enabled ? (b->strip.comp.enabled ? "コンプをオン" : "コンプをオフ") : "コンプを変更", {}, "comp");

        if (a->strip.compFirst != b->strip.compFirst)
            add (Change::Category::track, b->strip.compFirst ? "Comp → EQ の順に変更" : "EQ → Comp の順に変更", {}, "compFirst");

        if (a->inputChannels != b->inputChannels || a->outputChannels != b->outputChannels)
            add (Change::Category::track, "入出力（モノ / ステレオ）を変更", {}, "io");

        if (std::abs (a->crossfadeMs - b->crossfadeMs) > 1e-9 || a->crossfadeShape != b->crossfadeShape)
            add (Change::Category::track, "テイクのつなぎを変更", {}, "crossfade");

        if (a->render != b->render)
            add (Change::Category::render, b->render ? "バウンスを更新" : "バウンスを削除", {}, "render");

        // MIDI クリップとノート
        int notesAdded = 0, notesRemoved = 0, notesChanged = 0;
        Range noteRange;

        for (auto& cb : b->midiClips)
        {
            auto* ca = a->findMidiClip (cb.id);

            if (ca == nullptr)
            {
                Range r;
                r.add (cb.startTick, cb.endTick());
                add (Change::Category::clips, barRange (mapB, cb.startTick, cb.endTick()) + " クリップを追加（ノート "
                                                + std::to_string (cb.notes.size()) + " 個）", r, "midiClip", cb.id);
                continue;
            }

            if (ca->startTick != cb.startTick || ca->lengthTick != cb.lengthTick)
            {
                Range r;
                r.add (cb.startTick, cb.endTick());
                std::string what = ca->startTick != cb.startTick ? "クリップを移動（" + barRange (mapA, ca->startTick, ca->endTick())
                                                                     + " → " + barRange (mapB, cb.startTick, cb.endTick()) + "）"
                                                                 : barRange (mapB, cb.startTick, cb.endTick()) + " クリップの長さを変更";
                add (Change::Category::clips, what, r, "midiClipRange", cb.id);
            }

            // ノートは多いので、ID の索引を作って比べる（1 つずつ探すと数千ノートで遅くなる）
            std::unordered_map<std::string_view, const Note*> before, after;
            before.reserve (ca->notes.size());
            after.reserve (cb.notes.size());

            for (auto& n : ca->notes) before.emplace (n.id, &n);
            for (auto& n : cb.notes)  after.emplace (n.id, &n);

            for (auto& nb : cb.notes)
            {
                const auto found = before.find (nb.id);
                const Note* na = found != before.end() ? found->second : nullptr;

                if (na == nullptr)          { ++notesAdded;   noteRange.add (cb.startTick + nb.tick, cb.startTick + nb.endTick()); }
                else if (! (*na == nb))
                {
                    ++notesChanged;
                    noteRange.add (cb.startTick + nb.tick, cb.startTick + nb.endTick());
                    noteRange.add (ca->startTick + na->tick, ca->startTick + na->endTick());
                }
            }

            for (auto& na : ca->notes)
                if (after.count (na.id) == 0)
                {
                    ++notesRemoved;
                    noteRange.add (ca->startTick + na.tick, ca->startTick + na.endTick());
                }
        }

        for (auto& ca : a->midiClips)
            if (b->findMidiClip (ca.id) == nullptr)
            {
                Range r;
                r.add (ca.startTick, ca.endTick());
                add (Change::Category::clips, barRange (mapA, ca.startTick, ca.endTick()) + " クリップを削除", r, "midiClip", ca.id);
            }

        if (const int total = notesAdded + notesRemoved + notesChanged; total > 0)
        {
            std::string detail;
            auto part = [&] (const char* label, int n) { if (n > 0) detail += (detail.empty() ? "" : "・") + std::string (label) + std::to_string (n); };
            part ("追加", notesAdded);
            part ("削除", notesRemoved);
            part ("変更", notesChanged);

            add (Change::Category::notes, barRange (mapB, noteRange.from, noteRange.to) + " ノート変更 " + std::to_string (total)
                                             + "件（" + detail + "）", noteRange, "notes");
        }

        // オーディオクリップ
        for (auto& cb : b->audioClips)
        {
            auto* ca = findById (a->audioClips, cb.id);
            Range r;
            r.add (cb.startTick, cb.startTick + 1);

            if (ca == nullptr)
            {
                add (Change::Category::audioClips, barRange (mapB, cb.startTick, cb.startTick + 1) + " オーディオ「" + cb.displayName + "」を追加", r, "audioClip", cb.id);
                continue;
            }

            std::vector<std::string> what;
            if (ca->startTick != cb.startTick)                                                     what.push_back ("移動");
            if (ca->sourceOffsetSamples != cb.sourceOffsetSamples || ca->lengthSamples != cb.lengthSamples) what.push_back ("トリム");
            if (std::abs (ca->gainDb - cb.gainDb) > 1e-9)                                           what.push_back ("音量");
            if (ca->fadeInSamples != cb.fadeInSamples || ca->fadeOutSamples != cb.fadeOutSamples)   what.push_back ("フェード");
            if (ca->audioHash != cb.audioHash)                                                     what.push_back ("差し替え");
            if (ca->displayName != cb.displayName)                                                 what.push_back ("名前");

            if (! what.empty())
            {
                std::string joined;
                for (auto& w : what)
                    joined += (joined.empty() ? "" : "・") + w;

                add (Change::Category::audioClips, barRange (mapB, cb.startTick, cb.startTick + 1) + " オーディオ「" + cb.displayName + "」: " + joined, r, "audioClip", cb.id);
            }
        }

        for (auto& ca : a->audioClips)
            if (findById (b->audioClips, ca.id) == nullptr)
            {
                Range r;
                r.add (ca.startTick, ca.startTick + 1);
                add (Change::Category::audioClips, barRange (mapA, ca.startTick, ca.startTick + 1) + " オーディオ「" + ca.displayName + "」を削除", r, "audioClip", ca.id);
            }

        // ここで挙げていない項目だけが変わったとき（新しく足した設定など）も、変わったことは必ず見せる
        if (diff.changes.size() == changesBefore && ! (*a == *b))
            add (Change::Category::track, "その他の設定を変更", {}, "scope");
    }

    std::string chordText (const ChordEvent& e)
    {
        if (e.noChord)  return "X";
        if (e.chord)    return chord::format (toChord (*e.chord));
        return e.text;
    }
}

//==============================================================================
std::string drumPieceDisplayName (const std::string& key)
{
    static const std::map<std::string, std::string> names = {
        { "kick", "キック" }, { "rim", "サイドスティック" }, { "snare", "スネア" }, { "clap", "クラップ" },
        { "hhClosed", "ハイハット（クローズ）" }, { "hhPedal", "ハイハット（ペダル）" }, { "hhOpen", "ハイハット（オープン）" },
        { "tomLow", "ロータム" }, { "tomMid", "ミッドタム" }, { "tomHigh", "ハイタム" }, { "crash", "クラッシュ" }, { "ride", "ライド" }
    };

    auto it = names.find (key);
    return it != names.end() ? it->second : key;
}

bool ProjectDiff::touches (const std::string& scopeId) const
{
    return std::find (changedScopeIds.begin(), changedScopeIds.end(), scopeId) != changedScopeIds.end();
}

std::vector<Change> ProjectDiff::forScope (const std::string& scopeId) const
{
    std::vector<Change> r;

    for (auto& c : changes)
        if (c.scopeId == scopeId)
            r.push_back (c);

    return r;
}

std::vector<std::string> allScopeIds (const Project& p)
{
    std::vector<std::string> ids { p.tempoTrack.id, p.meterTrack.id, p.chordTrack.id, p.markerTrack.id, p.keyTrack.id, p.master.id };

    for (auto& t : p.tracks)
        ids.push_back (t.id);

    return ids;
}

bool scopeEquals (const Project& a, const Project& b, const std::string& id)
{
    if (id == a.tempoTrack.id || id == b.tempoTrack.id)  return a.tempoTrack == b.tempoTrack;
    if (id == a.meterTrack.id || id == b.meterTrack.id)  return a.meterTrack == b.meterTrack;
    if (id == a.chordTrack.id || id == b.chordTrack.id)  return a.chordTrack == b.chordTrack;
    if (id == a.markerTrack.id || id == b.markerTrack.id) return a.markerTrack == b.markerTrack;
    if (id == a.keyTrack.id || id == b.keyTrack.id)      return a.keyTrack == b.keyTrack;
    if (id == a.master.id || id == b.master.id)          return a.master == b.master;

    auto* ta = a.findTrack (id);
    auto* tb = b.findTrack (id);

    if (ta == nullptr || tb == nullptr)
        return ta == tb;

    return *ta == *tb;
}

ProjectDiff diffProjects (const Project& beforeIn, const Project& afterIn)
{
    Project before = beforeIn, after = afterIn;
    before.sortCanonical();
    after.sortCanonical();

    const TempoMap mapA (before), mapB (after);
    ProjectDiff diff;

    // テンポ
    {
        auto add = [&] (std::string s, Tick t, const std::string& id)
        {
            diff.changes.push_back ({ after.tempoTrack.id, ScopeKind::tempo, "テンポ", Change::Category::tempo, std::move (s), t, t + 1, "tempoEvent", id });
        };

        for (auto& e : after.tempoTrack.events)
        {
            auto* o = findById (before.tempoTrack.events, e.id);

            if (o == nullptr)
                add (barRange (mapB, e.tick, e.tick + 1) + " テンポ " + fmt (e.bpm, 1) + " を追加", e.tick, e.id);
            else if (! (*o == e))
                add (barRange (mapB, e.tick, e.tick + 1) + " テンポを変更（" + fmt (o->bpm, 1) + " → " + fmt (e.bpm, 1)
                       + (o->tick != e.tick ? "、位置を移動" : "") + "）", e.tick, e.id);
        }

        for (auto& o : before.tempoTrack.events)
            if (findById (after.tempoTrack.events, o.id) == nullptr)
                add (barRange (mapA, o.tick, o.tick + 1) + " テンポ " + fmt (o.bpm, 1) + " を削除", o.tick, o.id);
    }

    // 拍子
    {
        auto add = [&] (std::string s, int bar, const std::string& id)
        {
            const Tick t = mapB.barToTick (bar);
            diff.changes.push_back ({ after.meterTrack.id, ScopeKind::meter, "拍子", Change::Category::meter, std::move (s), t, t + 1, "meterEvent", id });
        };

        auto sig = [] (const MeterEvent& e) { return std::to_string (e.numerator) + "/" + std::to_string (e.denominator); };

        for (auto& e : after.meterTrack.events)
        {
            auto* o = findById (before.meterTrack.events, e.id);

            if (o == nullptr)
                add (std::to_string (e.bar) + "小節目 拍子 " + sig (e) + " を追加", e.bar, e.id);
            else if (! (*o == e))
                add (std::to_string (e.bar) + "小節目 拍子を変更（" + sig (*o) + " → " + sig (e) + "）", e.bar, e.id);
        }

        for (auto& o : before.meterTrack.events)
            if (findById (after.meterTrack.events, o.id) == nullptr)
                add (std::to_string (o.bar) + "小節目 拍子 " + sig (o) + " を削除", o.bar, o.id);
    }

    // コード
    {
        int added = 0, removed = 0, changed = 0;
        Range r;
        std::string example;

        for (auto& e : after.chordTrack.events)
        {
            auto* o = findById (before.chordTrack.events, e.id);

            if (o == nullptr)             { ++added;   r.add (e.tick, e.tick + 1); if (example.empty()) example = chordText (e); }
            else if (! (*o == e))
            {
                ++changed;
                r.add (e.tick, e.tick + 1);
                r.add (o->tick, o->tick + 1);
                if (example.empty()) example = chordText (*o) + " → " + chordText (e);
            }
        }

        for (auto& o : before.chordTrack.events)
            if (findById (after.chordTrack.events, o.id) == nullptr)
            {
                ++removed;
                r.add (o.tick, o.tick + 1);
            }

        if (const int total = added + removed + changed; total > 0)
        {
            std::string s = barRange (mapB, r.from, r.to) + " コード変更 " + std::to_string (total) + "件";
            if (! example.empty() && total == 1)
                s += "（" + example + "）";

            diff.changes.push_back ({ after.chordTrack.id, ScopeKind::chord, "コード", Change::Category::chords, s, r.from, r.to, "chordEvents", {} });
        }

        const auto& pa = before.chordTrack.playback;
        const auto& pb = after.chordTrack.playback;

        if (pa.enabled != pb.enabled)
            diff.changes.push_back ({ after.chordTrack.id, ScopeKind::chord, "コード", Change::Category::chords,
                                      pb.enabled ? "コードの発音をオン" : "コードの発音をオフ", -1, -1, "chordPlayback", {} });

        if (std::abs (pa.volumeDb - pb.volumeDb) > 1e-9)
            diff.changes.push_back ({ after.chordTrack.id, ScopeKind::chord, "コード", Change::Category::chords,
                                      "コードの音量を変更（" + fmt (pa.volumeDb) + " → " + fmt (pb.volumeDb) + " dB）", -1, -1, "chordPlayback", {} });

        if (! (pa.instrument == pb.instrument))
            diff.changes.push_back ({ after.chordTrack.id, ScopeKind::chord, "コード", Change::Category::chords, "コードの音源を変更", -1, -1, "chordPlayback", {} });
    }

    // マーカー
    {
        auto add = [&] (std::string s, Tick t, const std::string& id)
        {
            diff.changes.push_back ({ after.markerTrack.id, ScopeKind::marker, "マーカー", Change::Category::markers, std::move (s), t, t + 1, "markerEvent", id });
        };

        auto label = [] (const Marker& m) { return m.name.empty() ? std::string ("マーカー") : "マーカー「" + m.name + "」"; };

        for (auto& e : after.markerTrack.events)
        {
            auto* o = findById (before.markerTrack.events, e.id);

            if (o == nullptr)
                add (barRange (mapB, e.tick, e.tick + 1) + " " + label (e) + "を追加", e.tick, e.id);
            else if (! (*o == e))
                add (barRange (mapB, e.tick, e.tick + 1) + " " + label (e) + "を変更"
                       + (o->tick != e.tick ? "（位置を移動）" : o->name != e.name ? "（名前: " + o->name + " → " + e.name + "）" : ""), e.tick, e.id);
        }

        for (auto& o : before.markerTrack.events)
            if (findById (after.markerTrack.events, o.id) == nullptr)
                add (barRange (mapA, o.tick, o.tick + 1) + " " + label (o) + "を削除", o.tick, o.id);
    }

    // キー
    {
        auto name = [] (const KeyEvent& e) { return chord::keyName ({ e.tonic, e.minor }); };
        auto add = [&] (std::string s, int bar, const std::string& id)
        {
            const auto t = mapB.barToTick (bar);
            diff.changes.push_back ({ after.keyTrack.id, ScopeKind::key, "キー", Change::Category::keys, std::move (s), t, t + 1, "keyEvent", id });
        };

        for (auto& e : after.keyTrack.events)
        {
            auto* o = findById (before.keyTrack.events, e.id);

            if (o == nullptr)
                add (std::to_string (e.bar) + "小節 キーを " + name (e) + " に設定", e.bar, e.id);
            else if (! (*o == e))
                add (std::to_string (e.bar) + "小節 キーを変更（" + name (*o) + " → " + name (e) + "）", e.bar, e.id);
        }

        for (auto& o : before.keyTrack.events)
            if (findById (after.keyTrack.events, o.id) == nullptr)
                add (std::to_string (o.bar) + "小節 キー " + name (o) + " を削除", o.bar, o.id);
    }

    // マスター（リミッター）
    {
        const auto& la = before.master.limiter;
        const auto& lb = after.master.limiter;

        if (! (la == lb))
        {
            std::string s;

            if (la.enabled != lb.enabled)
                s = lb.enabled ? "マスターのリミッターをオン" : "マスターのリミッターをオフ";
            else
                s = "マスターのリミッターを変更";

            diff.changes.push_back ({ after.master.id, ScopeKind::master, "マスター", Change::Category::master, s, -1, -1, "master", {} });
        }
    }

    // トラック（ヘッドの並び順、削除されたものは最後）
    for (auto& tb : after.tracks)
        diffTrack (before.findTrack (tb.id), &tb, mapA, mapB, diff);

    for (auto& ta : before.tracks)
        if (after.findTrack (ta.id) == nullptr)
            diffTrack (&ta, nullptr, mapA, mapB, diff);

    // 変更のあったスコープ
    std::vector<std::string> order = allScopeIds (after);
    for (auto& ta : before.tracks)
        if (after.findTrack (ta.id) == nullptr)
            order.push_back (ta.id);

    for (auto& id : order)
        if (! scopeEquals (before, after, id))
            diff.changedScopeIds.push_back (id);

    return diff;
}

//==============================================================================
//==============================================================================
namespace
{
    std::string scopeLabel (const Project& p, const std::string& id, ScopeKind& kind)
    {
        if (id == p.tempoTrack.id)  { kind = ScopeKind::tempo;  return "テンポ"; }
        if (id == p.meterTrack.id)  { kind = ScopeKind::meter;  return "拍子"; }
        if (id == p.chordTrack.id)  { kind = ScopeKind::chord;  return "コード"; }
        if (id == p.markerTrack.id) { kind = ScopeKind::marker; return "マーカー"; }
        if (id == p.keyTrack.id)    { kind = ScopeKind::key;    return "キー"; }
        if (id == p.master.id)      { kind = ScopeKind::master; return "マスター"; }

        kind = ScopeKind::track;
        auto* t = p.findTrack (id);
        return t != nullptr ? t->name : std::string();
    }

    bool isSpecialScope (const Project& p, const std::string& id)
    {
        return id == p.tempoTrack.id || id == p.meterTrack.id || id == p.chordTrack.id
            || id == p.markerTrack.id || id == p.keyTrack.id || id == p.master.id;
    }

    /** トラックの別のコピー（ID を振り直す。「両方残す」用）。 */
    Track copyWithNewIds (const Track& t, const std::string& suffix)
    {
        Track copy = t;
        copy.id = generateUuid();
        copy.name = t.name + suffix;

        for (auto& c : copy.midiClips)
        {
            c.id = generateUuid();
            for (auto& n : c.notes)
                n.id = generateUuid();
        }

        for (auto& c : copy.audioClips)
            c.id = generateUuid();

        return copy;   // エフェクト・センドの ID はトラックごとなので、そのままでよい
    }
}

std::vector<ScopeSyncState> syncStates (const Project& base, const Project& local, const Project* head)
{
    std::vector<ScopeSyncState> states;
    std::vector<std::string> ids = allScopeIds (local);

    if (head != nullptr)
        for (auto& id : allScopeIds (*head))
            if (std::find (ids.begin(), ids.end(), id) == ids.end())
                ids.push_back (id);

    // ベースにだけある（両方で削除された）トラックは出さない
    for (auto& id : ids)
    {
        ScopeSyncState st;
        st.id = id;
        st.inLocal = isSpecialScope (local, id) || local.findTrack (id) != nullptr;
        st.inHead = head != nullptr && (isSpecialScope (*head, id) || head->findTrack (id) != nullptr);

        const auto& named = st.inLocal ? local : (head != nullptr ? *head : base);
        st.name = scopeLabel (named, id, st.kind);

        st.localOnly = isLocalOnlyTrack (base, local, id);
        st.mine = ! st.localOnly && ! scopeEquals (base, local, id);
        st.theirs = head != nullptr && ! scopeEquals (base, *head, id);
        st.conflict = st.mine && st.theirs && ! scopeEquals (local, *head, id);
        states.push_back (st);
    }

    return states;
}

bool isLocalOnlyTrack (const Project& base, const Project& local, const std::string& trackId)
{
    auto* t = local.findTrack (trackId);
    return t != nullptr && usesExternalPlugin (*t) && base.findTrack (trackId) == nullptr;
}

Project withoutLocalOnlyTracks (const Project& local)
{
    Project result = local;
    std::erase_if (result.tracks, [] (const Track& t) { return usesExternalPlugin (t); });
    return result;
}

Project uploadSnapshot (const Project& base, const Project& local, const std::set<std::string>& scopeIds)
{
    std::set<std::string> ids;

    for (auto& id : scopeIds)
        if (! isLocalOnlyTrack (base, local, id))
            ids.insert (id);

    return replaceScopes (base, local, ids);
}

Project replaceScopes (const Project& from, const Project& source, const std::set<std::string>& scopeIds)
{
    Project result = from;

    for (auto& id : scopeIds)
    {
        if (id == source.tempoTrack.id)  { result.tempoTrack = source.tempoTrack;   continue; }
        if (id == source.meterTrack.id)  { result.meterTrack = source.meterTrack;   continue; }
        if (id == source.chordTrack.id)  { result.chordTrack = source.chordTrack;   continue; }
        if (id == source.markerTrack.id) { result.markerTrack = source.markerTrack; continue; }
        if (id == source.keyTrack.id)    { result.keyTrack = source.keyTrack;       continue; }
        if (id == source.master.id)      { result.master = source.master;           continue; }

        if (auto* st = source.findTrack (id))
        {
            if (auto* rt = result.findTrack (id))
            {
                *rt = *st;
            }
            else
            {
                // source で直前にあるトラックの後ろに入れる
                const int si = source.indexOfTrack (id);
                int insertAt = (int) result.tracks.size();

                for (int j = si - 1; j >= 0; --j)
                    if (int ri = result.indexOfTrack (source.tracks[(size_t) j].id); ri >= 0)
                    {
                        insertAt = ri + 1;
                        break;
                    }

                result.tracks.insert (result.tracks.begin() + insertAt, *st);
            }
        }
        else
        {
            result.tracks.erase (std::remove_if (result.tracks.begin(), result.tracks.end(), [&] (const Track& t) { return t.id == id; }),
                                 result.tracks.end());
        }
    }

    return result;
}

Project resolvePull (const Project& base, const Project& local, const Project& head, const std::map<std::string, Resolution>& choices)
{
    std::set<std::string> takeLocal;
    std::vector<const Track*> keepBoth;

    for (auto& st : syncStates (base, local, &head))
    {
        // この PC だけのトラックは、ダウンロードしてもそのまま残す
        if (st.localOnly)
        {
            takeLocal.insert (st.id);
            continue;
        }

        auto it = choices.find (st.id);
        Resolution r = st.mine && ! st.theirs ? Resolution::mine : Resolution::theirs;

        if (it != choices.end())
            r = it->second;

        if (r == Resolution::both && st.kind != ScopeKind::track)
            r = Resolution::mine;   // テンポなどは 1 つしか持てない

        if (r == Resolution::mine)
            takeLocal.insert (st.id);
        else if (r == Resolution::both)
            if (auto* t = local.findTrack (st.id))
                keepBoth.push_back (t);
    }

    Project merged = replaceScopes (head, local, takeLocal);

    for (auto* t : keepBoth)
    {
        // サーバーの版のすぐ後ろに、自分の版を別のトラックとして置く
        auto copy = copyWithNewIds (*t, "（自分の版）");
        const int at = merged.indexOfTrack (t->id);
        merged.tracks.insert (at >= 0 ? merged.tracks.begin() + at + 1 : merged.tracks.end(), copy);
    }

    return merged;
}


//==============================================================================
namespace
{
    /** target の id の要素を source と同じにする（source になければ消す、target になければ足す）。 */
    template <typename T>
    void copyItem (std::vector<T>& target, const std::vector<T>& source, const std::string& id)
    {
        const auto inSource = std::find_if (source.begin(), source.end(), [&] (auto& x) { return x.id == id; });
        const auto inTarget = std::find_if (target.begin(), target.end(), [&] (auto& x) { return x.id == id; });

        if (inSource == source.end())
        {
            if (inTarget != target.end())
                target.erase (inTarget);
        }
        else if (inTarget != target.end())
        {
            *inTarget = *inSource;
        }
        else
        {
            target.push_back (*inSource);
        }
    }
}

Project applyChangeFrom (const Project& targetIn, const Project& source, const Change& change)
{
    auto target = targetIn;
    const auto& part = change.part;

    // トラック以外（テンポ・拍子・コード・マーカー・キー・マスター）
    if (part == "tempoEvent")   { copyItem (target.tempoTrack.events, source.tempoTrack.events, change.itemId);   return target; }
    if (part == "meterEvent")   { copyItem (target.meterTrack.events, source.meterTrack.events, change.itemId);   return target; }
    if (part == "markerEvent")  { copyItem (target.markerTrack.events, source.markerTrack.events, change.itemId); return target; }
    if (part == "keyEvent")     { copyItem (target.keyTrack.events, source.keyTrack.events, change.itemId);       return target; }
    if (part == "chordEvents")  { target.chordTrack.events = source.chordTrack.events;                           return target; }
    if (part == "chordPlayback"){ target.chordTrack.playback = source.chordTrack.playback;                       return target; }
    if (part == "master")       { target.master = source.master;                                                 return target; }

    if (change.scopeKind != ScopeKind::track)
        return replaceScopes (target, source, { change.scopeId });

    auto* t = target.findTrack (change.scopeId);
    auto* s = source.findTrack (change.scopeId);

    // トラックそのものの追加・削除・種類の変更など: トラックをまるごと source の版に
    if (part == "trackAdded" || part == "trackRemoved" || part == "scope" || part == "other" || t == nullptr || s == nullptr)
        return replaceScopes (target, source, { change.scopeId });

    if      (part == "name")       t->name = s->name;
    else if (part == "color")      t->color = s->color;
    else if (part == "volume")     t->volumeDb = s->volumeDb;
    else if (part == "pan")        t->pan = s->pan;
    else if (part == "mute")       t->mute = s->mute;
    else if (part == "solo")       t->solo = s->solo;
    else if (part == "instrument") t->instrument = s->instrument;
    else if (part == "effects")    t->effects = s->effects;
    else if (part == "output")     t->output = s->output;
    else if (part == "sends")      t->sends = s->sends;
    else if (part == "eq")         t->strip.eq = s->strip.eq;
    else if (part == "comp")       t->strip.comp = s->strip.comp;
    else if (part == "compFirst")  t->strip.compFirst = s->strip.compFirst;
    else if (part == "io")         { t->inputChannels = s->inputChannels; t->outputChannels = s->outputChannels; }
    else if (part == "crossfade")  { t->crossfadeMs = s->crossfadeMs; t->crossfadeShape = s->crossfadeShape; }
    else if (part == "render")     t->render = s->render;
    else if (part == "midiClip")   copyItem (t->midiClips, s->midiClips, change.itemId);
    else if (part == "audioClip")  copyItem (t->audioClips, s->audioClips, change.itemId);
    else if (part == "midiClipRange")
    {
        // 位置・長さだけ（中のノートは「ノート変更」の方で）
        auto* ct = t->findMidiClip (change.itemId);
        auto* cs = s->findMidiClip (change.itemId);

        if (ct != nullptr && cs != nullptr)
        {
            ct->startTick = cs->startTick;
            ct->lengthTick = cs->lengthTick;
        }
    }
    else if (part == "notes")
    {
        // 両方にあるクリップのノート（クリップの追加・削除は別の変更）
        for (auto& ct : t->midiClips)
            if (auto* cs = s->findMidiClip (ct.id))
                ct.notes = cs->notes;
    }
    else
    {
        return replaceScopes (target, source, { change.scopeId });
    }

    return target;
}

} // namespace collab
