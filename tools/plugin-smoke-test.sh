#!/usr/bin/env bash
# 外部プラグインまわりの動作確認（Linux / CI 用）。
#   1. スキャンは別プロセスで行い、クラッシュするプラグインはブラックリストに入る（本体は落ちない）
#   2. 外部プラグインの音源でバウンスでき、状態ファイルが書かれ、バウンスが最新と判定される
#   3. 状態ファイルがない環境（他の人の環境）ではバウンスした音で再生する
# 使い方: tools/plugin-smoke-test.sh <CollabDAW の実行ファイル> <ビルドフォルダ（-DCOLLAB_BUILD_TEST_PLUGINS=ON）>
set -euo pipefail

app="$1"
build="$2"
root="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d)"
export HOME="$work/home"
mkdir -p "$HOME/.vst3"

for name in CollabTestSynth CollabCrashPlugin; do
  bundle="$(find "$build" -type d -name "$name.vst3" -path "*_artefacts/*" | head -1)"
  test -n "$bundle" || { echo "$name.vst3 not found in $build"; exit 1; }
  cp -r "$bundle" "$HOME/.vst3/"
done
# moduleinfo.json があると .so を読み込まずに一覧に載るので、クラッシュ用は消しておく
rm -rf "$HOME/.vst3/CollabCrashPlugin.vst3/Contents/Resources"

run() { xvfb-run -a "$app" "$@" 2>&1 | tee -a "$work/log.txt"; }

echo "== scan"
out="$(run --scan-plugins)"
echo "$out" | grep -q "blacklisted: .*CollabCrashPlugin.vst3" || { echo "crash plugin was not blacklisted"; exit 1; }
echo "$out" | grep -q "found 1 plugin" || { echo "test synth was not found"; exit 1; }

echo "== bounce"
cp -r "$root/shared/fixtures/demo-project" "$work/proj"
python3 - "$work/proj/project.json" <<'PY'
import json, sys
p = sys.argv[1]
d = json.load(open(p))
d["tracks"] = [t for t in d["tracks"] if t["name"] == "Bass"]
d["tracks"][0]["instrument"] = {"kind": "external", "stateRef": "plugins-state/synth.bin",
    "plugin": {"format": "VST3", "name": "CollabTestSynth", "vendor": "CollabDAW Test", "uid": "unknown", "os": "linux"}}
json.dump(d, open(p, "w"), indent=2)
PY
out="$(run --bounce "$work/proj")"
echo "$out" | grep -q "^Bass: upToDate$" || { echo "bounce is not up to date"; exit 1; }
test -s "$work/proj/plugins-state/synth.bin" || { echo "plugin state was not written"; exit 1; }

echo "== other environment (no plugin state)"
cp -r "$work/proj" "$work/other"
rm -rf "$work/other/plugins-state"
out="$(run --render-status "$work/other")"
echo "$out" | grep -q "(playing render)" || { echo "render is not used"; exit 1; }
run --render "$work/other" "$work/other.wav" > /dev/null

echo "plugin smoke test passed"
