#!/usr/bin/env bash
# Optional syntax check of shaders/Orb.hlsl using glslangValidator's HLSL
# front-end. This is NOT the compiler used at runtime (that's D3DCompileFromFile
# / d3dcompiler_47.dll), but it catches typos on non-Windows dev machines.
#
#   sudo apt install glslang-tools       # Debian/Ubuntu
#   ./tools/check_hlsl.sh
set -euo pipefail
cd "$(dirname "$0")/.."

if ! command -v glslangValidator >/dev/null 2>&1; then
  echo "glslangValidator not found. Install glslang-tools, or just build on Windows."
  exit 0
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
status=0
for spec in "vert VSFullscreen" "frag PSOrb" "vert VSParticles" "frag PSParticles"; do
  set -- $spec
  stage=$1
  entry=$2
  cp shaders/Orb.hlsl "$tmp/Orb.$stage.hlsl"
  echo "--- checking $entry ($stage) ---"
  if ! glslangValidator -D -V -S "$stage" -e "$entry" --hlsl-iomap \
        "$tmp/Orb.$stage.hlsl" -o "$tmp/$entry.spv"; then
    status=1
  fi
done
if [ "$status" -eq 0 ]; then
  echo "All HLSL entry points parsed successfully."
fi
exit "$status"
