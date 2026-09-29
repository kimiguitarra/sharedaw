#!/usr/bin/env python3
"""
内蔵ベース 3.2.0 のサンプルを 3.0.0 のサンプルから作る。

- 弾いた瞬間の音程の揺れを取る: 強く弾いた弦は最初だけ高く鳴り（低い弦で最大 50 セントほど）、だんだん下がる。
  音程を 5 ms ごとに測り、いつも同じ高さ（pitch_keycenter の音）で鳴るように時間軸を伸び縮みさせて読み直す。
- アタックを和らげる: 弾いた直後の 0.3 秒の音量が、落ち着いた所（0.25〜0.5 秒）より 3 dB 以上大きい分を半分にする。
  最初の 60 ms は硬い高域を丸め、頭の 3 ms はフェードインにして、指やピックが当たる「カチッ」を弱める。

使い方: python3 tools/make-bass-3-2.py <3.1.0 のフォルダ> <出力フォルダ>
"""

import math
import os
import re
import sys

import numpy as np
import soundfile as sf
from scipy import signal

SR = 44100


def hz (midi):
    return 440.0 * 2 ** ((midi - 69) / 12)


def track_pitch (x, f_nom, hop):
    """hop 秒ごとの音程（Hz）と確からしさ。自己相関が最大になる周期を、前の値の近くで探す。"""
    T = SR / f_nom
    W = int (3 * T)
    lo, hi = int (T / 2 ** (3 / 12)), int (T * 2 ** (3 / 12)) + 1
    step = int (hop * SR)
    times, freqs, confs = [], [], []
    prev = None

    for s in range (0, len (x) - W - hi - 2, step):
        a = x[s: s + W]
        ea = np.dot (a, a)
        if ea < 1e-12:
            break
        l0, l1 = (lo, hi) if prev is None else (max (lo, int (prev * 0.98)), min (hi, int (prev * 1.02) + 1))
        lags = np.arange (l0, l1 + 1)
        cs = np.empty (len (lags))
        for k, L in enumerate (lags):
            b = x[s + L: s + L + W]
            cs[k] = np.dot (a, b) / math.sqrt (ea * np.dot (b, b) + 1e-20)
        i = int (np.argmax (cs))
        d = 0.0
        if 0 < i < len (cs) - 1:
            den = cs[i - 1] - 2 * cs[i] + cs[i + 1]
            d = 0.5 * (cs[i - 1] - cs[i + 1]) / den if den < 0 else 0.0
        L = lags[i] + d
        prev = L
        times.append ((s + W / 2) / SR)
        freqs.append (SR / L)
        confs.append (cs[i])

    return np.array (times), np.array (freqs), np.array (confs)


def flatten_pitch (x, steady):
    """弾いた直後に高くなって、だんだん steady Hz に落ち着く分を取る。戻り値は (新しい音, 頭で直したセント)。

    測った音程（セント）に「落ち着いた高さ + A·exp(-t/τ)」をあてはめ、A·exp(-t/τ) の分だけ時間軸を伸ばして読み直す。
    落ち着いた後は何も変えないので、tune= はそのまま使える。測りそこねた所にだまされないよう、形を決めてあてはめる。
    """
    t, f, c = track_pitch (x, steady, 0.005)
    use = (t < 1.5) & (c > 0.6)
    t, cents = t[use], 1200 * np.log2 (f[use] / steady)
    if len (t) < 20:
        return x, 0.0

    best = None
    for tau in np.arange (0.03, 0.8, 0.01):
        e = np.exp (-t / tau)
        M = np.stack ([np.ones_like (e), e], axis=1)
        (ref, A), *_ = np.linalg.lstsq (M, cents, rcond=None)
        err = np.mean (np.abs (cents - ref - A * e))
        if best is None or err < best[0]:
            best = (err, A, tau)

    _, A, tau = best
    A = float (np.clip (A, 0.0, 80.0))   # 下がる方向の揺れや、ありえない大きさは直さない
    if A < 3.0:
        return x, 0.0

    n = len (x)
    pos = np.arange (n) / SR
    ratio = 2 ** (-(A * np.exp (-pos / tau)) / 1200)
    src = np.concatenate (([0.0], np.cumsum (ratio[:-1])))
    src = src[src < n - 3]

    # 4 点の 3 次補間で読み直す
    i = np.floor (src).astype (int)
    u = src - i
    xm1 = x[np.maximum (i - 1, 0)]; x0 = x[i]; x1 = x[i + 1]; x2 = x[i + 2]
    c1 = 0.5 * (x1 - xm1)
    c2 = xm1 - 2.5 * x0 + 2 * x1 - 0.5 * x2
    c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1)
    return ((c3 * u + c2) * u + c1) * u + x0, A


