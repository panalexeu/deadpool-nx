// Merge DKSH caches (rex::deko3d::CacheDksh format, "NFDK" version 2) into one: dksh_merge OUT IN1 [IN2...]
// When two inputs hold the same key, the earlier input wins. All inputs must come from the same compiler (the id
// hash at offset 8). shaders.sh merges the variants retried with a longer limit (nfsmw_dksh.bin) into
// shadercache_dksh.bin: the console rewrites cache/nfsmw_dksh.bin with only what it compiled itself, so a PC-made file
// put there does not survive a session.
#define XXH_INLINE_ALL
#include <xxhash.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  uint64_t huella;
  uint32_t espec, variante, bytes;
  const uint8_t* datos;
  size_t orden;  // input order, so the earlier input wins a duplicate key
} Entrada;

static uint8_t* Leer(const char* ruta, size_t* n) {
  FILE* f = fopen(ruta, "rb");
  if (!f) { perror(ruta); exit(1); }
  fseek(f, 0, SEEK_END);
  *n = (size_t)ftell(f);
  rewind(f);
  uint8_t* d = malloc(*n ? *n : 1);
  if (fread(d, 1, *n, f) != *n) { perror(ruta); exit(1); }
  fclose(f);
  return d;
}

static int Clave(const Entrada* x, const Entrada* y) {
  if (x->huella != y->huella) return x->huella < y->huella ? -1 : 1;
  if (x->espec != y->espec) return x->espec < y->espec ? -1 : 1;
  if (x->variante != y->variante) return x->variante < y->variante ? -1 : 1;
  return 0;
}
static int Comparar(const void* a, const void* b) {
  const Entrada *x = a, *y = b;
  const int c = Clave(x, y);
  return c ? c : (x->orden < y->orden ? -1 : x->orden > y->orden);
}

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: %s OUT IN1 [IN2...]\n", argv[0]); return 2; }
  Entrada* e = NULL;
  size_t total = 0, cap = 0;
  uint64_t id = 0;
  for (int i = 2; i < argc; i++) {
    size_t n;
    const uint8_t* d = Leer(argv[i], &n);
    uint32_t magia = 0, version = 0, cuenta = 0;
    uint64_t idi = 0, integridad = 0;
    if (n >= 32) {
      memcpy(&magia, d, 4); memcpy(&version, d + 4, 4); memcpy(&idi, d + 8, 8);
      memcpy(&cuenta, d + 16, 4); memcpy(&integridad, d + 24, 8);
    }
    if (magia != 0x4B44464Eu || version != 2) { fprintf(stderr, "%s: not a DKSH cache\n", argv[i]); return 1; }
    if (i == 2) id = idi;
    else if (idi != id) { fprintf(stderr, "%s: made by another compiler\n", argv[i]); return 1; }
    if (32 + (size_t)cuenta * 24 > n || XXH3_64bits(d + 32, n - 32) != integridad) {
      fprintf(stderr, "%s: damaged\n", argv[i]);
      return 1;
    }
    size_t datos = 32 + (size_t)cuenta * 24;
    for (uint32_t k = 0; k < cuenta; k++) {
      const uint8_t* t = d + 32 + (size_t)k * 24;
      if (total == cap) { cap = cap ? cap * 2 : (size_t)1 << 17; e = realloc(e, cap * sizeof(Entrada)); }
      Entrada* x = &e[total];
      memcpy(&x->huella, t, 8); memcpy(&x->espec, t + 8, 4); memcpy(&x->variante, t + 12, 4); memcpy(&x->bytes, t + 16, 4);
      if (datos + x->bytes > n) { fprintf(stderr, "%s: damaged\n", argv[i]); return 1; }
      x->datos = d + datos;
      x->orden = total++;
      datos = (datos + x->bytes + 3) & ~(size_t)3;
    }
    printf("%s: %u entries\n", argv[i], cuenta);
  }
  qsort(e, total, sizeof(Entrada), Comparar);
  size_t unicas = 0;
  for (size_t k = 0; k < total; k++) {
    if (unicas && !Clave(&e[k], &e[unicas - 1])) continue;
    e[unicas++] = e[k];
  }
  size_t tam = 32 + unicas * 24;
  for (size_t k = 0; k < unicas; k++) tam = (tam + e[k].bytes + 3) & ~(size_t)3;
  uint8_t* o = calloc(tam, 1);
  const uint32_t cabecera[2] = {0x4B44464Eu, 2};
  const uint32_t n32 = (uint32_t)unicas;
  memcpy(o, cabecera, 8); memcpy(o + 8, &id, 8); memcpy(o + 16, &n32, 4);
  size_t datos = 32 + unicas * 24;
  for (size_t k = 0; k < unicas; k++) {
    uint8_t* t = o + 32 + k * 24;
    memcpy(t, &e[k].huella, 8); memcpy(t + 8, &e[k].espec, 4); memcpy(t + 12, &e[k].variante, 4);
    memcpy(t + 16, &e[k].bytes, 4);
    memcpy(o + datos, e[k].datos, e[k].bytes);
    datos = (datos + e[k].bytes + 3) & ~(size_t)3;
  }
  const uint64_t integridad = XXH3_64bits(o + 32, tam - 32);
  memcpy(o + 24, &integridad, 8);
  FILE* f = fopen(argv[1], "wb");
  if (!f || fwrite(o, 1, tam, f) != tam || fclose(f)) { perror(argv[1]); return 1; }
  printf("%s: %zu entries (%zu duplicates dropped), %zu bytes\n", argv[1], unicas, total - unicas, tam);
  return 0;
}
