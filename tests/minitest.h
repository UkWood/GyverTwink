// Минимальный тестовый каркас без внешних зависимостей (чтобы тесты собирались одним g++)
#pragma once
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

struct TestCase {
  const char* name;
  void (*fn)();
};
inline std::vector<TestCase>& testRegistry() {
  static std::vector<TestCase> r;
  return r;
}
struct TestReg {
  TestReg(const char* n, void (*f)()) { testRegistry().push_back({n, f}); }
};
inline int& testFailures() {
  static int f = 0;
  return f;
}

#define TEST(name) \
  static void name(); \
  static TestReg reg_##name(#name, name); \
  static void name()

#define CHECK(cond) \
  do { \
    if (!(cond)) { \
      std::printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      testFailures()++; \
    } \
  } while (0)

#define CHECK_EQ(a, b) \
  do { \
    long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { \
      std::printf("    FAIL %s:%d: %s == %s (%lld != %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); \
      testFailures()++; \
    } \
  } while (0)

inline int runAllTests() {
  int failedTests = 0;
  for (auto& t : testRegistry()) {
    int before = testFailures();
    std::printf("[ RUN  ] %s\n", t.name);
    t.fn();
    bool ok = testFailures() == before;
    if (!ok) failedTests++;
    std::printf("[ %s ] %s\n", ok ? " OK " : "FAIL", t.name);
  }
  std::printf("\n%zu tests, %d failed\n", testRegistry().size(), failedTests);
  return failedTests ? 1 : 0;
}
