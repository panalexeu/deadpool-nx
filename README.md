Nintendo Switch port of the Xbox 360 version of Deadpool (2013), built by static recompilation with rexglue-sdk and
drawn by a native deko3d renderer.

> **Status:** work in progress. The NRO, the shader library and its DKSH cache build; it has not been run on a console
> yet.

It reuses the Unreal Engine 3 on Switch stack of [singularity-nx](https://github.com/panalexeu/singularity-nx)
(itself from [gears2-nx](https://github.com/VasilkovND/gears2-nx)), adapted to Deadpool. The PC version is
deadpool-recomp, where `overrides.toml` comes from. It needs your **legally owned and purchased** Xbox 360 copy: no game
data or recompiled code is part of this repository.

### Instructions

Requirements: a Switch with Atmosphere running homebrew in title takeover mode (hold R while starting a game); Linux with
git, Python 3, CMake 3.25+, Ninja, clang-20, Docker; `dxc` and `spirv-val` from the Vulkan SDK.

1. Clone and set up the SDKs: rexglue-sdk-nx pinned to `f961303` in `thirdparty/rexglue-sdk-nx` plus
   `patches/rexglue-sdk-nx/` applied in order (listed under **SDK patches** below), upstream rexglue-sdk `v0.10.0` in
   `thirdparty/rexglue-sdk` (its `rexglue` codegen tool), and the Docker image with the Switch toolchain:

   ```bash
   git clone --recursive <this repo's URL> deadpool-nx
   cd deadpool-nx
   tools/setup.sh
   ```

2. Extract your copy of the game (`default.xex` and `TransGame/`) into `deadpool-extract/` (gitignored). Only the
   `default.xex` with SHA-256 `5a78a5f5319d79a2d64eebff5be10aed83b06263c1ad69a39d077fd6da82c002` (Title ID 415608EC)
   is supported.

3. Recompile the game code into `out/codegen-nx/` (never share it), about 30 minutes:

   ```bash
   tools/switch/codegen.sh        # or REXGLUE=<path to a v0.10.0 rexglue> tools/switch/codegen.sh
   ```

4. Build the shaders once (12,982 shaders and their DKSH variants, about 3.5 hours on 12 threads; the variants that
   time out at 120 s are retried with a 30-minute limit and merged into `shadercache_dksh.bin`):

   ```bash
   DXC=<path to dxc> VAL=<path to spirv-val> tools/switch/shaders.sh
   ```

5. Build the NRO (about an hour the first time; use `tools/switch/build-native.sh 2` on an 8 GB PC):

   ```bash
   tools/switch/build-native.sh
   ```

6. Copy to `sdmc:/switch/deadpool/`: `out/build/switch-native/deadpool.nro`, `switch/deadpool.toml`,
   `out/shaders/deadpool_shaders.dslib`, `out/shaders/shadercache_dksh.bin`, `out/shaders/info_shaders.bin` (into a
   `cache/` folder there) and your `deadpool-extract/` folder. Launch the NRO from the homebrew menu in title takeover
   mode; logs go to `logs/` next to it.

Known shader issue: the vertex shaders `vs_185367fe72f7ab08` and `vs_e1511ae81967229d` do not compile for the Switch
GPU (uam: "array index must be < 16"), so their draws are skipped; the other 12,982 have every DKSH variant.

### SDK patches

`patches/rexglue-sdk-nx/`, applied in order on rexglue-sdk-nx `f961303` (`git apply`):

| Patch | What it does |
|---|---|
| `0001` | gears2-nx's SDK patches 0001-0049, merged: Unreal Engine 3 on deko3d |
| `0002` | singularity-nx's SDK patches 0002-0016, merged: occlusion results, background shader compiler, texture and depth fixes, thread cores, shader info cache |
| `0003` | DKSH cache entries up to 8 MiB |
| `0004` | interpolators linked by register, as on the Xbox 360 |
| `0005` | queue scratch for shaders that spill; larger ones are refused |
| `0006` | remapped pixel shaders listed for the PC cache |
| `0007` | `invariant gl_Position` in every vertex shader, so depth-equal passes cover the same pixels |
| `0008` | profiler raw samples: frame times, per-core CPU, stack samples (`deadpool_perfil`) |
| `0009` | the geometry cache swept a slice per frame |
| `0010` | no SD work on the GPU ring thread; the profiler report runs below the game |
| `0011` | remapped pixel shaders patched in their DKSH binary instead of compiled on the console |
| `0012` | a missing variant's fallback remembered |
| `0013` | the ring thread's maps take their nodes from a pool |
| `0014` | XMA sweeps only allocated contexts; NEON sample conversion |
| `0015` | guest threads placed on the core of their Xbox hardware thread |
| `0016` | the frame dump can skip a frame's first N copies (`nfsmw_dk_volcado_saltar`) |

### Changes from singularity-nx

Same as there: the rexglue-sdk-nx patches `0001`-`0002` (gears2-nx's and singularity-nx's, merged), the ARM fixes (switch traps, memory barriers, spin hints)
and gears2-nx's CPU passes (leaf prologue, stack access, inline calls, guest registers in host locals), the CPU clock
thread, the guards. Game-specific parts, with Deadpool's addresses found by matching Singularity's functions
instruction by instruction in the generated code:

| Part | Deadpool | Singularity |
|---|---|---|
| Direct3D "is the GPU there yet" (sleeps instead of spinning) | `sub_82D92290` | `sub_826845D8` |
| Direct3D query GetData (sleeps instead of spinning) | `sub_82D93B90` (newer XDK) | `sub_8267CCB8` |
| C runtime memcpy / memset, native | `sub_82F8A050` / `sub_82F88DF0` | `sub_829E2330` / `sub_829E2290` |
| XAudio2 state-variable filter mono / stereo, native (six modes here, four there) | `sub_8312AF68` / `sub_8312B178` | `sub_828EDF78` / `sub_828EE028` |
| 540p scene (game patch 3): the hard-coded scene size | `sub_829D6808`: 1280x720 -> 960x540 | `sub_82DBFA80` |
| No predicated tiling (game patch 1) | not needed: the renderer init `sub_829B6600` stores 0 to the tiling flag (`0x83657740`) itself | `sub_82DAD680` |
| Coalesced config re-hash | dropped: the image holds no SHA-1 table for `Coalesced_*.bin` | `0x83A02E80` |
| Shader extractor | packages version 846, licensee 181: 16 more bytes before the folder name, the GUID fields of 623 | version 584 |

`deadpool.toml` starts from singularity-nx's settings (1785 MHz CPU, 460 MHz GPU, 540p, no runtime shader compiles),
except: the depth prepasses are drawn (Deadpool's screen-space shadows and depth of field read their depth), occlusion
waits for the real count and never answers from the previous frame, Singularity's XMA and audio filter changes are off,
and `nfsmw_dk_sombras_sesgo_pitch` keeps its default (1600): Singularity's 960 gave the 960-wide scene depth prepass the
shadow-map slope bias, and the base pass, GEQUAL against it, failed on every pixel (the world black).

### Profiling a scene

Set `deadpool_perfil = true` in `deadpool.toml`, play the scene for a minute or more, copy `logs/` from the card and
run `tools/switch/perf_report.py <logs> -o report.md` with the ELF of the same build (`out/build/switch-native/deadpool`).
It reports the frame-time distribution, a verdict per 10 s (GPU-bound, CPU-bound on which thread, or waiting), the
load of each core, the functions that spend each thread's CPU (self and inclusive, game functions as `sub_8XXXXXXX`),
what threads wait on in the kernel, and what each thread did during the frames over 50 ms.

### What works so far

- The whole game runs at 540p on the Switch.
- Rendering fixes:
  - **The world drawn:** the depth prepass gets the right slope bias.
  - **Bink movies:** their Y/U/V shaders were captured and added to the library.
  - **No black scene under the HUD:** Direct3D's clears now write the clear rectangle's vertex colour, where they used to write a stale PS c0 or 0, so the UI layer's alpha is 1 again.
  - **Remapped pixel shaders:** patched in their DKSH binary instead of compiled on the console (SDK `0011`).
- Performance:
  - **No SD writes on the GPU ring thread:** logs, remap lists and profiler reports are buffered and written in the background, which removes the periodic stalls.
  - **Ring thread, about 12 → 8 µs per draw:** node pools for its maps, the geometry cache swept a slice per frame, and the missing-variant fallback remembered.
  - **XMA:** only allocated contexts are swept, and frames are converted with NEON.
  - **Native code:** the hot copy, fill and D3D constant-copy functions, and the game's LZX decompression (libmspack).
  - **Thread placement:** guest threads go to the core of their Xbox hardware thread.
  - **PGO** (`PGO=generate` / `PGO=use` in `build-native.sh`, `tools/switch/pgo_merge.sh`): about +10 % fps in the heaviest late level (24.6 → 27.0 fps), which is CPU-bound.
- Tools:
  - the profiler (`deadpool_perfil`, `perf_report.py`);
  - frame dumps (`nfsmw_dk_volcado_saltar` skips a frame's first N copies).

### Known issues

- The heaviest late levels run at about 21-27 fps, limited by the CPU (about 280 % of the 300 % available).

### License

GPL-3.0 (game-side code from gears2-nx / singularity-nx); SDK patches BSD-3-Clause, shader tools MIT. See
`THIRD_PARTY_NOTICES.md`.
