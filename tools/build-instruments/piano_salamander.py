#!/usr/bin/env python3
"""
内蔵ピアノ（builtin.piano）を Salamander Grand Piano V3（Alexander Holm、CC-BY 3.0）から作る。

  python3 piano_salamander.py <SalamanderGrandPiano（sfzinstruments 版）のフォルダ> <出力: assets/instruments/builtin.piano/<version>>

軽量にするため（仕様書 §9: 内蔵音源込みで 1GB 以内）:
- 16 あるベロシティレイヤーを 8 にする（隣り合う 2 つのうち強いほうを使い、ベロシティの範囲は 2 つ分）
- 24bit → 16bit、長い減衰は MAX_SECONDS で打ち切ってフェードアウト
- ハンマー・弦の共鳴・ペダルのノイズは使わない
サンプル名の # は s にする（Ds1v2.flac）。必要: pip install numpy soundfile
"""
import os
import re
import sys

import numpy as np
import soundfile as sf

SRC, OUT = sys.argv[1], sys.argv[2]
MAX_SECONDS = 10.0
FADE_SECONDS = 1.5
TAIL_THRESHOLD = 10 ** (-84 / 20)

# 元のレイヤーのベロシティ範囲（notes.txt）
RANGES = {1: (1, 26), 2: (27, 34), 3: (35, 36), 4: (37, 43), 5: (44, 46), 6: (47, 50), 7: (51, 56), 8: (57, 64),
          9: (65, 72), 10: (73, 80), 11: (81, 88), 12: (89, 96), 13: (97, 104), 14: (105, 112), 15: (113, 120), 16: (121, 127)}
LAYERS = [(v + 1, RANGES[v][0], RANGES[v + 1][1]) for v in range(1, 17, 2)]   # (使うレイヤー, lovel, hivel)

regions = []
for line in open(os.path.join(SRC, "Data", "region.txt"), encoding="utf-8"):
    if not line.startswith("<region>"):
        continue
    ops = dict(re.findall(r"(\w+)=(\S+)", line))
    regions.append({"lokey": int(ops["lokey"]), "hikey": int(ops["hikey"]), "center": int(ops["pitch_keycenter"]),
                    "note": ops["sample"].split("$")[0], "release": ops.get("ampeg_release")})

os.makedirs(os.path.join(OUT, "audio"), exist_ok=True)
total = 0

for layer, _, _ in LAYERS:
    for r in regions:
        src = os.path.join(SRC, "Samples", f"{r['note']}v{layer}.flac")
        x, sr = sf.read(src, dtype="float64")
        loud = np.nonzero(np.max(np.abs(x), axis=1) > TAIL_THRESHOLD)[0]
        end = min(len(x), int(MAX_SECONDS * sr), (loud[-1] if len(loud) else 0) + int(0.05 * sr))
        x = x[:end].copy()
        fade = min(end, int(FADE_SECONDS * sr) if end >= int(MAX_SECONDS * sr) else int(0.05 * sr))
        x[end - fade:] *= np.linspace(1.0, 0.0, fade)[:, None]
        name = f"{r['note'].replace('#', 's')}v{layer}.flac"
        sf.write(os.path.join(OUT, "audio", name), x, sr, subtype="PCM_16", format="FLAC")
        total += os.path.getsize(os.path.join(OUT, "audio", name))

# アプリが書く <master>（トーン）を消さないよう、<global> / <master> は使わない
lines = ["// Salamander Grand Piano V3 by Alexander Holm (CC-BY 3.0)。ShareDAW 用に 8 レイヤー・16bit に縮小",
         "// 各レイヤー内の強弱は amp_veltrack で付ける（元の既定値に近い値）"]
for layer, lo, hi in LAYERS:
    lines.append(f"<group> lovel={lo} hivel={hi} amp_veltrack=30 ampeg_release=1 note_polyphony=2 group_volume=9")
    for r in regions:
        release = f" ampeg_release={r['release']}" if r["release"] else ""
        lines.append(f"<region> sample=audio/{r['note'].replace('#', 's')}v{layer}.flac lokey={r['lokey']} hikey={r['hikey']} "
                     f"pitch_keycenter={r['center']}{release}")

with open(os.path.join(OUT, "piano.sfz"), "w", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")

print(f"{len(LAYERS)} layers x {len(regions)} notes, {total / 1e6:.1f} MB")
