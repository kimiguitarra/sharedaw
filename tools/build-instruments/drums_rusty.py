#!/usr/bin/env python3
"""
内蔵ドラム（builtin.drums）の「rusty」キットを Karoryfer x Big Cat Big Rusty Drums（CC0）から作る。

  python3 drums_rusty.py <big-rusty-drums のフォルダ> <出力: assets/instruments/builtin.drums/<version>>

- 各打撃の近接マイク（モノラル）とオーバーヘッド（ステレオ）を、作者の既定のミックスのバランスで 1 本のステレオにまとめる
  （マイクごとに鳴らすより軽く、ファイルも少ない）。
- ベロシティレイヤー・ラウンドロビンは元の SFZ マッピング（近接マイクのもの）どおりに書き出す。
- 無音の末尾を切り、16bit FLAC（44.1kHz のまま。sfizz がエンジンのレートに変換する）で保存する。
- 出力: audio/rusty/<piece>/*.flac と samples/rusty/<piece>.sfz（<group>/<region> だけ。key はアプリが付ける）

必要: pip install numpy soundfile
"""
import json
import os
import re
import sys

import numpy as np
import soundfile as sf

SRC, OUT = sys.argv[1], sys.argv[2]
MAP = os.path.join(SRC, "Programs", "mappings")

# piece: (マッピングのフォルダ, 近接マイクのマッピングファイル, [(マイクのサンプルフォルダ名, 音量, パン or None=ステレオ)])
#   音量・パンは元の「02-basic.sfz」の既定値（マイクごとの CC）に合わせる: 音量 = CC / 127（× マスターの amplitude）、
#   パン = (CC - 63) / 64。マイクのバランスが作者の既定のミックスと同じになり、キット全体は 1 つの係数でだけ正規化する。
#   サンプルの場所はマッピングの sample= のパスの「マイク名」の部分を置き換えて求める。


def cc(v, amp=100):
    return v / 127 * amp / 100


def pan(v):
    return (v - 63) / 64


SNARE = [("btm", cc(100), 0.0), ("top", cc(40), 0.0), ("oh", cc(60), None)]
HIHAT = [("cl", cc(100, 70), 0.0), ("oh", cc(100, 70), None)]
CYMBAL_LEFT = [("cl", cc(40), pan(30)), ("oh", cc(100), None)]
CYMBAL_RIGHT = [("cl", cc(40), pan(97)), ("oh", cc(100), None)]

PIECES = {
    "kick":     ("kick_24",   "k_kick.sfz",        [("kick", cc(100), 0.0)]),
    "snare":    ("snare_14",  "sn_center_top.sfz", SNARE),
    "rimshot":  ("snare_14",  "sn_rims_top.sfz",   SNARE),
    "rim":      ("snare_14",  "sn_ss_top.sfz",     SNARE),
    "hhClosed": ("hihat_14",  "ht_tc_cl.sfz",      HIHAT),
    "hhPedal":  ("hihat_14",  "ht_chik_cl.sfz",    HIHAT),
    "hhOpen":   ("hihat_14",  "ht_open_cl.sfz",    HIHAT),
    "tom14":    ("tom_14",    "tom_14_cl.sfz",     [("cl", cc(100), pan(20)), ("oh", cc(70), None)]),
    "tom15":    ("tom_15",    "tom_15_cl.sfz",     [("cl", cc(100), pan(40)), ("oh", cc(70), None)]),
    "tom18":    ("tom_18",    "tom_18_cl.sfz",     [("cl", cc(100), pan(87)), ("oh", cc(70), None)]),
    "tom22":    ("tom_22",    "tom_22_cl.sfz",     [("cl", cc(100), pan(100)), ("oh", cc(70), None)]),
    "crash":    ("crash_17",  "cr_cl.sfz",         CYMBAL_LEFT),
    "crash2":   ("crash_sizzle_17", "crs_cr_cl.sfz", CYMBAL_RIGHT),
    "china":    ("china_18",  "cn_cl.sfz",         CYMBAL_RIGHT),
    "ride":     ("ride_22",   "rd_cl.sfz",         CYMBAL_RIGHT),
    "rideBell": ("ride_22",   "rd_bl_cl.sfz",      CYMBAL_RIGHT),
}

PEAK = 0.89             # キット全体の最大ピーク（-1 dBFS）
KIT_VOLUME_DB = 7       # 生のドラムはピークに対して平均音量が小さいので、再生時に持ち上げる（group_volume はパーツの volume に足される）
TAIL_THRESHOLD = 10 ** (-72 / 20)
FADE_SECONDS = 0.05


def parse_mapping(path):
    """<group> ごとの (opcodes, [(sample, seq_position)]) を返す。"""
    groups, current, region = [], None, None
    text = open(path, encoding="utf-8").read()
    text = re.sub(r"//[^\n]*", "", text)

    for token in re.findall(r"<\w+>|[\w$]+=[^\s<]+", text):
        if token == "<group>":
            current = {"opcodes": {}, "regions": []}
            groups.append(current)
            region = None
        elif token == "<region>":
            region = {"sample": None, "seq": 1}
            current["regions"].append(region)
        elif "=" in token:
            k, v = token.split("=", 1)
            if region is not None:
                if k == "sample":
                    region["sample"] = v
                elif k == "seq_position":
                    region["seq"] = int(v)
                else:
                    region.setdefault("extra", {})[k] = v
            elif current is not None:
                current["opcodes"][k] = v

    return groups


