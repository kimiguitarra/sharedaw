#!/usr/bin/env bash
# 起動の確認（CI 用）: ビルドしたアプリを実際に起動して一通り操作し、落ちずに終わるかを見る。
#   tools/app-smoke-test.sh <アプリの実行ファイル> <assets フォルダ>
# - デモ曲の音源を assets の最新の版にして、書き出し（--render）が音の入ったファイルを作るか
# - 書き出し（ファイル → 書き出し と同じ処理）: WAV・MP3・パラデータ・MIDI を書いて、読み直せるか（--export）
# - --smoke-test: メニューの作成、曲を開く、ピアノロールの画面・ミキサーの開閉、再生・停止、外観の切り替え
# 設定は空の HOME で行う（初めて起動した人と同じ状態）。Linux では xvfb-run の中で呼ぶ。
# SMOKE_WRAPPER（例: "arch -x86_64"）を付けると、その上で起動する（Mac の Intel 版を Rosetta で確かめる）。
set -euo pipefail

exe="$1"
assets="$2"
work="$(mktemp -d)"
cp -r shared/fixtures/demo-project "$work/demo"
mkdir -p "$work/home"

python3 - "$work/demo/project.json" "$assets" <<'PY'
import json, os, sys
path, assets = sys.argv[1], sys.argv[2]
def latest(i):
    vs = [v for v in os.listdir(os.path.join(assets, "instruments", i)) if os.path.isfile(os.path.join(assets, "instruments", i, v, "manifest.json"))]
    return max(vs, key=lambda v: tuple(int(x) for x in v.split(".")))
p = json.load(open(path))
for t in p["tracks"]:
    inst = t.get("instrument")
    if inst and inst.get("kind") == "builtin":
        inst["version"] = latest(inst["id"])
        if inst["id"] == "builtin.drums":
            inst["params"]["kit"] = "JazzAcoustic"
        print("smoke project:", t["name"], inst["id"], inst["version"])
p["chordTrack"]["playback"]["instrument"]["version"] = latest("builtin.piano")
json.dump(p, open(path, "w"), ensure_ascii=False, indent=2)
PY

# 時間切れ（固まった）も失敗にする
run_with_timeout() {
    local seconds="$1"; shift
    "$@" &
    local pid=$!
    # 見張り役の出力はつながない（パイプを開いたままにすると、呼んだ側がその分待たされる）
    ( sleep "$seconds"; kill -9 "$pid" ) >/dev/null 2>&1 &
    local watchdog=$!
    local status=0
    wait "$pid" || status=$?
    pkill -P "$watchdog" 2>/dev/null || true
    kill "$watchdog" 2>/dev/null || true
    [ "$status" -ne 137 ] || echo "timed out after ${seconds}s (or killed)"
    return "$status"
}

echo "== render"
status=0
HOME="$work/home" run_with_timeout 300 ${SMOKE_WRAPPER:-} "$exe" --render "$work/demo" "$work/demo.wav" > "$work/render.log" 2>&1 || status=$?
grep -v "Assertion failure" "$work/render.log" || true
[ "$status" -eq 0 ] || { echo "render failed (exit status $status)"; exit 1; }
size=$(wc -c < "$work/demo.wav")
echo "rendered $size bytes"
[ "$size" -gt 100000 ] || { echo "render is too small"; exit 1; }

echo "== export"
for kind in wav mp3 midi stems; do
    case "$kind" in wav) out="$work/mix.wav" ;; mp3) out="$work/mix.mp3" ;; midi) out="$work/song.mid" ;; stems) out="$work/stems" ;; esac
    status=0
    HOME="$work/home" run_with_timeout 300 ${SMOKE_WRAPPER:-} "$exe" --export "$kind" "$work/demo" "$out" > "$work/export.log" 2>&1 || status=$?
    grep -E "^(exported|midi part|export failed|load failed|cannot read)" "$work/export.log" || true
    [ "$status" -eq 0 ] || { echo "export $kind failed (exit status $status)"; exit 1; }
done
stems=$(ls "$work/stems" | wc -l | tr -d ' ')
echo "stems: $stems files"
[ "$stems" -ge 3 ] || { echo "too few stems"; exit 1; }

echo "== smoke test"
HOME="$work/home" run_with_timeout "${SMOKE_TIMEOUT:-300}" ${SMOKE_WRAPPER:-} "$exe" --smoke-test "$work/demo" > "$work/smoke.log" 2>&1 || status=$?
grep -v "Assertion failure" "$work/smoke.log" || true

