#include <cstdio>

#include "version.h"

int main() {
  if (framebeam::playerVersion().empty()) {
    std::fprintf(stderr, "playerVersion() is empty\n");
    return 1;
  }
  return 0;
}