def mic_path(sample, close_mic, mic):
    # ../Samples/kick_24/kick/kick/k_vl1_rr1.flac の「マイク」フォルダを差し替える
    parts = sample.replace("\\", "/").split("/")
    assert parts[-2] == close_mic, (sample, close_mic)
    parts[-2] = mic
    return os.path.normpath(os.path.join(SRC, "Programs", *parts))


def to_stereo(x, pan):
    if x.ndim == 2:
        return x
    left, right = min(1.0, 1.0 - pan), min(1.0, 1.0 + pan)
    return np.stack([x * left, x * right], axis=1)


def mix(sample, mics, close_mic):
    tracks, rate = [], None

    for mic, gain, pan in mics:
        data, sr = sf.read(mic_path(sample, close_mic, mic), dtype="float64")
        rate = rate or sr
        assert sr == rate
        tracks.append((mic, to_stereo(data, pan if pan is not None else 0.0) * gain))

    # スネアの裏のマイクは表と逆相になりやすいので、相関が負なら反転する
    names = [m for m, _ in tracks]
    if "btm" in names and "top" in names:
        b, t = names.index("btm"), names.index("top")
        n = min(len(tracks[b][1]), len(tracks[t][1]), int(0.05 * rate))
        if np.sum(tracks[b][1][:n, 0] * tracks[t][1][:n, 0]) < 0:
            tracks[b] = ("btm", -tracks[b][1])

    length = max(len(t) for _, t in tracks)
    out = np.zeros((length, 2))
    for _, t in tracks:
        out[: len(t)] += t
    return out, rate


def trim(x, rate):
    loud = np.nonzero(np.max(np.abs(x), axis=1) > TAIL_THRESHOLD)[0]
    end = min(len(x), (loud[-1] if len(loud) else 0) + int(FADE_SECONDS * rate))
    x = x[:end].copy()
    fade = min(end, int(FADE_SECONDS * rate))
    x[end - fade:] *= np.linspace(1.0, 0.0, fade)[:, None]
    return x


def close_mic_of(mapping_path):
    first = next(r["sample"] for g in parse_mapping(mapping_path) for r in g["regions"])
    return first.replace("\\", "/").split("/")[-2]


def mix_piece(folder, mapping, mics):
    path = os.path.join(MAP, folder, mapping)
    groups = parse_mapping(path)
    close = close_mic_of(path)
    mixed = {}

    for g in groups:
        for r in g["regions"]:
            if r["sample"] not in mixed:
                mixed[r["sample"]] = mix(r["sample"], mics, close)

    return groups, mixed


def write_piece(key, folder, mapping, groups, mixed, scale):
    out_dir = os.path.join(OUT, "audio", "rusty", key)
    os.makedirs(out_dir, exist_ok=True)
    names = {}

    for sample, (x, sr) in mixed.items():
        name = os.path.basename(sample)
        names[sample] = f"audio/rusty/{key}/{name}"
        sf.write(os.path.join(out_dir, name), trim(x * scale, sr), sr, subtype="PCM_16", format="FLAC")

    # アプリが書く <master>（key、音量、パン、チューニング）を消さないよう、<global> / <master> は使わない
    lines = [f"// {key}: Big Rusty Drums（CC0）{folder} / {mapping} のマイクを作者の既定のバランスでまとめたもの"]
    common = f"loop_mode=one_shot off_mode=normal ampeg_release=0.05 group_volume={KIT_VOLUME_DB}"

    for g in groups:
        ops = " ".join(f"{k}={v}" for k, v in g["opcodes"].items() if k.startswith(("lovel", "hivel", "amp_velcurve_")))
        lines.append(f"<group> {ops} seq_length={max(r['seq'] for r in g['regions'])} {common}".replace("  ", " "))
        for r in g["regions"]:
            lines.append(f"<region> sample={names[r['sample']]} seq_position={r['seq']}")

    os.makedirs(os.path.join(OUT, "samples", "rusty"), exist_ok=True)
    with open(os.path.join(OUT, "samples", "rusty", key + ".sfz"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")

    loudest, rate = mixed[groups[-1]["regions"][0]["sample"]]
    attack = loudest[: int(0.3 * rate)] * scale
    rms_db = 20 * np.log10(np.sqrt(np.mean(attack ** 2)) + 1e-12)
    peak_db = 20 * np.log10(np.max(np.abs(loudest)) * scale)
    size = sum(os.path.getsize(os.path.join(out_dir, n)) for n in os.listdir(out_dir))
    print(f"{key:9s} {len(mixed):3d} files  {size / 1e6:6.1f} MB  peak {peak_db:6.1f} dB  attack RMS {rms_db:6.1f} dB")


pieces = {key: mix_piece(folder, mapping, mics) for key, (folder, mapping, mics) in PIECES.items()}
peak = max(np.max(np.abs(x)) for _, mixed in pieces.values() for x, _ in mixed.values())
scale = PEAK / peak
print(f"kit gain {20 * np.log10(scale):+.1f} dB")

for key, (groups, mixed) in pieces.items():
    folder, mapping, _ = PIECES[key]
    write_piece(key, folder, mapping, groups, mixed, scale)