if [ "$status" -ne 0 ] || ! grep -q "SMOKE TEST PASSED" "$work/smoke.log"; then
    echo "smoke test failed (exit status $status)"
    exit 1
fi

# 録音のタイミング: 出力を決まった遅れで入力に戻す仮想の機器で録音し、録れた音（メトロノームのクリック）が拍の位置に来るか
# （入力 300 + 出力 500 サンプル、バッファ 256 / 入力 600 + 出力 600、バッファ 512）
echo "== recording timing"
for spec in 300,500,256 600,600,512; do
    rm -rf "$work/rec"
    cp -r "$work/demo" "$work/rec"
    # 曲の音はほぼ無音にして（トラックは残す）、メトロノームのクリックだけが入力に戻るようにする
    python3 - "$work/rec/project.json" <<'PY'
import json, sys
p = json.load(open(sys.argv[1]))
for t in p["tracks"]:
    t["volumeDb"] = -80.0
p["chordTrack"]["events"] = []
json.dump(p, open(sys.argv[1], "w"), ensure_ascii=False)
PY
    status=0
    # 2 回目は、もう MIDI クリップがある所の上に録る（Tracktion がそのクリップにノートを足して、画面に出ないまま鳴っていた）
    overlap=""; [ "$spec" = "600,600,512" ] && overlap=1
    SHAREDAW_REC_OVERLAP="$overlap" SHAREDAW_LOOPBACK="$spec" HOME="$work/home" run_with_timeout 120 ${SMOKE_WRAPPER:-} "$exe" --record-test "$work/rec" 5 0 1 > "$work/rec.log" 2>&1 || status=$?
    grep -E "^(loopback|audio offset|midi offset|live|take|record failed|no take|notes )" "$work/rec.log" || true
    [ "$status" -eq 0 ] || { echo "record test failed (exit status $status)"; exit 1; }
    python3 - "$work/rec.log" <<'PY'
import re, sys
text = open(sys.argv[1]).read()
m = re.search(r"audio offset ms: median (-?[\d.e-]+)", text)
assert m, "no audio offset"
# 録った音は拍から 1 ms 以内（Tracktion の録音は、バッファ 1〜2 個分＝5〜20 ms 前にずれていた）
assert abs(float(m.group(1))) < 1.0, text
# MIDI: クリックごとに弾いたノートが全部録れている（録音中に入力の設定を触ると途中で止まっていた）・拍から 15 ms 以内（Linux）
m = re.search(r"midi offset ms: median (-?[\d.e-]+) .*\((\d+)\)", text)
# macOS の CI（仮想マシン）は処理が詰まって仮想の機器の時間がぶれるので、ずれは表示だけにする（ノートの数は確かめる）
assert m and int(m.group(2)) >= 6, text
if sys.platform != "darwin":
    assert abs(float(m.group(1))) < 15.0, text
elif abs(float(m.group(1))) >= 15.0:
    print("note: midi offset", m.group(1), "ms (not checked on macOS CI)")
# 録音中も、弾いたノートが画面用に届いている
assert re.search(r"live: \S+ peaks \d+ notes ([1-9]\d*)", text), text
# エンジンが鳴らすノートは、プロジェクト（画面）のノートと同じ（隠れて鳴る音がない）
counts = re.findall(r"^notes .*: project (\d+) engine (\d+)$", text, re.M)
assert counts and all(a == b for a, b in counts), text
PY
done
echo "recording timing ok"

# ピッチベンド: 全音符の A3（220 Hz）を一番上まで曲げると 2 半音上（246.9 Hz）で鳴る
echo "== pitch bend"
for mode in flat up; do
    rm -rf "$work/pb-$mode"
    cp -r "$work/demo" "$work/pb-$mode"
    python3 - "$work/pb-$mode/project.json" "$mode" <<'PY'
import json, sys, uuid
p = json.load(open(sys.argv[1]))
p["chordTrack"]["events"] = []
tracks = []
for t in p["tracks"]:
    if t["name"] == "Piano":
        c = t["clips"][0]
        c["startTick"], c["lengthTick"] = 0, 960 * 8
        c["notes"] = [{"id": str(uuid.uuid4()), "tick": 0, "lengthTick": 960 * 4, "pitch": 57, "velocity": 100}]
        if sys.argv[2] == "up":
            c["pitchBends"] = [[0, 8191]]
        tracks.append(t)
