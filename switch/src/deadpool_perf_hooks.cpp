// deadpool-nx - CPU time the console has no use for. From singularity-nx's singularity_perf_hooks.cpp (gears2-nx's
// gow2/src/gow2_perf_hooks.cpp, GPL-3.0), with Deadpool's addresses: each function matched against Singularity's
// instruction by instruction in the generated code (the same Xbox 360 SDK and C runtime code).
//
// The game has three cores on the Switch, and in busy scenes all three were full while Unreal's rendering thread spent
// up to 41% of its time in one wait loop (Singularity). On the Xbox 360 these spins cost a hardware thread out of six;
// here they take a core from the threads they wait for (the game's audio thread, the GPU ring). While they answer
// "still waiting" the thread now sleeps instead.
//
//   sub_82D92290  Direct3D's "is the GPU there yet" check: 4 x 8 db16cyc delays, then a look at the GPU's progress.
//                 Answers 1 while still waiting; its callers simply call it again. (Singularity: sub_826845D8.)
//   sub_82D93B90  Direct3D's query GetData: 1 (S_FALSE) while the GPU has not written the result, 0 once it has.
//                 A newer XDK's than Singularity's sub_8267CCB8 (a data size argument, several counters), the same
//                 answers.
//
// Real work done natively:
//   sub_82F8A050  the C runtime's memcpy (r3 destination, r4 source, r5 bytes; answers r3), identical to Singularity's
//                 sub_829E2330.
//   sub_82F88DF0  the C runtime's memset (r3 destination, r4 byte, r5 bytes), identical to Singularity's sub_829E2290.
#include "deadpool_pch.h"

#include <atomic>
#include <chrono>
#include <bit>
#include <cmath>
#include <cstring>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>

#if defined(__SWITCH__)
#include <switch.h>
#endif

REXCVAR_DEFINE_INT32(deadpool_espera_us, 100, "Deadpool",
                     "Microseconds the game's busy waits (GPU progress, occlusion results) sleep each time the answer is "
                     "still 'not yet'; 0 = spin as on the Xbox 360");
REXCVAR_DEFINE_BOOL(deadpool_cpu_nativo, true, "Deadpool",
                    "The game's memcpy (sub_82F8A050) and memset (sub_82F88DF0) as native code");

extern "C" void DeadpoolOcclusionWait();  // deadpool_gpu.cpp: the game waits, answer it now

