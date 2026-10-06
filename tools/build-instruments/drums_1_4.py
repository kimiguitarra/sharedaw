#!/usr/bin/env python3
"""
内蔵ドラム 1.4.0 のマニフェストを 1.3.0 から作る（サンプルは 1.3.0 のものをそのまま使う: samplesFrom）。

Superior Drummer 3 のマップのノート（1.3.0 で別名にしてある）を、同じ録音から音の違いを付けて鳴らし分ける:
- ハイハットの開き具合（オープン 0〜5）: 開いた録音を、開き具合に合わせた長さで減衰させる（0 がいちばん閉じ気味で短い）
- チップ（スティックの先）/ エッジ・シャンク（スティックの肩）: チップは低い所を削って軽く、シャンクは胴鳴りを足して太く
- ベル: 低い所を削って高い所を持ち上げる
- ライドのエッジ（クラッシュのように叩く）: 低い所から中域を足して広がりを出す
- ミュート: 短く切る
スネア（とリムショット）は、胴の深さ（深い・普通・浅い）とシェル（標準・ウッド・メタル）を選べる。

使い方: python3 tools/build-instruments/drums_1_4.py assets/instruments/builtin.drums
"""

import json
import os
import sys

root = sys.argv[1]
m = json.load (open (os.path.join (root, "1.3.0", "manifest.json"), encoding = "utf-8"))
m["version"] = "1.4.0"
m["samplesFrom"] = "1.3.0"

# 音の違い（SFZ のオプコード）。音量は amplitude（%）で変える（パーツの音量の volume を上書きしないように）
TIP = "fil_type=hpf_2p cutoff=650 amplitude=80"
SHANK = "eq2_freq=320 eq2_bw=1.4 eq2_gain=3 amplitude=112"
BELL = "fil_type=hpf_2p cutoff=1600 eq2_freq=3800 eq2_bw=1.2 eq2_gain=4"
OPEN_DECAY = [1.1, 1.8, 3.0, 5.0, 9.0, None]   # オープン 0〜5 の減衰（SFZ の ampeg_decay。None は録音のまま。0 でもクローズより少し長く鳴る）


def openness (k):
    d = OPEN_DECAY[min (k, 5)]
    return "" if d is None else f"ampeg_sustain=0 ampeg_decay={d}"


def join (*parts):
    return " ".join (p for p in parts if p)


def variant (name, note):
    """別名の表示名から、その音の違いを決める。"""
    n = name
    level = None

    for k in range (6):
        if f" {k}）" in n:
            level = k

    sfz = []

    if "オープン" in n and level is not None:
        sfz.append (openness (level))

    if "ベル" in n and "ライド" not in n and "クラッシュ" not in n:
        sfz.append (BELL)
    elif "チップ" in n:
        sfz.append (TIP)
    elif "シャンク" in n:
        sfz.append (SHANK)

    if "タイト" in n:
        sfz.append ("ampeg_sustain=0 ampeg_decay=0.07")

    if "ミュート" in n:
        sfz.append ("ampeg_sustain=0 ampeg_decay=0.22")

    if "ライド（クラッシュ）" in n or "エッジ" in n and "ライド" in n:
        sfz.append ("eq2_freq=450 eq2_bw=2 eq2_gain=4 amplitude=115")

    return join (*sfz)


for piece in m["pieces"]:
    for a in piece.get ("aliases", []):
        sfz = variant (a.get ("name", ""), a["note"])

        if sfz:
            a["sfz"] = sfz

    # ライドのエッジ（59）はクラッシュのように広がる音
    if piece["key"] == "ride2":
        piece["sfzExtra"] = join (piece.get ("sfzExtra", ""), "eq2_freq=450 eq2_bw=2 eq2_gain=4 amplitude=115")

    # スネア: 胴の深さとシェル（リムショットも同じ）
    if piece["key"] == "snare":
        piece["options"] = [
            { "key": "depth", "name": "胴の深さ", "default": "normal", "choices": [
                { "key": "deep", "name": "深い", "sfz": "eq3_freq=170 eq3_bw=1.2 eq3_gain=4.5 eq4_freq=7000 eq4_bw=1.5 eq4_gain=-4" },
                { "key": "normal", "name": "普通", "sfz": "" },
                { "key": "shallow", "name": "浅い", "sfz": "eq3_freq=190 eq3_bw=1.2 eq3_gain=-5 eq4_freq=3200 eq4_bw=1.4 eq4_gain=3 ampeg_sustain=0 ampeg_decay=0.42 amplitude=150" },
            ] },
            { "key": "shell", "name": "シェル", "default": "standard", "choices": [
                { "key": "standard", "name": "標準", "sfz": "" },
                { "key": "wood", "name": "ウッド", "sfz": "eq5_freq=420 eq5_bw=1.5 eq5_gain=2 eq6_freq=6500 eq6_bw=1.5 eq6_gain=-3" },
                { "key": "metal", "name": "メタル", "sfz": "eq5_freq=950 eq5_bw=0.4 eq5_gain=3.5 eq6_freq=6000 eq6_bw=1.2 eq6_gain=4" },
            ] },
        ]

    if piece["key"] == "rimshot":
        piece["optionsFrom"] = "snare"

out = os.path.join (root, "1.4.0", "manifest.json")
os.makedirs (os.path.dirname (out), exist_ok = True)
json.dump (m, open (out, "w", encoding = "utf-8"), ensure_ascii = False, indent = 2)

for piece in m["pieces"]:
    for a in piece.get ("aliases", []):
        if a.get ("sfz"):
            print (a["note"], a["name"], "->", a["sfz"])
