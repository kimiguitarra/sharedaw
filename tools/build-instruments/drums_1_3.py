#!/usr/bin/env python3
"""
内蔵ドラム（builtin.drums）1.3.0 を 1.2.0 から作る（サンプルの音声は 1.0.0 / 1.2.0 のものをそのまま使う）。

  python3 drums_1_3.py <assets/instruments/builtin.drums>

- ハイハット（クローズ・ペダル・オープン）の強さ（ベロシティ）による音量を滑らかにする。
  元の録音は強さの段階（レイヤー）ごとに音量が大きく違い、段階の境目で 4〜6 dB 跳んでいた
  （例: ベロシティ 64 未満で急に小さくなる）。各レイヤーの実際の音量を測って、
  音量 = 最強の音量 × (ベロシティ / 127)^0.8 になるように、レイヤーごとの音量とレイヤー内のカーブを決める
  （音色はこれまでどおりレイヤーで変わる）。
- Superior Drummer 3 の既定の MIDI マップのノートでも鳴るように、パーツに別名のノート（aliases）を付ける。

必要: pip install numpy soundfile
"""
import json
import math
import os
import re
import shutil
import sys

import numpy as np
import soundfile as sf

ROOT = sys.argv[1]
SRC = os.path.join(ROOT, "1.2.0")
OUT = os.path.join(ROOT, "1.3.0")
CURVE = 0.8           # 音量 ∝ (v/127)^CURVE（1.0 で振幅がベロシティに比例）
HATS = ["hhClosed", "hhPedal", "hhOpen"]

# Superior Drummer 3 の既定の MIDI マップ（GM と同じノートは除く）。パーツ -> [(ノート, 表示名)]
SD3 = {
    "kick":         [(34, "キック（SD3）")],
    "rim":          [(1, "サイドスティック（SD3）"), (2, "サイドスティック（SD3）"), (3, "サイドスティック（SD3）")],
    "snare":        [(33, "スネア（エッジ）"), (68, "スネア（ミュート）"), (69, "スネア（フラム）"), (125, "スネア（センター寄り）")],
    "rimshot":      [(71, "スネア（リムのみ）")],
    "hhClosed":     [(22, "ハイハット（クローズ・エッジ）"), (11, "ハイハット（クローズ・チップ）"), (61, "ハイハット（クローズ・チップ）"),
                     (62, "ハイハット（タイト・エッジ）"), (63, "ハイハット（タイト・チップ）"), (119, "ハイハット（クローズ・ベル）"),
                     (65, "ハイハット（シーケンス）")],
    "hhPedal":      [(21, "ハイハット（ペダル）"), (10, "ハイハット（ペダル）"), (23, "ハイハット（ペダル・オープン）")],
    "hhOpen":       [(n, f"ハイハット（オープン・チップ {n - 12}）") for n in range(12, 18)]
                    + [(64, "ハイハット（オープン・エッジ 0）"), (24, "ハイハット（オープン・エッジ 1）"), (25, "ハイハット（オープン・エッジ 2）"),
                       (26, "ハイハット（オープン・エッジ 3）"), (60, "ハイハット（オープン・エッジ 4）")]
                    + [(n, f"ハイハット（オープン・ベル {n - 120}）") for n in range(120, 125)],
    "tomHighMid":   [(82, "タム 1（リムショット）"), (81, "タム 1（リム）")],
    "tomMid":       [(80, "タム 2（リムショット）"), (79, "タム 2（リム）")],
    "tomLow":       [(78, "タム 3（リムショット）"), (77, "タム 3（リム）")],
    "tomFloorHigh": [(75, "フロアタム 1（リムショット）"), (74, "フロアタム 1（リム）")],
    "tomFloor":     [(73, "フロアタム 2（リムショット）"), (72, "フロアタム 2（リム）")],
    "crash":        [(n, "クラッシュ（シンバル 1）") for n in range(83, 89)]
                    + [(27, "クラッシュ（ボウ）"), (28, "クラッシュ（ベル）"), (50, "クラッシュ（ミュート）")]
                    + [(n, "クラッシュ（シンバル 2）") for n in range(89, 95)],
    "splash":       [(56, "スプラッシュ（ミュート）")] + [(n, "スプラッシュ（シンバル 3）") for n in range(95, 101)],
    "crash2":       [(31, "クラッシュ 2（ボウ）"), (32, "クラッシュ 2（ベル）"), (58, "クラッシュ 2（ミュート）")]
                    + [(n, "クラッシュ 2（シンバル 4）") for n in range(101, 107)],
    "china":        [(54, "チャイナ（ミュート）")] + [(n, "チャイナ（シンバル 5）") for n in range(107, 113)],
    "ride":         [(29, "ライド（ボウ・シャンク）"), (113, "ライド（ボウ・チップ）"), (116, "ライド（ボウ・シャンク）"), (118, "ライド（ミュート）")],
    "rideBell":     [(30, "ライド（ベル）"), (114, "ライド（ベル）"), (117, "ライド（ベル・チップ）")],
    "ride2":        [(115, "ライド（クラッシュ）")],
}


