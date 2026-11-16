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
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/system/lzx.h>

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

// The Xbox SDK's own memory routines and Direct3D's shader-constant copy, as native code (perf_report.py, dense
// scenes: ~1-2 % of the main, worker and render threads). Each is exact by construction (a copy, a fill); the steps
// they read from game data (1 byte per step in the copy's tail, 16 bytes per step in the fill) are checked first, and
// anything unexpected (another step value, overlapping copy ranges) runs the original.
//
//   sub_82D71990  copy: r3 destination, r4 source, r5 bytes; answers r3 (8/4-byte and vector paths, a byte tail
//                 stepping by the word at 0x8359B3E0, dcbt prefetches).
//   sub_82D71E58  fill: r3 destination, r4 byte, r5 bytes (vector stores stepping by the word at 0x8359B3E4, dcbzl
//                 for zero).
//   sub_82D9C6A8  Direct3D's constant upload: r6 16-byte constants from r5 (any alignment) to r3 + (r4 + 120) * 16,
//                 then the 64-bit word at r3 |= r7.
//
// deadpool_cpu_nativo_comprobar: the first 2000 calls of each also run the original on the same memory and compare
// the bytes (the original's result is kept); "[nativo]" lines report the count and any difference.
REXCVAR_DEFINE_BOOL(deadpool_cpu_nativo_xdk, true, "Deadpool",
                    "The Xbox SDK's memory copy and fill (sub_82D71990, sub_82D71E58) and Direct3D's shader-constant "
                    "copy (sub_82D9C6A8) as native code");
REXCVAR_DEFINE_BOOL(deadpool_cpu_nativo_comprobar, false, "Deadpool",
                    "Check the native versions above against the originals on their first 2000 calls each (slower "
                    "while it lasts; '[nativo]' log lines)");

namespace {
uint32_t U32Be(const uint8_t* base, uint32_t a) { return rex::memory::load_and_swap<uint32_t>(base + a); }

// Once per session: the step words the copy and the fill read, so the log says whether the native versions run.
void InformarPasos(const uint8_t* base) {
  static std::atomic<bool> hecho{false};
  if (!hecho.exchange(true)) {
    REXLOG_INFO("[nativo] SDK copy/fill steps in game data: {} and {} (native when 1 and 16)", U32Be(base, 0x8359B3E0),
                U32Be(base, 0x8359B3E4));
  }
}

struct Comprobacion {
  const char* nombre;
  std::atomic<uint32_t> llamadas{0}, diferencias{0};
  std::atomic<bool> informado{false};
};
Comprobacion g_comp_copia{"copia (sub_82D71990)"}, g_comp_relleno{"relleno (sub_82D71E58)"},
    g_comp_constantes{"constantes D3D (sub_82D9C6A8)"};

// Runs `original` and `nativo` on the same `bytes` at `destino` and compares; the original's bytes are what remains.
template <class O, class N>
void Comprobar(Comprobacion& c, uint8_t* destino, uint32_t bytes, O original, N nativo) {
  const uint32_t n = c.llamadas.fetch_add(1, std::memory_order_relaxed);
  if (n >= 2000) {
    nativo();
    if (!c.informado.exchange(true)) {
      REXLOG_INFO("[nativo] {}: 2000 calls checked against the original, {} differed", c.nombre,
                  c.diferencias.load());
    }
    return;
  }
  std::vector<uint8_t> antes(destino, destino + bytes);
  original();
  std::vector<uint8_t> ref(destino, destino + bytes);
  std::memcpy(destino, antes.data(), bytes);
  nativo();
  if (std::memcmp(destino, ref.data(), bytes) != 0) {
    if (c.diferencias.fetch_add(1) < 8) REXLOG_WARN("[nativo] {}: DIFFERS on a call of {} bytes", c.nombre, bytes);
    std::memcpy(destino, ref.data(), bytes);
  }
}
}  // namespace

