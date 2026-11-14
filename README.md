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
   `patches/rexglue-sdk-nx/` (the same 16 patches as singularity-nx), upstream rexglue-sdk `v0.10.0` in
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

4. Build the shaders once (12,982 shaders and their DKSH variants, takes hours):

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

`deadpool.toml` starts from singularity-nx's settings (1785 MHz CPU, 460 MHz GPU, 540p, no runtime shader compiles,
no depth prepass); none of them is tested with Deadpool yet.

### License

GPL-3.0 (game-side code from gears2-nx / singularity-nx); SDK patches BSD-3-Clause, shader tools MIT. See
`THIRD_PARTY_NOTICES.md`.
