#!/usr/bin/env python3
"""
内蔵ドラム（builtin.drums）の「JazzAcoustic」キットを Virtuosity Drums（CC0）から作る。

  python3 drums_virtuosity.py <virtuosity_drums のフォルダ> <出力: assets/instruments/builtin.drums/<version>>

- ビンテージの 4 点キット（キック・スネア・タム 2 つ）とライド・フラットライドで、ジャズ向けの柔らかい音。
- 元の「01-basic-kit.sfz」と同じく、キックのマイク・スネアのマイク（モノラル）とオーバーヘッド（ステレオ）を同じ音量で
  1 本のステレオにまとめる（マイクごとに鳴らすより軽く、ファイルも少ない）。
- ベロシティレイヤー・ラウンドロビン・ベロシティカーブは元の SFZ マッピングどおりに書き出す。
- 頭と末尾の無音を切り、44.1kHz・16bit FLAC で保存する（元は 48kHz）。
- 出力: audio/virtuosity/<piece>/*.flac と samples/virtuosity/<piece>.sfz（<group>/<region> だけ。key はアプリが付ける）

必要: pip install numpy scipy soundfile
"""
import os
import re
import sys
from math import gcd

import numpy as np
import soundfile as sf
from scipy import signal

SRC, OUT = sys.argv[1], sys.argv[2]
MAP = os.path.join(SRC, "Programs", "mappings")
MICS = ["kickmic", "snaremic", "oh"]
RATE = 44100

# piece: (マッピング, 元の <master> の設定のうち使うもの)
KICK = "amp_veltrack=100 amp_velcurve_1=0.4"
SNARE = "amp_veltrack=0"
HIHAT = "amp_veltrack=100 amp_velcurve_1=0.4"
CRASH = "amp_veltrack=100 amp_velcurve_1=0.4"
RIDE = "amp_veltrack=50"
TOM = "amp_veltrack=0"

PIECES = {
    "kick":      ("kick_snon",        KICK),
    "snare":     ("snare_center",     SNARE),
    "rimshot":   ("snare_rimshot",    SNARE),
    "rim":       ("snare_crossstick", SNARE),
    "hhClosed":  ("hh_closed",        HIHAT),
    "hhPedal":   ("hh_pedal",         "amp_veltrack=50"),
    "hhOpen":    ("hh_open",          HIHAT),
    "tomHigh":   ("htom_center",      TOM),
    "tomLow":    ("ltom_center",      TOM),
    "ride":      ("ride_ride",        RIDE),
    "rideBell":  ("ride_bell",        RIDE),
    "flatRide":  ("flatride_ride",    RIDE),
    "flatCrash": ("flatride_crash",   RIDE),
    "crash":     ("crash_crash",      CRASH),
    "sizzle":    ("crash_sizzle",     CRASH),
}

PEAK = 0.89             # キット全体の最大ピーク（-1 dBFS）
KIT_VOLUME_DB = 7       # 生のドラムはピークに対して平均音量が小さいので、再生時に持ち上げる（rusty と同じ）
TAIL_THRESHOLD = 10 ** (-72 / 20)
FADE_SECONDS = 0.05


def parse_mapping(path):
    """[(sample, {opcode: value})] を返す（このマッピングは <region> ごとに設定が書いてある）。"""
    text = re.sub(r"//[^\n]*", "", open(path, encoding="utf-8").read())
    regions = []
    for token in re.findall(r"<\w+>|[\w$]+=[^\s<]+", text):
        if token == "<region>":
            regions.append({})
        elif "=" in token and regions:
            k, v = token.split("=", 1)
            regions[-1][k] = v
    return [(r.pop("sample"), r) for r in regions]


def mic_path(sample, mic):
    # ../Samples/kickmic/kick/kickmic_kick_snon_vl1_rr1.flac のマイクの名前を差し替える
    parts = sample.replace("\\", "/").split("/")
    assert parts[2] == "kickmic", sample
    parts[2] = mic
    parts[-1] = parts[-1].replace("kickmic_", mic + "_", 1)
    return os.path.normpath(os.path.join(SRC, "Programs", *parts))


