#!/usr/bin/env python3
"""
内蔵ベース 3.9.0 のウッドベースを、3.2.0 のサンプルから作る（打ち込みやすさ優先）。

3.8.0 との違い: 弦が指板に当たるビビり（1 kHz より上の細かい倍音）が残っていて、オケに混ぜると歪んだように聞こえた。
基音の 9 倍（700 Hz〜1.2 kHz）より上を急に切り（2 次を前後から）、ビビりを取る。輪郭になる 2〜8 倍音（〜700 Hz）は残す。
特に D♭2・E♭2 あたり（36・39 のサンプル）は、一部の倍音だけが飛び出して鳴り続け（C2 の 5 倍音は基音の -9 dB。となりの音は -32 dB）、
ビビったように聞こえる。なめらかな倍音の並びより大きい倍音だけをサンプルごとに下げる。

3.7.0 との違い: 400 Hz より上をほとんど切っていたので、こもって（もごもご）輪郭が見えなかった。
倍音は 2.5 kHz あたりまで残して（それより上の指の雑音・ザラつきだけをなだらかに切る）、弦の輪郭が見えるようにし、
ボワつく所（250 Hz あたり）を少し下げる。低音（基音）の太さは SFZ の EQ で 70 Hz あたりを少し持ち上げる。
弾いた瞬間の角（3.7.0）は、倍音を残したのでそのまま出る。

3.5.0 との違い: 弾いた直後に一度小さくなってから大きさが戻る（尻上がりに聞こえる）ことがあったので、
弾いた後は 1.2 秒かけて 4 dB だけなだらかに小さくなり、その後は同じ大きさで伸びるようにした。
弾いた瞬間より後で大きさが上がる所は、なくなるまで下げる（大きさは下がる一方）。ループはなだらかに小さくなり終わった所から選ぶ。

- 余計な倍音を減らす: 基音から数えて 4 倍音くらいまでを残し、それより上をなだらかに切る（指の当たる音・胴の複雑な鳴りが減る）
- 音価いっぱい伸ばせるようにする: 弾いた直後（0.25 秒）より後は自然な減衰を打ち消し、なだらかな減衰（4 dB）に置き換えて、
  落ち着いた所を継ぎ目なくループする（loop_continuous）。伸ばしている所の大きさはどの音もそろえる
- 強さは 3 段（3.2.0 の vl2・vl3・vl4 の 1 本目）。ラウンドロビンはやめて、いつも同じ音にする

ジャズベース（指弾き・ピック）は 3.2.0 のサンプルをそのまま使う（SFZ のパスで ../3.2.0/ を指す）。

使い方: python3 tools/make-bass-3-9.py assets/instruments/builtin.bass
"""

import os
import re
import sys

import numpy as np
import soundfile as sf
from scipy import signal

ATTACK_SECONDS = 0.25      # ここまでは元の音のまま（弾いた感じ）
DECAY_END_SECONDS = 1.2    # ここまでになだらかに小さくなり、その後は同じ大きさ
DECAY_DB = 4.0
LOOP_MIN_SECONDS = 0.45
SUSTAIN_DBFS = -24.0       # 伸ばしている所の大きさ（RMS）
MUD_HZ, MUD_Q, MUD_DB = 250.0, 1.0, -3.0   # ボワつく所を下げる
LAYERS = [("vl2", 0, 70), ("vl3", 71, 105), ("vl4", 106, 127)]


def hz (midi):
    return 440.0 * 2 ** ((midi - 69) / 12)


def envelope (x, sr):
    """なめらかな大きさ（ヒルベルト変換の絶対値を 8 Hz で平らにしたもの）。"""
    e = np.abs (signal.hilbert (x))
    sos = signal.butter (2, 8.0, fs = sr, output = "sos")
    return np.maximum (signal.sosfiltfilt (sos, e), 1e-6)


