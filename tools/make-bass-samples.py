#!/usr/bin/env python3
"""
内蔵ベース 2.0.0 のサンプルを作る（弦の物理モデル: 拡張 Karplus-Strong）。

- jazz-finger: ジャズベース（指弾き）… 柔らかい指の当たり、ネック寄りを弾く、フロントとリアのピックアップを混ぜた音
- jazz-pick:   ジャズベース（ピック弾き）… 硬い当たり、ブリッジ寄り、高域が多い、ピックの当たる音
- upright:     ダブルベース（ウッドベース）… 指で弦の中ほどを弾く、高域が早く減衰、胴の鳴りと指の「ボン」という音

音は E1〜A#3 を 3 半音ごと、強さ 2 段で作る（sfz でその間を埋める）。
使い方: python3 tools/make-bass-samples.py <出力フォルダ>
"""

import math
import os
import sys

import numpy as np
import soundfile as sf
from scipy.signal import lfilter

SR = 44100
ROOTS = list (range (28, 59, 3))           # E1 (28) 〜 A#3 (58)
LAYERS = [ (0, 80, 0.45), (81, 127, 1.0) ] # (lovel, hivel, 強さ)
RNG = np.random.default_rng (20260928)     # 毎回同じ音になるように


def midi_to_hz (n):
    return 440.0 * 2.0 ** ((n - 69) / 12.0)


def one_pole_lowpass (x, cutoff):
    a = math.exp (-2.0 * math.pi * cutoff / SR)
    return lfilter ([1.0 - a], [1.0, -a], x)


def resonator (x, freq, q, gain):
    """胴鳴りの共鳴（2 次のバンドパス）。"""
    w = 2.0 * math.pi * freq / SR
    alpha = math.sin (w) / (2.0 * q)
    b = [alpha * gain, 0.0, -alpha * gain]
    a = [1.0 + alpha, -2.0 * math.cos (w), 1.0 - alpha]
    return lfilter (np.array (b) / a[0], np.array (a) / a[0], x)


def comb (x, delay):
    """弦の弾く位置・ピックアップの位置による櫛形フィルター（1 - z^-d）。"""
    d = max (1, int (round (delay)))
    y = x.copy()
    y[d:] -= x[:-d]
    return y


def pluck (f0, seconds, excitation, t60, brightness):
    """
    拡張 Karplus-Strong。excitation を 1 周期ぶんの遅延線に入れて回す。
    t60: 基音が 60 dB 下がるまでの秒数。brightness: 0（すぐこもる）〜 1（高域が残る）。
    """
    n = int (seconds * SR)
    period = SR / f0

    g = 10.0 ** (-3.0 / (f0 * t60))                # 1 周期あたりの減衰
    s = 0.5 * (1.0 - brightness) + 0.02           # ループの平滑化（大きいほど高域が早く消える）

    # 遅延線の長さ + ループフィルターの遅れ（s サンプル）+ オールパスの遅れ（frac）= 1 周期
    delay = int (math.floor (period - s))
    frac = period - s - delay

    if frac < 0.1:           # オールパスが安定する範囲（0.1〜1.1）に収める
        delay -= 1
        frac += 1.0

    ap = (1.0 - frac) / (1.0 + frac)

    buf = np.zeros (delay)          # 読んだ所に書く: ちょうど delay サンプルの遅れ
    exc = np.zeros (n)
    exc[:len (excitation)] = excitation

    out = np.zeros (n)
    idx = 0
    prev = 0.0
    ap_x1 = 0.0
    ap_y1 = 0.0

    for i in range (n):
        x = buf[idx]
        # ループフィルター: 平均化（低域通過）と減衰
        y = g * ((1.0 - s) * x + s * prev)
        prev = x
        # 端数の遅れ（オールパス）
        z = ap * y + ap_x1 - ap * ap_y1
        ap_x1 = y
        ap_y1 = z
        v = z + exc[i]
        buf[idx] = v
        out[i] = v
        idx += 1
        if idx >= len (buf):
            idx = 0

    return out


