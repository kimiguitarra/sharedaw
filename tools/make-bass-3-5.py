#!/usr/bin/env python3
"""
内蔵ベース 3.5.0 のウッドベースを、3.2.0 のサンプルから作る（打ち込みやすさ優先）。

- 余計な倍音を減らす: 基音から数えて 4 倍音くらいまでを残し、それより上をなだらかに切る（指の当たる音・胴の複雑な鳴りが減る）
- 音価いっぱい伸ばせるようにする: 弾いた直後（0.25 秒）より後は自然な減衰を打ち消して大きさをそろえ、
  落ち着いた所を継ぎ目なくループする（loop_continuous）。伸ばしている所の大きさはどの音もそろえる
- 強さは 3 段（3.2.0 の vl2・vl3・vl4 の 1 本目）。ラウンドロビンはやめて、いつも同じ音にする

ジャズベース（指弾き・ピック）は 3.2.0 のサンプルをそのまま使う（SFZ のパスで ../3.2.0/ を指す）。

使い方: python3 tools/make-bass-3-5.py assets/instruments/builtin.bass
"""

import os
import re
import sys

import numpy as np
import soundfile as sf
from scipy import signal

ATTACK_SECONDS = 0.25      # ここまでは元の音のまま（弾いた感じ）
LOOP_MIN_SECONDS = 0.45
SUSTAIN_DBFS = -24.0       # 伸ばしている所の大きさ（RMS）
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


def best_loop (x, sr, period):
    """
    ループの頭と終わり。頭は 0.3〜1.6 秒、長さは周期の倍数の近くで 0.15〜0.6 秒。
    継ぎ目で形がそろうこと（相関）と、ループの中で大きさが揺れない（うなりがない）ことの両方で選ぶ。
    """
    w = int (max (period * 4, 0.03 * sr))
    best = (None, None, -1e9, 0.0)

    for ls in np.linspace (0.3 * sr, 1.6 * sr, 40).astype (int):
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


def process (x, sr, f0):
    # 余計な倍音を切る（基音の 4 倍、180〜900 Hz。位相がずれないように前後から）
    fc = float (np.clip (4.0 * f0, 180.0, 900.0))
    sos = signal.butter (2, fc, fs = sr, output = "sos")
    y = signal.sosfiltfilt (sos, x)

    # 弾いた直後より後は、減衰を打ち消して大きさをそろえる（最大 +24 dB まで）
    env = envelope (y, sr)
    t0 = int (ATTACK_SECONDS * sr)
    gain = np.ones_like (y)
    gain[t0:] = np.minimum (env[t0] / env[t0:], 10 ** (24 / 20))
    y = y * gain

    # 継ぎ目のないループ
    period = sr / f0
    ls, le, score = best_loop (y, sr, period)
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

    return y, ls, le - 1, score, fc


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
    dst = os.path.join (root, "3.5.0")
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
                y, ls, le, corr, fc = process (x, sr, f0)
                wob = wobble_db (y, ls, le, sr / f0)
                quality = corr - 0.15 * wob

                if best is None or quality > best[0]:
                    best = (quality, name, cents, f0, y, ls, le, corr, wob, fc, sr)

                if corr > 0.9 and wob < 3.0 and name == candidates[0]:
                    break

            _, name, cents, f0, y, ls, le, corr, wob, fc, sr = best
            out = f"{key}_{layer}.flac"
            sf.write (os.path.join (dst, "upright", out), y, sr, subtype = "PCM_24")
            print (f"{out} (from {name}): f0 {f0:.1f} Hz, cut {fc:.0f} Hz, loop {ls}-{le} ({(le - ls) / sr:.2f} s), match {corr:.3f}, wobble {wob:.1f} dB")

            regions.append (f"<region> sample=upright/{out} pitch_keycenter={key} tune={cents} lokey={lokey} hikey={hikey} "
                            f"lovel={lovel} hivel={hivel} loop_mode=loop_continuous loop_start={ls} loop_end={le}")

    with open (os.path.join (dst, "upright.sfz"), "w", encoding = "utf-8") as f:
        f.write ("// ウッドベース（打ち込み向け）: Meatbass（Karoryfer Samples、CC0）のピチカートから、余計な倍音を減らし、\n")
        f.write ("// 音価いっぱい伸ばせるようにループしたもの（tools/make-bass-3-5.py）。押さえている間は同じ大きさで鳴り、離すと止まる\n")
        f.write ("<group> note_polyphony=1 group=1 off_by=1 amp_veltrack=70 ampeg_attack=0.001 "
                 "ampeg_release=0.18 eq2_freq=110 eq2_bw=1.2 eq2_gain=2\n")
        f.write ("\n".join (regions) + "\n")

    # ジャズベースは 3.2.0 のサンプルをそのまま使う
    for name in ("jazz-finger.sfz", "jazz-pick.sfz"):
        text = open (os.path.join (src, name), encoding = "utf-8").read()
        text = text.replace ("sample=jazz/", "sample=../3.2.0/jazz/")
        open (os.path.join (dst, name), "w", encoding = "utf-8").write (text)


if __name__ == "__main__":
    main()