# 元の offset=$HH_PPREROLL（48kHz で 1000 サンプル）: ペダルを踏む前の音を飛ばす。ここでは先に切っておく
SKIP = {"hhPedal": 1000}


def mix(sample, skip=0):
    out = None
    for mic in MICS:
        x, sr = sf.read(mic_path(sample, mic), dtype="float64")
        x = x[skip:]
        if x.ndim == 1:
            x = np.stack([x, x], axis=1)
        if sr != RATE:
            g = gcd(RATE, sr)
            x = signal.resample_poly(x, RATE // g, sr // g, axis=0)
        if out is None:
            out = x
        else:
            n = max(len(out), len(x))
            out = np.pad(out, ((0, n - len(out)), (0, 0))) + np.pad(x, ((0, n - len(x)), (0, 0)))
    return out


def trim(x):
    # 頭の 4〜6 ms の無音（ハイハットのペダルは踏む前の音も）を詰め、ノートを置いた所で鳴るようにする（打つ 1 ms 前から残す）
    level = np.max(np.abs(x), axis=1)
    start = max(0, int(np.argmax(level > level.max() * 0.05)) - int(0.001 * RATE))
    x = x[start:]
    loud = np.nonzero(np.max(np.abs(x), axis=1) > TAIL_THRESHOLD)[0]
    end = min(len(x), (loud[-1] if len(loud) else 0) + int(FADE_SECONDS * RATE))
    x = x[:end].copy()
    fade = min(end, int(FADE_SECONDS * RATE))
    x[end - fade:] *= np.linspace(1.0, 0.0, fade)[:, None]
    return x


pieces = {}
for key, (mapping, _) in PIECES.items():
    regions = parse_mapping(os.path.join(MAP, "kickmic", mapping + "_map.sfz"))
    pieces[key] = (regions, {s: mix(s, SKIP.get(key, 0)) for s, _ in regions})

peak = max(np.max(np.abs(x)) for _, mixed in pieces.values() for x in mixed.values())
scale = PEAK / peak
print(f"kit gain {20 * np.log10(scale):+.1f} dB")

for key, (regions, mixed) in pieces.items():
    mapping, master = PIECES[key]
    out_dir = os.path.join(OUT, "audio", "virtuosity", key)
    os.makedirs(out_dir, exist_ok=True)
    names = {}
    for s, x in mixed.items():
        name = os.path.basename(s).replace("kickmic_", "")
        names[s] = f"audio/virtuosity/{key}/{name}"
        sf.write(os.path.join(out_dir, name), trim(x * scale), RATE, subtype="PCM_16", format="FLAC")

    # アプリが書く <master>（key、音量、パン、チューニング）を消さないよう、<global> / <master> は使わない
    lines = [f"// {key}: Virtuosity Drums（CC0）{mapping} のキック・スネア・オーバーヘッドのマイクを同じ音量でまとめたもの",
             f"<group> loop_mode=one_shot off_mode=normal ampeg_release=0.05 group_volume={KIT_VOLUME_DB} {master}"]
    for s, ops in regions:
        extra = " ".join(f"{k}={v}" for k, v in ops.items() if k.startswith(("lovel", "hivel", "amp_velcurve_", "seq_")))
        lines.append(f"<region> sample={names[s]} {extra}".rstrip())

    os.makedirs(os.path.join(OUT, "samples", "virtuosity"), exist_ok=True)
    with open(os.path.join(OUT, "samples", "virtuosity", key + ".sfz"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")

    size = sum(os.path.getsize(os.path.join(out_dir, n)) for n in os.listdir(out_dir))
    print(f"{key:9s} {len(mixed):3d} files  {size / 1e6:6.1f} MB")
