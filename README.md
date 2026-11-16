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
   `patches/rexglue-sdk-nx/` (singularity-nx's 16 patches plus `0017` DKSH cache entries up to 8 MiB, `0018` interpolators linked
   by register as on the Xbox 360, `0019` queue scratch for spilling shaders, `0020` remapped pixel shaders listed for
   the PC cache, `0021` `invariant gl_Position` in every vertex shader and `0022` the profiler's raw samples, frame
   times and CPU per core, `0023` the geometry cache swept a slice per frame), upstream rexglue-sdk `v0.10.0` in
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
GPU (uam: "array index must be < 16"), so their draws are skipped; the other 12,980 have every DKSH variant.

### Changes from singularity-nx

Same as there: the rexglue-sdk-nx patches `0001`-`0016`, the ARM fixes (switch traps, memory barriers, spin hints)
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

### Known issues

- Bink movies are black: their YUV shaders are not in the shader library, and the renderer takes their draws for
  clears.
- The register-linked interpolators (SDK `0018`) compile their remapped pixel shaders on the console the first time
  (~10-40 ms each, in the background); they should become PC-built cache variants.

### License

GPL-3.0 (game-side code from gears2-nx / singularity-nx); SDK patches BSD-3-Clause, shader tools MIT. See
`THIRD_PARTY_NOTICES.md`.