def wobble_db (x, start, end, period):
    """ループの中の大きさの揺れ（2 周期ごとの RMS の最大と最小の差、dB）。うなりがあると大きい。"""
    w = max (2, int (period * 2))
    rms = [np.sqrt (np.mean (x[i: i + w] ** 2)) for i in range (start, end - w, max (1, w // 2))]
    return 20 * np.log10 (max (rms) / (min (rms) + 1e-12)) if len (rms) > 1 else 0.0


def best_loop (x, sr, period, first, last):
    """
    ループの頭と終わり。頭は first〜last 秒（なだらかに小さくなり終わった後）、長さは周期の倍数の近くで 0.15〜0.6 秒。
    継ぎ目で形がそろうこと（相関）と、ループの中で大きさが揺れない（うなりがない）ことの両方で選ぶ。
    """
    w = int (max (period * 4, 0.03 * sr))
    best = (None, None, -1e9, 0.0)

    for ls in np.linspace (first * sr, last * sr, 40).astype (int):
        a = x[ls: ls + w]
        na = np.linalg.norm (a) + 1e-12
        kmin = int (np.ceil (0.15 * sr / period))
        kmax = int (np.ceil (0.6 * sr / period))

        for k in range (kmin, kmax + 1, max (1, (kmax - kmin) // 12)):
            centre = int (round (ls + k * period))
            local = (None, -2.0)

            for e in range (centre - int (period / 2), centre + int (period / 2) + 1, max (1, int (period / 40))):
                b = x[e: e + w]

                if len (b) < w:
                    break

                corr = float (np.dot (a, b) / (na * (np.linalg.norm (b) + 1e-12)))

                if corr > local[1]:
                    local = (e, corr)

            if local[0] is None:
                continue

            wob = wobble_db (x, int (ls), local[0], period)
            score = local[1] - 0.15 * wob

            if score > best[2]:
                best = (int (ls), local[0], score, local[1])

    return best[0], best[1], best[3]


def peaking (freq, q, gain_db, sr):
    """RBJ のピーキング EQ（sos）。"""
    a = 10 ** (gain_db / 40)
    w = 2 * np.pi * freq / sr
    alpha = np.sin (w) / (2 * q)
    b = [1 + alpha * a, -2 * np.cos (w), 1 - alpha * a]
    den = [1 + alpha / a, -2 * np.cos (w), 1 - alpha / a]
    return np.array ([[b[0] / den[0], b[1] / den[0], b[2] / den[0], 1.0, den[1] / den[0], den[2] / den[0]]])


def harmonic_levels (x, sr, f0, count):
    """伸ばしている所（0.3〜0.8 秒）の、基音から数えた各倍音の大きさ（基音に対する dB）。"""
    seg = x[int (0.3 * sr): int (0.8 * sr)]
    spec = np.abs (np.fft.rfft (seg * np.hanning (len (seg)))) ** 2
    freqs = np.fft.rfftfreq (len (seg), 1 / sr)
    level = [10 * np.log10 (spec[(freqs > h * f0 * 0.98) & (freqs < h * f0 * 1.02)].sum() + 1e-12) for h in range (1, count + 1)]
    return [l - level[0] for l in level]


def tame_harmonics (y, sr, f0):
    """
    なめらかな倍音の並び（倍音が 1 オクターブ上がるごとに 10 dB 小さくなる）より大きく鳴っている倍音だけを下げる（上げはしない）。
    C2 の 5 倍音（330 Hz あたり。ほかの弦の共鳴）、E♭2 の 3・4 倍音・8 倍音などが目立って、ビビったように聞こえていた。
    """
    cuts = {}

    # 50 Hz より低い音は、基音がもともと小さく（2・3 倍音の方が大きい）この比べ方が使えない。そのままにする
    if f0 < 50.0:
        return y, []

    # 1 回では下がりきらない（倍音の高さが少しずれている）ので、測り直して 3 回まで
    for _ in range (3):
        for h, level in enumerate (harmonic_levels (y, sr, f0, 12), start = 1):
            if h < 3 or h * f0 > 0.45 * sr:
                continue

            allowed = -10.0 * np.log2 (h) + (5.0 if h <= 4 else 3.0)

            if level > allowed + 0.5:
                cut = level - allowed
                cuts[h] = cuts.get (h, 0.0) + cut
                # 前後からかけるので、半分ずつ。幅は倍音の間隔の半分くらい
                y = signal.sosfiltfilt (peaking (h * f0, h * 1.5, -cut / 2.0, sr), y)

    return y, sorted (cuts.items())


def process (x, sr, f0):
    # ビビり（指板に当たる音）・指の雑音を切る（基音の 9 倍、700 Hz〜1.2 kHz より上を急に。位相がずれないように前後から）
    fc = float (np.clip (9.0 * f0, 700.0, 1200.0))
    y = signal.sosfiltfilt (signal.butter (2, fc, fs = sr, output = "sos"), x)
    y, cuts = tame_harmonics (y, sr, f0)

    # ボワつく所（250 Hz あたり）を少し下げる（前後からかけるので、それぞれ半分ずつ）
    y = signal.sosfiltfilt (peaking (MUD_HZ, MUD_Q, MUD_DB / 2.0, sr), y)

    # 弾いた直後より後は、減衰を打ち消して大きさをそろえる（最大 +24 dB まで）
    env = envelope (y, sr)
    t0 = int (ATTACK_SECONDS * sr)
    gain = np.ones_like (y)
    gain[t0:] = np.minimum (env[t0] / env[t0:], 10 ** (24 / 20))
    y = y * gain

    # 元の音が 18 dB 以上小さくなる前に、なだらかな減衰を終えてループする（高い音ほど早く減衰する。小さくなりすぎた所は雑音が目立つ）
    quiet = np.nonzero (env[t0:] < env[t0] * 10 ** (-18 / 20))[0]
    usable = (t0 + quiet[0]) / sr if len (quiet) else len (y) / sr - 0.3
    decay_end = float (np.clip (usable - 0.45, ATTACK_SECONDS + 0.3, DECAY_END_SECONDS))

    # なだらかな減衰に置き換える（弾いた直後から DECAY_END_SECONDS までに DECAY_DB 下がり、その後は同じ）
    t = np.arange (len (y)) / sr
    k = np.clip ((t - ATTACK_SECONDS) / (decay_end - ATTACK_SECONDS), 0.0, 1.0)
    y = y * 10 ** (-DECAY_DB * (1 - (1 - k) ** 2) / 20)

    # 弾いた瞬間より後で大きさが上がる所は下げる（大きさは下がる一方にする。尻上がりにしない）
    env = envelope (y, sr)
    peak = int (np.argmax (env[: int (0.15 * sr)]))
    stop = len (y) - int (0.15 * sr)   # 終わりの近くは大きさの求め方がずれるので、手前の値を使う
    run = np.minimum.accumulate (env[peak: stop])
    run = np.concatenate ([run, np.full (len (y) - stop, run[-1])])
    y[peak:] = y[peak:] * (run / env[peak:])

    # 継ぎ目のないループ
    period = sr / f0
    ls, le, score = best_loop (y, sr, period, decay_end + 0.05, max (decay_end + 0.15, min (1.7, usable - 0.2)))
    fade = int (min ((le - ls) / 2, 0.1 * sr))
    a = np.linspace (0.0, 1.0, fade)
    y[le - fade: le] = y[le - fade: le] * (1 - a) + y[ls - fade: ls] * a

    y = y[: le + 64]

    # 伸ばしている所（ループ）の大きさを、どの音でも同じにする（打ち込んだとき音ごとにばらつかないように）
    rms = np.sqrt (np.mean (y[ls: le] ** 2))
    y = y * (10 ** (SUSTAIN_DBFS / 20) / rms)
    peak = np.max (np.abs (y))

    if peak > 0.95:
        y = y * (0.95 / peak)   # 弾いた瞬間が大きすぎるときだけ下げる

    return y, ls, le - 1, score, fc, cuts


def tunes (sfz_path):
    """3.2.0 の upright.sfz から、サンプルごとの tune（セント）を読む。"""
    result = {}

    for line in open (sfz_path, encoding = "utf-8"):
        m = re.search (r"sample=upright/(\S+)\.flac pitch_keycenter=(\d+) tune=(-?\d+)", line)

        if m:
            result[m.group (1)] = (int (m.group (2)), int (m.group (3)))

    return result


def main ():
    root = sys.argv[1]
    src = os.path.join (root, "3.2.0")
    dst = os.path.join (root, "3.9.0")
    os.makedirs (os.path.join (dst, "upright"), exist_ok = True)

    tune = tunes (os.path.join (src, "upright.sfz"))
    keys = sorted ({ k for k, _ in tune.values() })
    regions = []

    for i, key in enumerate (keys):
        lokey = 0 if i == 0 else key - 1
        hikey = 127 if i == len (keys) - 1 else key + 1

        for layer, lovel, hivel in LAYERS:
            # その強さの 1 本目。うまくループできなければ（うなりなど）、2 本目・となりの強さのサンプルも試して良い方
            candidates = [f"{key}_{layer}_1", f"{key}_{layer}_2"] + [f"{key}_{l}_{n}" for l, _, _ in LAYERS if l != layer for n in (1, 2)]
            best = None

            for name in candidates:
                if name not in tune:
                    continue

                _, cents = tune[name]
                x, sr = sf.read (os.path.join (src, "upright", name + ".flac"))
                x = x.mean (axis = 1) if x.ndim > 1 else x

                # sfz の tune で正しい高さにしているので、サンプルの実際の基音は少しずれている
                f0 = hz (key) * 2 ** (-cents / 1200)
                y, ls, le, corr, fc, cuts = process (x, sr, f0)
                wob = wobble_db (y, ls, le, sr / f0)
                quality = corr - 0.15 * wob

                if best is None or quality > best[0]:
                    best = (quality, name, cents, f0, y, ls, le, corr, wob, fc, sr, cuts)

                if corr > 0.9 and wob < 3.0 and name == candidates[0]:
                    break

            _, name, cents, f0, y, ls, le, corr, wob, fc, sr, cuts = best
            out = f"{key}_{layer}.flac"
            sf.write (os.path.join (dst, "upright", out), y, sr, subtype = "PCM_24")
            print (f"{out} (from {name}): f0 {f0:.1f} Hz, cut {fc:.0f} Hz, loop {ls}-{le} ({(le - ls) / sr:.2f} s), match {corr:.3f}, wobble {wob:.1f} dB, cut " + ", ".join (f"h{h} -{c:.0f}" for h, c in cuts))

            regions.append (f"<region> sample=upright/{out} pitch_keycenter={key} tune={cents} lokey={lokey} hikey={hikey} "
                            f"lovel={lovel} hivel={hivel} loop_mode=loop_continuous loop_start={ls} loop_end={le}")

    with open (os.path.join (dst, "upright.sfz"), "w", encoding = "utf-8") as f:
        f.write ("// ウッドベース（打ち込み向け）: Meatbass（Karoryfer Samples、CC0）のピチカートから、指の雑音・ビビり・ボワつきを減らし、\n")
        f.write ("// 音価いっぱい伸ばせるようにループしたもの（tools/make-bass-3-9.py）。弦の輪郭は残し、弾いた後は少しだけ小さくなり、その後は同じ大きさで鳴る。離すと止まる\n")
        f.write ("<group> note_polyphony=1 group=1 off_by=1 amp_veltrack=70 ampeg_attack=0.001 "
                 "ampeg_release=0.18 eq2_freq=70 eq2_bw=1.2 eq2_gain=2\n")
        f.write ("\n".join (regions) + "\n")

    # ジャズベースは 3.2.0 のサンプルをそのまま使う
    for name in ("jazz-finger.sfz", "jazz-pick.sfz"):
        text = open (os.path.join (src, name), encoding = "utf-8").read()
        text = text.replace ("sample=jazz/", "sample=../3.2.0/jazz/")
        open (os.path.join (dst, name), "w", encoding = "utf-8").write (text)


if __name__ == "__main__":
    main()
