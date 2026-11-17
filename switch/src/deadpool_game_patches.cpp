// deadpool-nx - game patches in the generated code, run by tools/switch/codegen.sh. Each patched site asks
// DeadpoolGamePatch(id) whether it is on: a cvar each, so they can be compared from deadpool.toml without a rebuild.
// From singularity-nx's singularity_game_patches.cpp (gears2-nx's gow2_game_patches.cpp, GPL-3.0).
//
//   1  (singularity-nx: GUseTilingMode = FALSE) not needed: Deadpool's renderer init sub_829B6600 stores 0 to its
//      tiling and MSAA flags (0x83657740 / 0x83657744) itself; only the TOGGLETILING / TOGGLEMSAA console commands
//      (sub_829D2150) turn them on.
//   3  The 540p scene (tools/switch/codegen/patch_scene_resolution.py, from gears2-nx 1.1.0's game patch 8):
//      sub_829D6808's hard-coded height 720 and 1280 width cap become 540 / 960. deadpool_app.h sets the SDK's
//      video_mode_width / video_mode_height to 960x540 with it, for the UI and back buffer.
#include <rex/cvar.h>

REXCVAR_DEFINE_INT32(deadpool_resolution, 720, "Deadpool",
                     "Scene resolution at startup: 720 (1280x720, the game's), 540 (960x540, about 56% of the "
                     "pixel work) or 360 (640x360, 25%); game patch 3 plus a video mode of that size")
    .allowed({"720", "540", "360"});

extern "C" int DeadpoolGamePatch(int id) {
  switch (id) {
    case 3: return REXCVAR_GET(deadpool_resolution) != 720;
    default: return 0;
  }
}

// Game patch 3's scene height (720 = the game's own).
extern "C" int DeadpoolSceneHeight() {
  const int h = REXCVAR_GET(deadpool_resolution);
  return h == 540 || h == 360 ? h : 720;
}