def make_note (preset, note, strength):
    f0 = midi_to_hz (note)
    period = SR / f0
    length = int (period)

    if preset == "upright":
        seconds, t60 = 3.6, 3.0 + 1.0 * (58 - note) / 30
        brightness = 0.10 + 0.20 * strength
        burst = RNG.standard_normal (length)
        burst = one_pole_lowpass (burst, 350 + 700 * strength)
        burst = comb (burst, period * 0.45)                     # 弦の中ほどを指で弾く
    elif preset == "jazz-pick":
        seconds, t60 = 4.5, 3.8 + 1.5 * (58 - note) / 30
        brightness = 0.55 + 0.35 * strength
        burst = RNG.standard_normal (length)
        burst = one_pole_lowpass (burst, 2500 + 4500 * strength)
        burst = comb (burst, period * 0.12)                     # ブリッジ寄り
    else:  # jazz-finger
        seconds, t60 = 4.5, 3.5 + 1.5 * (58 - note) / 30
        brightness = 0.30 + 0.30 * strength
        burst = RNG.standard_normal (length)
        burst = one_pole_lowpass (burst, 700 + 1800 * strength)
        burst = comb (burst, period * 0.30)                     # ネック寄り

    burst *= np.hanning (length) ** 0.5
    burst /= max (1e-9, np.max (np.abs (burst)))
    y = pluck (f0, seconds, burst * strength, t60, brightness)

    if preset == "upright":
        # 胴の鳴り（低めの共鳴をいくつか）と、指が弦を離れる「ボン」
        body = resonator (y, 98, 6, 0.8) + resonator (y, 190, 5, 0.5) + resonator (y, 390, 4, 0.25)
        y = 0.6 * y + body
        thump = one_pole_lowpass (RNG.standard_normal (int (0.04 * SR)), 180) * np.hanning (int (0.04 * SR))
        y[:len (thump)] += thump * 0.8 * strength
        y = one_pole_lowpass (y, 2200)
    else:
        # ピックアップ: 位置による櫛形フィルター（フロントとリアを混ぜる）とトーン
        front = comb (y, period * 0.18)
        rear = comb (y, period * 0.07)
        y = 0.6 * front + 0.4 * rear
        y = one_pole_lowpass (y, 3200 if preset == "jazz-finger" else 5500)

        if preset == "jazz-pick":
            click = RNG.standard_normal (int (0.004 * SR)) * np.hanning (int (0.004 * SR))
            y[:len (click)] += one_pole_lowpass (click, 6000) * 0.5 * strength

        y = np.tanh (y * 1.3) / 1.3   # アンプの軽い歪み

    # 最後の 0.3 秒でフェードアウト（ぷつっと切れないように）
    fade = int (0.3 * SR)
    y[-fade:] *= np.linspace (1.0, 0.0, fade)
    return y


def main():
    out_dir = sys.argv[1] if len (sys.argv) > 1 else "."
    os.makedirs (out_dir, exist_ok = True)

    for preset in ("jazz-finger", "jazz-pick", "upright"):
        notes = {}
        peak = 0.0
        loudness = []

        for root in ROOTS:
            for lo, hi, strength in LAYERS:
                y = make_note (preset, root, strength)
                notes[(root, lo)] = y

                if strength == 1.0:
                    peak = max (peak, np.max (np.abs (y)))
                    loudness.append (np.sqrt (np.mean (y[:SR // 2] ** 2)))

        # 強い段の出だし 0.5 秒の平均が -16 dBFS（ほかの音色と同じくらいの大きさ）。ただしピークは -1 dBFS まで
        # 全部を同じ倍率にして、音の高さ・強さのつり合いは保つ
        scale = min (10 ** (-16 / 20) / float (np.mean (loudness)), 10 ** (-1 / 20) / peak)

        for (root, lo), y in notes.items():
            path = os.path.join (out_dir, preset, f"{root}_v{lo}.flac")
            os.makedirs (os.path.dirname (path), exist_ok = True)
            sf.write (path, (y * scale).astype (np.float32), SR, subtype = "PCM_16")

        print (preset, "done")


if __name__ == "__main__":
    main()
