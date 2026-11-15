#!/usr/bin/env bash
# The remapped pixel shaders (SDK 0019, interpolators linked by register) the console used, compiled on the PC into
# out/shaders/shadercache_dksh.bin, so the console stops compiling them while playing.
#
#   tools/switch/remap_variants.sh [list]
#
# The console appends each one it uses to cache/variantes_remapeo.txt beside the NRO (SPIR-V fingerprint,
# specialization, variant, 32 input locations in hex). Copy it to tools/switch/shaders/variantes_remapeo.txt (the
# default list; lines from several sessions are merged and deduplicated here) and run this, then copy
# out/shaders/shadercache_dksh.bin to the card. Needs tools/switch/shaders.sh to have run once (the library, ds_dksh).
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="$root/out/shaders"
lista="${1:-$root/tools/switch/shaders/variantes_remapeo.txt}"
[ -s "$lista" ] || { echo "no list at $lista"; exit 0; }
[ -x "$out/build_dksh/ds_dksh" ] || { echo "run tools/switch/shaders.sh first"; exit 1; }
sdk="${DEADPOOL_SDK_NX:-$root/thirdparty/rexglue-sdk-nx}"
mkdir -p "$out/bin"
[ -x "$out/bin/dksh_merge" ] ||
  clang-20 -O2 -I"$sdk/thirdparty/xxHash" "$root/tools/switch/shaders/dksh_merge.c" -o "$out/bin/dksh_merge"
{ grep -v '^#' "$lista" || true; } | sort -u > "$out/variantes_remapeo.txt"
[ -s "$out/variantes_remapeo.txt" ] || { echo "no remap variants listed"; exit 0; }
echo "$(wc -l < "$out/variantes_remapeo.txt") remap variants listed"
(cd "$out" && build_dksh/ds_dksh deadpool_shaders.dslib remapeo_dksh.bin --variantes variantes_remapeo.txt \
   --solo-variantes 1 --contenedores contenedores --informe remapeo.tsv --hilos "$(nproc)" > remapeo.log 2>&1) ||
  echo "some failed: $out/remapeo.tsv"
grep -E 'variantes de|cache:' "$out/remapeo.log" | tail -3
"$out/bin/dksh_merge" "$out/shadercache_merged.bin" "$out/remapeo_dksh.bin" "$out/shadercache_dksh.bin"
mv "$out/shadercache_merged.bin" "$out/shadercache_dksh.bin"
