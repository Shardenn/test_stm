// Minimal, dependency-free test framework.
//
// Every emulator test boots the firmware, and the firmware keeps global state
// and never returns from main(), so each test runs in its own child process:
// the executable re-invokes itself with "--run <test>".
#pragma once

#include <cstdint>
#include <sstream>
#include <string>

namespace tf {

enum Flags : unsigned {
  kNone = 0,
  // The test checks behaviour that deliberately differs from the original C
  // firmware (a bug fix); it is skipped for the reference build.
  kRewriteOnly = 1u << 0,
};

using TestFn = void (*)();

struct Registrar {
  Registrar(const char* name, const char* requirement, TestFn fn, unsigned flags, const char* note);
};

struct AbortTest {};

void reportFailure(const char* file, int line, const std::string& message);
void log(const std::string& message);

// Entry point shared by all test executables.
//   (no args)                    run every test, each in a child process
//   --list                       list tests
//   --run <name>                 run one test in this process
//   --trace <seed>               print the trace of fuzz scenario <seed>
//   --diff <exe> <count> [first] compare fuzz traces of this exe and <exe>
using TraceFn = void (*)(uint32_t seed);  // prints the trace of one fuzz scenario
int main(int argc, char** argv, const char* variant, bool isReference, TraceFn trace);

template <typename A, typename B>
std::string describe(const char* ea, const char* eb, const A& a, const B& b) {
  std::ostringstream os;
  os << ea << " == " << eb << "  (" << a << " vs " << b << ")";
  return os.str();
}

}  // namespace tf

#define TF_CAT2(a, b) a##b
#define TF_CAT(a, b) TF_CAT2(a, b)

#define TEST_CASE_EX(id, requirement, flags, note)                                    \
  static void id();                                                                   \
  static ::tf::Registrar TF_CAT(id, _registrar)(#id, requirement, &id, flags, note); \
  static void id()

#define TEST_CASE(id, requirement) TEST_CASE_EX(id, requirement, ::tf::kNone, "")

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) ::tf::reportFailure(__FILE__, __LINE__, "CHECK(" #cond ")"); \
  } while (0)

#define CHECK_MSG(cond, msg)                                                         \
  do {                                                                               \
    if (!(cond)) {                                                                   \
      std::ostringstream tf_os_;                                                     \
      tf_os_ << "CHECK(" #cond ") " << msg;                                          \
      ::tf::reportFailure(__FILE__, __LINE__, tf_os_.str());                         \
    }                                                                                \
  } while (0)

#define CHECK_EQ(a, b)                                                                        \
  do {                                                                                        \
    const auto& tf_a_ = (a);                                                                  \
    const auto& tf_b_ = (b);                                                                  \
    if (!(tf_a_ == tf_b_)) ::tf::reportFailure(__FILE__, __LINE__, ::tf::describe(#a, #b, tf_a_, tf_b_)); \
  } while (0)

#define CHECK_BETWEEN(v, lo, hi)                                                         \
  do {                                                                                   \
    const auto tf_v_ = (v);                                                              \
    if (!(tf_v_ >= (lo) && tf_v_ <= (hi))) {                                             \
      std::ostringstream tf_os_;                                                         \
      tf_os_ << #v " = " << tf_v_ << " not in [" << (lo) << ", " << (hi) << "]";         \
      ::tf::reportFailure(__FILE__, __LINE__, tf_os_.str());                             \
    }                                                                                    \
  } while (0)

#define REQUIRE(cond)                                                          \
  do {                                                                         \
    if (!(cond)) {                                                             \
      ::tf::reportFailure(__FILE__, __LINE__, "REQUIRE(" #cond ")");          \
      throw ::tf::AbortTest{};                                                 \
    }                                                                          \
  } while (0)
