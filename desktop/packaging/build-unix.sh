#!/usr/bin/env bash
set -euo pipefail
desktop_root="$(cd "$(dirname "$0")/.." && pwd)"
python3 "$desktop_root/packaging/fetch_node.py"
if [[ "$(uname -s)" == Darwin ]]; then node_target=macos; else node_target=linux; fi
cmake -S "$desktop_root" -B "$desktop_root/build" -DCMAKE_BUILD_TYPE=Release -DWHIPPET_NODE_DIR="$desktop_root/vendor/$node_target-node" "$@"
cmake --build "$desktop_root/build" --parallel 3
ctest --test-dir "$desktop_root/build" --output-on-failure
python3 "$desktop_root/tests/integration.py" "$desktop_root/vendor/$node_target-node/whippetd"
mkdir -p "$desktop_root/dist"
(cd "$desktop_root/build" && cpack -B "$desktop_root/dist")