def soften_attack (x):
    """弾いた直後の大きすぎる分を半分に（dB で）。頭 3 ms はフェードイン。"""
    w = int (0.02 * SR)
    env = np.sqrt (np.convolve (x ** 2, np.ones (w) / w, mode="same")) + 1e-9
    body = np.sqrt (np.mean (x[int (0.25 * SR): int (0.5 * SR)] ** 2)) + 1e-9
    excess = 20 * np.log10 (env / body) - 3.0
    reduce_db = -0.5 * np.maximum (excess, 0.0)

    # 0.3 秒以降は何もしない（0.2〜0.35 秒でなめらかに戻す）
    pos = np.arange (len (x)) / SR
    fade_out = np.clip ((0.35 - pos) / 0.15, 0.0, 1.0)
    gain_db = reduce_db * fade_out

    # 急な変化で音が歪まないように、ゲインを 10 ms でならす
    s = int (0.01 * SR)
    gain_db = np.convolve (np.pad (gain_db, s, mode="edge"), np.ones (2 * s + 1) / (2 * s + 1), mode="same")[s:-s]
    y = x * 10 ** (gain_db / 20)

    # 弾いた瞬間の硬い高域（指やピックが弦に当たる音）を、最初の 60 ms だけ丸める（位相がずれないフィルター）
    lp = signal.filtfilt (*signal.butter (2, 1500 / (SR / 2)), y)
    w = np.clip (pos / 0.06, 0.0, 1.0) ** 2
    y = lp * (1 - w) + y * w

    f = int (0.003 * SR)
    y[:f] *= np.sin (np.linspace (0, math.pi / 2, f)) ** 2
    return y, float (-gain_db.min())


def measure_cents (x, midi):
    """midi の高さからのずれ（セント）。make-bass-3.py と同じ測り方（0.3〜1.5 秒）。"""
    a = int (np.argmax (np.abs (x)))
    seg = x[a + int (0.3 * SR): a + int (1.5 * SR)]
    seg = (seg - seg.mean()) * np.hanning (len (seg))
    n = 1 << 20
    spec = np.abs (np.fft.rfft (seg, n))

    def mag (f):
        i = int (round (f * n / SR))
        return spec[max (i - 2, 0): i + 3].max()

    best, best_score = 0.0, -1e18
    for c in np.arange (-60, 60.5, 0.5):
        f0 = hz (midi + c / 100)
        score = sum (math.log (mag (f0 * h) + 1e-12) for h in range (1, 7) if f0 * h < 4000)
        if score > best_score:
            best, best_score = c, score
    return best


def main():
    src_dir, out = sys.argv[1:3]   # src_dir は 3.1.0（sfz の設定はそのまま使い、サンプルは ../3.0.0/ から読む）
    region = re.compile (r"sample=(\S+) pitch_keycenter=(\d+) tune=(-?\d+)")
    done = {}

    for sfz in ("jazz-finger.sfz", "jazz-pick.sfz", "upright.sfz"):
        lines = open (os.path.join (src_dir, sfz), encoding="utf-8").read().splitlines()
        for i, line in enumerate (lines):
            m = region.search (line)
            if not m:
                if line.startswith ("// サンプルは"):
                    lines[i] = "// サンプルは 3.0.0 のものから、弾いた瞬間の音程の揺れを取り、アタックを和らげたもの（tools/make-bass-3-2.py）"
                continue
            name, key, tune = m.group (1), int (m.group (2)), int (m.group (3))
            local = name.replace ("../3.0.0/", "")

            if local not in done:
                x, sr = sf.read (os.path.join (src_dir, name), dtype="float64")
                assert sr == SR
                # tune= で合わせていた高さ（落ち着いた所の高さ）に、頭から終わりまでそろえる
                y, fixed = flatten_pitch (x, hz (key - tune / 100.0))
                y, softened = soften_attack (y)
                dst = os.path.join (out, local)
                os.makedirs (os.path.dirname (dst), exist_ok=True)
                sf.write (dst, np.clip (y, -1, 1), SR, subtype="PCM_16", format="FLAC")
                # 測り直して、大きく変わったら測りそこねとみなして元の tune= のまま
                new_tune = -int (round (measure_cents (y, key)))
                done[local] = new_tune if abs (new_tune - tune) <= 8 else tune
                print (f"  {local}: attack pitch {fixed:4.0f} cent flattened, attack -{softened:.1f} dB, tune {tune} -> {done[local]}")

            lines[i] = line.replace (m.group (0), f"sample={local} pitch_keycenter={key} tune={done[local]}")

        with open (os.path.join (out, sfz), "w", encoding="utf-8") as f:
            f.write ("\n".join (lines) + "\n")


if __name__ == "__main__":
    main()
