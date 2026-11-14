#!/usr/bin/env bash
# The shader library and its DKSH cache, from your own copy of the game. Unreal Engine 3 keeps compiled Xenos shaders
# in the shader caches inside its cooked packages (TransGame/CookedXenon); they are pulled out
# (shaders/deadpool_extract_shaders.cpp), translated ahead of time and compiled for the Switch GPU
# (shaders/build_library.sh), so the game never compiles a shader while it runs.
#
#   tools/switch/shaders.sh
#
# Output: out/shaders/deadpool_shaders.dslib and out/shaders/shadercache_dksh.bin (copy both next to the NRO), and
# out/shaders/info_shaders.bin and out/shaders/nfsmw_dksh.bin (copy them to cache/ next to the NRO).
# Needs dxc and spirv-val (DXC=..., VAL=... or on PATH), clang-20 / clang++-20 and tools/setup.sh done.
# Takes hours (Singularity: 21,766 shaders and ~152,000 DKSH variants), on every core.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
game="$root/deadpool-extract"
out="$root/out"
sdk="${DEADPOOL_SDK_NX:-$root/thirdparty/rexglue-sdk-nx}"
mspack="$sdk/thirdparty/libmspack/libmspack/mspack"
[ -d "$game/TransGame/CookedXenon" ] || { echo "No $game/TransGame/CookedXenon (see README)" >&2; exit 1; }
[ -d "$mspack" ] || { echo "No libmspack in $sdk (run tools/setup.sh)" >&2; exit 1; }

mkdir -p "$out/bin"
src="$root/tools/switch/shaders/deadpool_extract_shaders.cpp"
if [ ! -x "$out/bin/deadpool_extract_shaders" ] || [ "$src" -nt "$out/bin/deadpool_extract_shaders" ]; then
  clang-20 -O2 -w -I"$mspack" -c "$mspack/lzxd.c" -o "$out/bin/lzxd.o"
  clang++-20 -std=c++20 -O2 -I"$mspack" -I"$sdk/thirdparty/xxHash" "$src" "$out/bin/lzxd.o" \
    -o "$out/bin/deadpool_extract_shaders"
fi
rm -rf "$out/shaders_extracted"
"$out/bin/deadpool_extract_shaders" "$out/shaders_extracted" "$game/TransGame" | tail -3 || true
echo "shaders: $(ls "$out/shaders_extracted" | grep -c '^vs_') VS, $(ls "$out/shaders_extracted" | grep -c '^ps_') PS"

# The variants the console compiled at run time in earlier sessions (shaders/dksh_variants.py) go in the cache too.
DKSH=1 VARIANTES="$root/tools/switch/shaders/dksh_variants.txt" \
  "$root/tools/switch/shaders/build_library.sh" "$out/shaders_extracted" "$out/shaders"

# Variants the full build could not compile in its 120 s limit, retried with 30 minutes each into a second cache,
# out/shaders/nfsmw_dksh.bin (copy it to cache/ next to the NRO). Deadpool: 199 of 203 compile on the retry (2-6 min
# each); vs_185367fe72f7ab08 and vs_e1511ae81967229d fail in uam ("array index must be < 16") at any limit.
clang-20 -O2 -I"$sdk/thirdparty/xxHash" "$root/tools/switch/shaders/xxh3.c" -o "$out/bin/xxh3"
python3 - "$out" <<'PY'
import csv, os, subprocess, sys
out = sys.argv[1]
bad = [r for r in csv.DictReader(open(f"{out}/shaders/dksh.tsv"), delimiter="\t") if r["resultado"] != "ok"]
names = sorted({r["shader"] for r in bad})
h = {}
if names:
    res = subprocess.run([f"{out}/bin/xxh3"] + [f"{out}/shaders/spirv/{n}.spv" for n in names],
                         capture_output=True, text=True, check=True).stdout
    h = {os.path.basename(p)[:-4]: k for k, p in (l.split() for l in res.splitlines())}
with open(f"{out}/shaders/fallidas.txt", "w") as f:
    f.write("# The variants the full DKSH build could not compile in 120 s (or at all), retried with a longer limit.\n")
    for r in bad:
        f.write(f"{h[r['shader']]} {r['especializacion']} {r['variante']}\n")
print(f"{len(bad)} variants of {len(names)} shaders to retry")
PY
if [ "$(grep -vc '^#' "$out/shaders/fallidas.txt")" -gt 0 ]; then
  (cd "$out/shaders" && build_dksh/ds_dksh deadpool_shaders.dslib nfsmw_dksh.bin --variantes fallidas.txt \
     --solo-variantes 1 --contenedores contenedores --informe fallidas.tsv --hilos "$(nproc)" --limite 1800 \
     > fallidas.log 2>&1) || echo "some variants still fail: $out/shaders/fallidas.tsv"
  grep -v 'variantes$' "$out/shaders/fallidas.log" | tail -4
fi
ls -la "$out/shaders/deadpool_shaders.dslib" "$out/shaders/shadercache_dksh.bin" "$out/shaders/info_shaders.bin" \
  "$out/shaders/nfsmw_dksh.bin" 2>/dev/null || true
