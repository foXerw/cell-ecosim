#include <cstdio>

#include "check.h"

int main() {
  // Unbuffered, so a hang or a crash still shows which case it died on.
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  return ::ecosim::test::run_all();
}