p["tracks"] = tracks
json.dump(p, open(sys.argv[1], "w"), ensure_ascii=False)
PY
    HOME="$work/home" run_with_timeout 300 ${SMOKE_WRAPPER:-} "$exe" --render "$work/pb-$mode" "$work/pb-$mode.wav" > /dev/null 2>&1 || { echo "pitch bend render failed"; exit 1; }
done
python3 - "$work/pb-flat.wav" "$work/pb-up.wav" <<'PY'
import sys, struct, math
def read_wav(path):
    """標準ライブラリだけで読む（32 bit float の WAV も）。1 チャンネル目と、サンプルレートを返す。"""
    data = open(path, "rb").read()
    pos, fmt, rate, ch, bits, samples = 12, 1, 48000, 2, 16, b""
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            fmt, ch, rate = struct.unpack("<HHI", body[:8])
            bits = struct.unpack("<H", body[14:16])[0]
            if fmt == 0xfffe:
                fmt = struct.unpack("<H", body[24:26])[0]
        elif cid == b"data":
            samples = body
        pos += 8 + size + (size & 1)
    width = bits // 8
    count = len(samples) // width
    if fmt == 3:
        vals = struct.unpack("<%df" % count, samples[:count * 4])
    elif width == 3:
        vals = [int.from_bytes(samples[i:i + 3], "little", signed=True) for i in range(0, count * 3, 3)]
    else:
        vals = struct.unpack("<%d%s" % (count, {2: "h", 4: "i"}[width]), samples[:count * width])
    return list(vals[::ch]), rate
def pitch(path):
    seg, rate = read_wav(path)
    seg = seg[int(0.3 * rate): int(1.3 * rate)]
    # 自己相関で 150〜300 Hz の周期を探す（間引いて速く）
    best, best_lag = -1e30, 0
    for lag in range(int(rate / 300), int(rate / 150) + 1):
        s = sum(seg[i] * seg[i + lag] for i in range(0, len(seg) - lag, 8))
        if s > best:
            best, best_lag = s, lag
    # 前後の値で放物線補間（周期の端数）
    def ac(l): return sum(seg[i] * seg[i + l] for i in range(0, len(seg) - l, 8))
    a, b, c = ac(best_lag - 1), best, ac(best_lag + 1)
    shift = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0.0
    return rate / (best_lag + shift)
flat, up = pitch(sys.argv[1]), pitch(sys.argv[2])
semitones = 12 * math.log2(up / flat)
print("pitch bend: %.1f Hz -> %.1f Hz (%.2f semitones)" % (flat, up, semitones))
assert abs(semitones - 2.0) < 0.15, semitones
PY

# クリップを動かす・元に戻す・やり直すたびに、エンジンのクリップとノートが画面（プロジェクト）と同じ
# （エンジンのクリップを作り直すとき 1 つおきに消し残し、画面にない古い場所の音が鳴っていた）
echo "== engine follows edits and undo"
rm -rf "$work/edit"
cp -r "$work/demo" "$work/edit"
status=0
HOME="$work/home" run_with_timeout 120 ${SMOKE_WRAPPER:-} "$exe" --edit-test "$work/edit" > "$work/edit.log" 2>&1 || status=$?
grep -E "^edit" "$work/edit.log" || true
[ "$status" -eq 0 ] && grep -q "^edit test: ok" "$work/edit.log" || { echo "edit test failed (exit status $status)"; exit 1; }

# MIDI キーボードの音は、選んだ MIDI トラックの音源だけで鳴る（入力はすべての MIDI トラックにつないだまま、門で選ぶ。
# 入力先を変えて再生の処理を作り直すと、再生中に選択トラックを切り替えたときに音が途切れていた）
echo "== keyboard plays only the selected track"
rm -rf "$work/gate"
cp -r "$work/demo" "$work/gate"
python3 - "$work/gate/project.json" <<'PY'
import json, sys
p = json.load(open(sys.argv[1]))
for t in p["tracks"]:
    t["clips"] = []
p["chordTrack"]["events"] = []
json.dump(p, open(sys.argv[1], "w"), ensure_ascii=False)
PY
status=0
SHAREDAW_LOOPBACK=256,256,256 SHAREDAW_LOOPBACK_NOMETRO=1 SHAREDAW_SWITCH=midi HOME="$work/home" run_with_timeout 120 ${SMOKE_WRAPPER:-} "$exe" --switch-test "$work/gate" 1 > "$work/gate.log" 2>&1 || status=$?
grep -E "^gate" "$work/gate.log" || true
[ "$status" -eq 0 ] && grep -q "^gate: ok" "$work/gate.log" || { echo "keyboard gate failed (exit status $status)"; exit 1; }
