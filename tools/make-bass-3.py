#!/usr/bin/env python3
"""
内蔵ベース 3.0.0 のサンプルを、実際に録音されたベースのサンプル（どちらも CC0）から作る。

- Growlybass（Karoryfer Samples、Squier Jazz Bass、ライン録り）: https://github.com/sfzinstruments/karoryfer.growlybass
- Meatbass（Karoryfer Samples、1958 年製のコントラバスのピチカート）: https://github.com/sfzinstruments/karoryfer.meatbass

元の Growlybass は低い E 弦（41.2 Hz）を MIDI 40 に割り当てている（楽譜の書き方。実際の音より 1 オクターブ上）。
ここでは実際に鳴る高さ（MIDI 28 = E1 = 41.2 Hz）に割り当てる。各サンプルの高さを測って、ずれは tune= で直す。

使い方: python3 tools/make-bass-3.py <growlybass のフォルダ> <meatbass のフォルダ> <出力フォルダ>
"""

import math
import os
import sys

import numpy as np
import soundfile as sf

SR = 44100
NAMES = { "c": 0, "db": 1, "d": 2, "eb": 3, "e": 4, "f": 5, "gb": 6, "g": 7, "ab": 8, "a": 9, "bb": 10, "b": 11 }


def note_number (name):
    """科学的表記（c4 = 60）の音名を MIDI ノート番号に。"""
    letters = name.rstrip ("0123456789")
    return 12 * (int (name[len (letters):]) + 1) + NAMES[letters]


def measure_cents (x, midi):
    """midi の高さからのずれ（セント）。倍音の強さの和が最大になる所を ±60 セントで探す。"""
    a = int (np.argmax (np.abs (x)))
    seg = x[a + int (0.3 * SR): a + int (1.5 * SR)]   # 弾いた直後は高めに揺れるので、落ち着いた所で測る
    seg = (seg - seg.mean()) * np.hanning (len (seg))
    n = 1 << 20
    spec = np.abs (np.fft.rfft (seg, n))

    def mag (f):
        i = int (round (f * n / SR))
        return spec[max (i - 2, 0): i + 3].max()

    best, best_score = 0.0, -1e18
    for c in np.arange (-60, 60.5, 0.5):
        f0 = 440.0 * 2 ** ((midi + c / 100 - 69) / 12)
        score = sum (math.log (mag (f0 * h) + 1e-12) for h in range (1, 7) if f0 * h < 4000)
        if score > best_score:
            best, best_score = c, score
    return best


def trim (x, seconds, fade):
    """頭の無音を詰め、長さを seconds に切って、最後を fade 秒で消す。"""
    peak = np.max (np.abs (x))
    start = max (0, int (np.argmax (np.abs (x) > peak * 0.01)) - int (0.002 * SR))
    x = x[start: start + int (seconds * SR)].copy()
    f = min (len (x), int (fade * SR))
    x[-f:] *= np.linspace (1.0, 0.0, f) ** 2
    return x


def load (path):
    x, sr = sf.read (path, dtype="float64")
    assert sr == SR, path
    return x if x.ndim == 1 else x.mean (axis=1)