namespace {
enum Espera { kEsperaGpu, kEsperaConsulta, kEsperas };
std::atomic<uint64_t> g_esperas[kEsperas], g_esperas_ns[kEsperas];
std::atomic<uint64_t> g_informe_ns{0};

uint64_t AhoraNs() {
  return uint64_t(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

void Dormir(Espera tipo) {
#if defined(__SWITCH__)
  const uint64_t inicio = AhoraNs();
  const int32_t us = REXCVAR_GET(deadpool_espera_us);
  svcSleepThread(us > 0 ? int64_t(us) * 1000 : 0);
  const uint64_t fin = AhoraNs();
  g_esperas[tipo].fetch_add(1, std::memory_order_relaxed);
  g_esperas_ns[tipo].fetch_add(fin - inicio, std::memory_order_relaxed);
  uint64_t antes = g_informe_ns.load(std::memory_order_relaxed);
  if (fin - antes >= 10'000'000'000ull && g_informe_ns.compare_exchange_strong(antes, fin)) {
    REXLOG_INFO("[esperas] 10 s: GPU {} ({:.0f} ms), consultas {} ({:.0f} ms)", g_esperas[kEsperaGpu].exchange(0),
                double(g_esperas_ns[kEsperaGpu].exchange(0)) / 1e6, g_esperas[kEsperaConsulta].exchange(0),
                double(g_esperas_ns[kEsperaConsulta].exchange(0)) / 1e6);
  }
#else
  (void)tipo;
#endif
}
}  // namespace

REX_EXTERN(__imp__sub_82D92290);
REX_HOOK_RAW(sub_82D92290) {
  __imp__sub_82D92290(ctx, base);
  if (ctx.r3.u32 == 1) Dormir(kEsperaGpu);
}

REX_EXTERN(__imp__sub_82D93B90);
REX_HOOK_RAW(sub_82D93B90) {
  __imp__sub_82D93B90(ctx, base);
  if (ctx.r3.u32 == 1) {  // S_FALSE: not there yet
    DeadpoolOcclusionWait();
    Dormir(kEsperaConsulta);
  }
}

REX_EXTERN(__imp__sub_82F8A050);
REX_HOOK_RAW(sub_82F8A050) {
  if (!REXCVAR_GET(deadpool_cpu_nativo)) {
    __imp__sub_82F8A050(ctx, base);
    return;
  }
  if (const uint32_t n = ctx.r5.u32) std::memmove(base + ctx.r3.u32, base + ctx.r4.u32, n);
}

REX_EXTERN(__imp__sub_82F88DF0);
REX_HOOK_RAW(sub_82F88DF0) {
  if (!REXCVAR_GET(deadpool_cpu_nativo)) {
    __imp__sub_82F88DF0(ctx, base);
    return;
  }
  if (const uint32_t n = ctx.r5.u32) std::memset(base + ctx.r3.u32, int(ctx.r4.u32 & 0xFF), n);
}

// sub_8312AF68  a state-variable filter of the game's audio mixer (XAudio2), Singularity's sub_828EDF78 there ~20% of
//               its audio thread in busy scenes. Its parameters at r3: +0 input samples, +4 output samples, +8 / +12
//               pointers to the low and band states, +16 sample count, +24 mode, +28 the frequency coefficient f, +32
//               the damping. Per sample, as the PowerPC code (each fused multiply-add rounded once to single
//               precision): high = (x - low) - damping * band; band += high * f; low += band * f. Modes 0 low-pass,
//               1 band-pass, 2 high-pass, 3 notch (low + high), as Singularity's. Deadpool's newer XAudio2 adds two
//               one-pole modes: low = (1 - f) * low + x * f, out low (4) or x - low (5); above 5 nothing is written
//               (the states are stored back as they were). Called only through a table (no direct call sites), so no
//               inlining hides it.
REXCVAR_DEFINE_BOOL(deadpool_audio_filtro_nativo, true, "Deadpool",
                    "The game's audio state-variable filter (sub_8312AF68) as native code");

REX_EXTERN(__imp__sub_8312AF68);
REX_HOOK_RAW(sub_8312AF68) {
  if (!REXCVAR_GET(deadpool_audio_filtro_nativo)) {
    __imp__sub_8312AF68(ctx, base);
    return;
  }
  auto u32 = [&](uint32_t a) { return rex::memory::load_and_swap<uint32_t>(base + a); };
  auto lf = [&](uint32_t a) { return std::bit_cast<float>(u32(a)); };
  auto sf = [&](uint32_t a, float v) { rex::memory::store_and_swap<uint32_t>(base + a, std::bit_cast<uint32_t>(v)); };
  const uint32_t p = ctx.r3.u32;
  const uint32_t src = u32(p + 0), dst = u32(p + 4), p_low = u32(p + 8), p_band = u32(p + 12), n = u32(p + 16);
  const uint32_t mode = u32(p + 24);
  const float f = lf(p + 28), q = lf(p + 32);
  float low = lf(p_low), band = lf(p_band);
  const float k = 1.0f - f;  // fsubs from the constant 1.0 (0x82006578)
  for (uint32_t i = 0; mode <= 5 && i < n; ++i) {
    const float x = lf(src + i * 4);
    if (mode >= 4) {  // one pole
      low = std::fmaf(x, f, k * low);  // fmuls, then fmadds
      sf(dst + i * 4, mode == 4 ? low : x - low);
      continue;
    }
    const float d = x - low;                       // fsubs
    const float high = std::fmaf(-q, band, d);     // fnmsubs: -(q * band - d)
    band = std::fmaf(high, f, band);               // fmadds
    low = std::fmaf(band, f, low);                 // fmadds
    const float out = mode == 0 ? low : mode == 1 ? band : mode == 2 ? high : low + high;
    sf(dst + i * 4, out);
  }
  sf(p_low, low);
  sf(p_band, band);
}

// sub_8312B178  the same filter for interleaved stereo (Singularity's sub_828EE028): the same parameters and six modes,
//               the low and band states two floats each (left, right), ((count - 1) / 2) + 1 sample pairs, as the
//               PowerPC loop counts them.
REX_EXTERN(__imp__sub_8312B178);
REX_HOOK_RAW(sub_8312B178) {
  if (!REXCVAR_GET(deadpool_audio_filtro_nativo)) {
    __imp__sub_8312B178(ctx, base);
    return;
  }
  auto u32 = [&](uint32_t a) { return rex::memory::load_and_swap<uint32_t>(base + a); };
  auto lf = [&](uint32_t a) { return std::bit_cast<float>(u32(a)); };
  auto sf = [&](uint32_t a, float v) { rex::memory::store_and_swap<uint32_t>(base + a, std::bit_cast<uint32_t>(v)); };
  const uint32_t p = ctx.r3.u32;
  const uint32_t src = u32(p + 0), dst = u32(p + 4), p_low = u32(p + 8), p_band = u32(p + 12), n = u32(p + 16);
  const uint32_t mode = u32(p + 24);
  const float f = lf(p + 28), q = lf(p + 32);
  float low[2] = {lf(p_low), lf(p_low + 4)}, band[2] = {lf(p_band), lf(p_band + 4)};
  const uint32_t pares = n && mode <= 5 ? ((n - 1) >> 1) + 1 : 0;
  const float k = 1.0f - f;
  for (uint32_t i = 0; i < pares; ++i) {
    for (uint32_t c = 0; c < 2; ++c) {
      const uint32_t at = (i * 2 + c) * 4;
      const float x = lf(src + at);
      if (mode >= 4) {  // one pole
        low[c] = std::fmaf(x, f, k * low[c]);
        sf(dst + at, mode == 4 ? low[c] : x - low[c]);
        continue;
      }
      const float high = std::fmaf(-q, band[c], x - low[c]);
      band[c] = std::fmaf(high, f, band[c]);
      low[c] = std::fmaf(band[c], f, low[c]);
      sf(dst + at, mode == 0 ? low[c] : mode == 1 ? band[c] : mode == 2 ? high : low[c] + high);
    }
  }
  sf(p_low, low[0]);
  sf(p_low + 4, low[1]);
  sf(p_band, band[0]);
  sf(p_band + 4, band[1]);
}