REX_EXTERN(__imp__sub_82D71990);
REX_HOOK_RAW(sub_82D71990) {
  const uint32_t d = ctx.r3.u32, s = ctx.r4.u32, n = ctx.r5.u32;
  InformarPasos(base);
  if (!REXCVAR_GET(deadpool_cpu_nativo_xdk) || U32Be(base, 0x8359B3E0) != 1 || (n && d < s + n && s < d + n)) {
    __imp__sub_82D71990(ctx, base);
    return;
  }
  auto nativo = [&] {
    if (n) std::memcpy(base + d, base + s, n);
    ctx.r3.u64 = d;
  };
  if (REXCVAR_GET(deadpool_cpu_nativo_comprobar) && n) {
    Comprobar(g_comp_copia, base + d, n, [&] { __imp__sub_82D71990(ctx, base); }, nativo);
    ctx.r3.u64 = d;
  } else {
    nativo();
  }
}

REX_EXTERN(__imp__sub_82D71E58);
REX_HOOK_RAW(sub_82D71E58) {
  const uint32_t d = ctx.r3.u32, n = ctx.r5.u32;
  const int byte = int(ctx.r4.u32 & 0xFF);
  if (!REXCVAR_GET(deadpool_cpu_nativo_xdk) || U32Be(base, 0x8359B3E4) != 16) {
    __imp__sub_82D71E58(ctx, base);
    return;
  }
  auto nativo = [&] {
    if (n) std::memset(base + d, byte, n);
  };
  if (REXCVAR_GET(deadpool_cpu_nativo_comprobar) && n) {
    const uint64_t r3 = ctx.r3.u64;
    Comprobar(g_comp_relleno, base + d, n, [&] { __imp__sub_82D71E58(ctx, base); }, nativo);
    ctx.r3.u64 = r3;
  } else {
    nativo();
  }
}

REX_EXTERN(__imp__sub_82D9C6A8);
REX_HOOK_RAW(sub_82D9C6A8) {
  const uint32_t dispositivo = ctx.r3.u32, inicio = ctx.r4.u32, fuente = ctx.r5.u32, cuantos = ctx.r6.u32;
  const uint32_t destino = dispositivo + ((inicio + 120) << 4);
  const uint32_t bytes = cuantos * 16;
  if (!REXCVAR_GET(deadpool_cpu_nativo_xdk) || (fuente < destino + bytes && destino < fuente + bytes)) {
    __imp__sub_82D9C6A8(ctx, base);
    return;
  }
  const uint64_t marca = ctx.r7.u64;
  auto nativo = [&] {
    if (bytes) std::memcpy(base + destino, base + fuente, bytes);
    const uint64_t m = rex::memory::load_and_swap<uint64_t>(base + dispositivo) | marca;
    rex::memory::store_and_swap<uint64_t>(base + dispositivo, m);
  };
  if (REXCVAR_GET(deadpool_cpu_nativo_comprobar) && destino == dispositivo + ((inicio + 120) << 4) &&
      destino >= dispositivo + 8) {
    // Two regions change: the 8-byte mask word and the constants; checked as one span from the word.
    const uint32_t tramo = destino + bytes - dispositivo;
    Comprobar(g_comp_constantes, base + dispositivo, tramo, [&] { __imp__sub_82D9C6A8(ctx, base); }, nativo);
  } else {
    nativo();
  }
}

// sub_823BB2B8  the game's LZX chunk decompression (r3 destination, r4 its size, r5 source, r6 source size; answers 1
//               when the whole destination was produced): an Xbox SDK LZX context with default parameters (128 KB
//               window), XMemDecompress(ctx, dest, &size, src, srcSize - 4), the context destroyed. Natively with the
//               SDK's libmspack LZX decoder (lzx_decompress). Package streaming ran it on a loader thread during the
//               fps dips at a level's start (the recompiled bit decoder sub_82D8FC80). A native failure runs the
//               original; deadpool_cpu_nativo_comprobar checks the first 300 chunks byte for byte (original kept).
REXCVAR_DEFINE_BOOL(deadpool_lzx_nativo, true, "Deadpool",
                    "The game's LZX chunk decompression (sub_823BB2B8) with the SDK's native libmspack decoder");

