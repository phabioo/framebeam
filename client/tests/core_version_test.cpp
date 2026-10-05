#include <cstdio>

#include "version.h"

int main() {
  if (framebeam::playerVersion().empty()) {
    std::fprintf(stderr, "playerVersion() ist leer\n");
    return 1;
  }
  return 0;
}
