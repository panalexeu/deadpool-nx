#define XXH_INLINE_ALL
#include <xxhash.h>
#include <cstdio>
#include <cstdlib>
#include "ds_shader_library.h"
int main(int argc, char** argv) {
  ds::BibliotecaShaders b; b.CargarIndice(argv[1]);
  for (int i = 2; i < argc; ++i) { size_t n = std::strtoul(argv[i], nullptr, 10); auto s = b.LeerSpirv(n);
    std::printf("n%zu %016llX\n", n, (unsigned long long)XXH3_64bits(s.data(), s.size() * 4)); }
}
