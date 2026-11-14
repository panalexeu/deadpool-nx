// XXH3_64 of each file, printed as "<16 hex digits> <path>": the SPIR-V fingerprint (HuellaSpirv) the DKSH cache keys
// its variants by. shaders.sh uses it to list the variants the full build failed, for ds_dksh --variantes.
#define XXH_INLINE_ALL
#include <xxhash.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char** argv) {
  for (int i = 1; i < argc; i++) {
    FILE* f = fopen(argv[i], "rb");
    if (!f) { perror(argv[i]); return 1; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char* b = malloc(n ? n : 1);
    if (fread(b, 1, n, f) != (size_t)n) { perror(argv[i]); return 1; }
    fclose(f);
    printf("%016llx %s\n", (unsigned long long)XXH3_64bits(b, n), argv[i]);
    free(b);
  }
  return 0;
}
