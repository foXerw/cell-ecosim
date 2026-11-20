// Minimal self-registering test harness. No external framework on purpose:
// the native side must build with only g++ + CMake (see docs/building.md).
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace ecosim::test {

struct Case {
  const char* name;
  void (*fn)();
};

inline std::vector<Case>& cases() {
  static std::vector<Case> v;
  return v;
}

inline int& check_failures() {
  static int n = 0;
  return n;
}

struct Register {
  Register(const char* name, void (*fn)()) { cases().push_back(Case{name, fn}); }
};

inline int report(const char* file, int line, const std::string& what) {
  ++check_failures();
  std::printf("    FAIL %s:%d: %s\n", file, line, what.c_str());
  return 0;
}

inline int run_all() {
  int failed_cases = 0;
  for (const Case& c : cases()) {
    const int before = check_failures();
    c.fn();
    const bool ok = (check_failures() == before);
    if (!ok) ++failed_cases;
    std::printf("%s %s\n", ok ? "  ok  " : " FAIL ", c.name);
  }
  std::printf("\n%zu cases, %d failed (%d checks failed)\n", cases().size(), failed_cases,
               check_failures());
  return failed_cases == 0 ? 0 : 1;
}

}  // namespace ecosim::test

#define ECOSIM_TEST(name)                                                    \
  static void name();                                                        \
  static ::ecosim::test::Register ecosim_reg_##name(#name, &name);           \
  static void name()

#define CHECK_TRUE(cond)                                                    \
  do {                                                                      \
    if (!(cond)) ::ecosim::test::report(__FILE__, __LINE__, "expected true: " #cond); \
  } while (0)

#define CHECK_EQ_i32(expected, actual)                                                       \
  do {                                                                                       \
    const int32_t e_ = (int32_t)(expected);                                                   \
    const int32_t a_ = (int32_t)(actual);                                                     \
    if (e_ != a_)                                                                             \
      ::ecosim::test::report(__FILE__, __LINE__,                                             \
                             std::string(#actual) + " == " + std::to_string(a_) + ", want " + \
                                 std::to_string(e_));                                        \
  } while (0)

#define CHECK_EQ_i64(expected, actual)                                                       \
  do {                                                                                       \
    const int64_t e_ = (int64_t)(expected);                                                   \
    const int64_t a_ = (int64_t)(actual);                                                     \
    if (e_ != a_)                                                                             \
      ::ecosim::test::report(__FILE__, __LINE__,                                             \
                             std::string(#actual) + " == " + std::to_string(a_) + ", want " + \
                                 std::to_string(e_));                                        \
  } while (0)
