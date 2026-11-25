#include <cassert>
#include <cstdlib>
#include <string_view>

#include "specialproxy/proxy/core.hpp"

int main() {
  const std::string_view v = spx::version();
  assert(!v.empty());
  // Expect "major.minor.patch"
  assert(v.find('.') != std::string_view::npos);
  return EXIT_SUCCESS;
}