def build (out_dir, sub, notes, layers, rrs, source, seconds, fade, sounding_offset):
    """notes: 元の音名。layers: [(元の強さの名前, lovel, hivel)]。戻り値は sfz の region の行。"""
    os.makedirs (os.path.join (out_dir, sub), exist_ok=True)
    regions = []
    roots = [note_number (n) + sounding_offset for n in notes]

    # 音ごとの音量のばらつきを揃える（強さの段の差はそのまま残す）: 一番強い段の RMS を基準にする
    gains = {}
    for n in notes:
        loud = load (source (n, layers[-1][0], rrs[0]))
        head = loud[int (np.argmax (np.abs (loud))): ][: int (0.5 * SR)]
        gains[n] = 10 ** (-14.0 / 20) / (np.sqrt (np.mean (head ** 2)) + 1e-12)

    all_peaks = []
    rendered = []
    for i, n in enumerate (notes):
        root = roots[i]
        lo = 0 if i == 0 else (roots[i - 1] + root) // 2 + 1
        hi = 127 if i == len (notes) - 1 else (root + roots[i + 1]) // 2
        for layer, lovel, hivel in layers:
            for k, rr in enumerate (rrs):
                x = trim (load (source (n, layer, rr)), seconds, fade) * gains[n]
                cents = measure_cents (x, root)
                name = f"{sub}/{root}_{layer}_{k + 1}.flac"
                rendered.append ((name, x))
                all_peaks.append (np.max (np.abs (x)))
                regions.append (f"<region> sample={name} pitch_keycenter={root} tune={int (round (-cents))} "
                                f"lokey={lo} hikey={hi} lovel={lovel} hivel={hivel} seq_position={k + 1}")
                print (f"  {name}: {cents:+.1f} cent")

    # 全体でピークが -1 dBFS を超えないように
    scale = min (1.0, 10 ** (-1.0 / 20) / max (all_peaks))
    for name, x in rendered:
        sf.write (os.path.join (out_dir, name), x * scale, SR, subtype="PCM_16", format="FLAC")
    return regions


def main():
    growly, meat, out = sys.argv[1:4]

    # ジャズベース: 3 半音ごと（db2 は低い E 弦を C# に下げたもの）。強さ 4 段、ラウンドロビン 2
    jazz_notes = [ "db2", "e2", "gb2", "a2", "c3", "eb3", "gb3", "a3", "c4", "eb4", "gb4", "a4", "c5", "eb5" ]
    jazz_layers = [ ("pp", 0, 45), ("p", 46, 80), ("f", 81, 110), ("ff", 111, 127) ]
    jazz = build (out, "jazz", jazz_notes, jazz_layers, [ 1, 2 ],
                  lambda n, l, r: os.path.join (growly, "sustain", f"{n}_{l}_rr{r}.wav"),
                  seconds=4.0, fade=0.4, sounding_offset=-12)

    # ダブルベース（ピチカート）: 名前は実際の高さ。強さ 4 段、ラウンドロビン 2
    upright_notes = [ "a0", "c1", "eb1", "gb1", "a1", "c2", "eb2", "gb2", "a2", "c3", "eb3", "gb3", "a3", "c4" ]
    upright_layers = [ ("vl1", 0, 45), ("vl2", 46, 80), ("vl3", 81, 110), ("vl4", 111, 127) ]
    upright = build (out, "upright", upright_notes, upright_layers, [ 1, 2 ],
                     lambda n, l, r: os.path.join (meat, "Samples", "pizz", f"{n}_{l}_rr{r}.wav"),
                     seconds=2.6, fade=0.5, sounding_offset=0)

    common = "note_polyphony=1 group=1 off_by=1 seq_length=2 amp_veltrack=70 ampeg_attack=0.001"

    def write (file, comment, header, regions):
        with open (os.path.join (out, file), "w", encoding="utf-8") as f:
            f.write (f"// {comment}\n<group> {header}\n")
            f.write ("\n".join (regions) + "\n")

    write ("jazz-finger.sfz", "ジャズベース（指弾き）: Growlybass（Karoryfer Samples、CC0）の Squier Jazz Bass",
           f"{common} ampeg_release=0.07", jazz)
    write ("jazz-pick.sfz", "ジャズベース（明るめ・ピック風）: 同じサンプルの高域を上げ、ローを少し締めたもの",
           f"{common} ampeg_release=0.05 eq1_freq=2500 eq1_bw=1.5 eq1_gain=6 eq2_freq=80 eq2_bw=1 eq2_gain=-3", jazz)
    write ("upright.sfz", "ダブルベース（ピチカート）: Meatbass（Karoryfer Samples、CC0）",
           f"{common} ampeg_release=0.15", upright)


if __name__ == "__main__":
    main()