def parse_opcodes(text):
    return dict(re.findall(r"(\w+)=(\S+)", text))


def parse_sfz(path):
    """<group> / <region> だけの SFZ を、リージョンごとの opcode（グループの値を含む）のリストにする。"""
    comment, regions, group = [], [], {}
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if line.startswith("//"):
            comment.append(line)
        elif line.startswith("<group>"):
            group = parse_opcodes(line[len("<group>"):])
        elif line.startswith("<region>"):
            regions.append({**group, **parse_opcodes(line[len("<region>"):])})
    return comment, regions


def layer_level_db(base_dir, regions):
    """レイヤーの音量: 各サンプルの 30ms の RMS の最大値（dB）の平均。"""
    levels = []
    for r in regions:
        x, rate = sf.read(os.path.join(base_dir, r["sample"]), always_2d=True)
        mono = x.mean(axis=1)
        win = max(1, int(rate * 0.03))
        rms = np.sqrt(np.convolve(mono ** 2, np.ones(win) / win, mode="valid"))
        levels.append(20 * math.log10(rms.max() + 1e-12))
    return sum(levels) / len(levels)


def target_db(v):
    return 20 * math.log10((max(v, 1) / 127.0) ** CURVE)


def even_out(src_path, out_path, base_dir):
    comment, regions = parse_sfz(src_path)
    layers = {}
    for r in regions:
        lo, hi = int(r.get("lovel", 0)), int(r.get("hivel", 127))
        layers.setdefault((lo, hi), []).append(r)

    levels = {k: layer_level_db(base_dir, v) for k, v in layers.items()}
    top = levels[max(layers, key=lambda k: k[1])]
    velocity_keys = ("lovel", "hivel", "amp_veltrack", "group_volume")

    lines = comment + [f"// ShareDAW 1.3.0: 強さによる音量を (ベロシティ/127)^{CURVE} に揃えた（tools/build-instruments/drums_1_3.py）"]
    for (lo, hi), rs in sorted(layers.items()):
        lo1 = max(lo, 1)
        volume = float(rs[0].get("group_volume", 0)) + (top + target_db(hi)) - levels[(lo, hi)]
        low_gain = 10 ** ((target_db(lo1) - target_db(hi)) / 20)
        lines.append(f"<group> lovel={lo} hivel={hi} amp_veltrack=100 amp_velcurve_{lo1}={low_gain:.4f} amp_velcurve_{hi}=1 "
                     f"group_volume={volume:.2f}")
        for r in rs:
            ops = {k: v for k, v in r.items() if k not in velocity_keys and not k.startswith("amp_velcurve_")}
            lines.append("<region> " + " ".join(f"{k}={v}" for k, v in ops.items()))
    open(out_path, "w", encoding="utf-8").write("\n".join(lines) + "\n")


def main():
    if os.path.exists(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)
    shutil.copy(os.path.join(SRC, "LICENSE-CC0-1.0.txt"), OUT)

    # サンプルの SFZ（音声のパスは 1.3.0 のフォルダから見た相対パスに直す）
    for dirpath, _, files in os.walk(os.path.join(SRC, "samples")):
        for name in files:
            src = os.path.join(dirpath, name)
            rel = os.path.relpath(src, SRC)
            out = os.path.join(OUT, rel)
            os.makedirs(os.path.dirname(out), exist_ok=True)
            text = open(src, encoding="utf-8").read().replace("sample=audio/", "sample=../1.2.0/audio/")
            open(out, "w", encoding="utf-8").write(text)

    for kit in ["rusty", "virtuosity"]:
        for piece in HATS:
            path = os.path.join(OUT, "samples", kit, piece + ".sfz")
            even_out(path, path, OUT)

    manifest = json.load(open(os.path.join(SRC, "manifest.json"), encoding="utf-8"))
    manifest["version"] = "1.3.0"
    used = {p["note"] for p in manifest["pieces"]}
    for p in manifest["pieces"]:
        aliases = [{"note": n, "name": name} for n, name in SD3.get(p["key"], []) if n not in used]
        if aliases:
            p["aliases"] = aliases
            used.update(a["note"] for a in aliases)
    json.dump(manifest, open(os.path.join(OUT, "manifest.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=2)


main()