namespace {
Comprobacion g_comp_lzx{"LZX (sub_823BB2B8)"};
std::atomic<uint32_t> g_lzx_fallos{0};

// 1 when the whole destination was produced natively, 0 on any failure (the caller then runs the original).
// XMemDecompress's data is framed: each 32 KB output frame starts with its compressed size (big-endian 16 bits),
// preceded by 0xFF and the frame's uncompressed size when that is not 32 KB; libmspack wants the LZX payloads alone,
// one after the other (checked on the PC: 1,130 chunks from 400 of the game's packages decode to their sizes).
bool LzxNativo(uint8_t* base, uint32_t destino, uint32_t tam_destino, uint32_t fuente, uint32_t tam_fuente) {
  if (tam_fuente < 4) return false;
  const uint8_t* in = base + fuente;
  const size_t n = tam_fuente - 4;
  thread_local std::vector<uint8_t> carga;
  carga.clear();
  carga.reserve(n);
  for (size_t i = 0; i + 2 <= n;) {
    size_t c;
    if (in[i] == 0xFF) {
      if (i + 5 > n) break;
      c = (size_t(in[i + 3]) << 8) | in[i + 4];
      i += 5;
    } else {
      c = (size_t(in[i]) << 8) | in[i + 1];
      i += 2;
    }
    if (!c || i + c > n) break;
    carga.insert(carga.end(), in + i, in + i + c);
    i += c;
  }
  if (carga.empty()) return false;
  return lzx_decompress(carga.data(), carga.size(), base + destino, tam_destino, 0x20000, nullptr, 0) == 0;
}
}  // namespace

REX_EXTERN(__imp__sub_823BB2B8);
REX_HOOK_RAW(sub_823BB2B8) {
  const uint32_t destino = ctx.r3.u32, tam_destino = ctx.r4.u32, fuente = ctx.r5.u32, tam_fuente = ctx.r6.u32;
  if (!REXCVAR_GET(deadpool_lzx_nativo) || !destino || !tam_destino || !fuente) {
    __imp__sub_823BB2B8(ctx, base);
    return;
  }
  if (REXCVAR_GET(deadpool_cpu_nativo_comprobar) && g_comp_lzx.llamadas.load(std::memory_order_relaxed) < 300) {
    const uint32_t n = g_comp_lzx.llamadas.fetch_add(1);
    std::vector<uint8_t> antes(base + destino, base + destino + tam_destino);
    __imp__sub_823BB2B8(ctx, base);
    const uint64_t resultado = ctx.r3.u64;
    std::vector<uint8_t> ref(base + destino, base + destino + tam_destino);
    std::memcpy(base + destino, antes.data(), tam_destino);
    const bool ok = LzxNativo(base, destino, tam_destino, fuente, tam_fuente);
    const bool igual = ok == (resultado == 1) && (!ok || std::memcmp(base + destino, ref.data(), tam_destino) == 0);
    if (!igual && g_comp_lzx.diferencias.fetch_add(1) < 8) {
      REXLOG_WARN("[nativo] {}: DIFFERS on a chunk of {} -> {} bytes (native {}, original {})", g_comp_lzx.nombre,
                  tam_fuente, tam_destino, ok ? "ok" : "failed", resultado);
    }
    std::memcpy(base + destino, ref.data(), tam_destino);
    ctx.r3.u64 = resultado;
    if (n + 1 == 300) {
      REXLOG_INFO("[nativo] {}: 300 chunks checked against the original, {} differed", g_comp_lzx.nombre,
                  g_comp_lzx.diferencias.load());
    }
    return;
  }
  if (LzxNativo(base, destino, tam_destino, fuente, tam_fuente)) {
    ctx.r3.u64 = 1;
    return;
  }
  if (g_lzx_fallos.fetch_add(1) < 8) {
    REXLOG_WARN("[nativo] LZX: native decode failed on a chunk of {} -> {} bytes; the original decodes it", tam_fuente,
                tam_destino);
  }
  __imp__sub_823BB2B8(ctx, base);
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
