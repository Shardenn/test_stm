#include "test_framework.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#ifdef _WIN32
#define TF_POPEN _popen
#define TF_PCLOSE _pclose
#else
#include <sys/wait.h>
#define TF_POPEN popen
#define TF_PCLOSE pclose
#endif

namespace tf {
namespace {

struct TestInfo {
  const char* name;
  const char* requirement;
  TestFn fn;
  unsigned flags;
  const char* note;
};

std::vector<TestInfo>& registry() {
  static std::vector<TestInfo> tests;
  return tests;
}

int g_failures = 0;

int runChild(const std::string& command, std::string& output) {
#ifdef _WIN32
  // cmd.exe /c strips one pair of outer quotes.
  const std::string full = "\"" + command + " 2>&1\"";
#else
  const std::string full = command + " 2>&1";
#endif
  FILE* pipe = TF_POPEN(full.c_str(), "r");
  if (!pipe) return -1;
  char buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, pipe)) > 0) output.append(buf, n);
  int rc = TF_PCLOSE(pipe);
#ifndef _WIN32
  if (WIFEXITED(rc)) rc = WEXITSTATUS(rc);
#endif
  return rc;
}

std::string quote(const std::string& s) { return "\"" + s + "\""; }

[[noreturn]] void finish(int code) {
  std::fflush(stdout);
  std::fflush(stderr);
  std::cout.flush();
  // The firmware thread never terminates; skip static destruction.
  std::_Exit(code);
}

int runOne(const char* name) {
  for (const TestInfo& t : registry()) {
    if (std::strcmp(t.name, name) != 0) continue;
    try {
      t.fn();
    } catch (const AbortTest&) {
    }
    return g_failures == 0 ? 0 : 1;
  }
  std::cerr << "unknown test: " << name << "\n";
  return 2;
}

int runAll(const char* self, const char* variant, bool isReference) {
  int passed = 0, failed = 0, skipped = 0;
  std::cout << "Firmware variant: " << variant << "\n";
  for (const TestInfo& t : registry()) {
    if (isReference && (t.flags & kRewriteOnly)) {
      std::cout << "[SKIP] " << t.name << "  (" << t.requirement << ")  -- " << t.note << "\n";
      ++skipped;
      continue;
    }
    std::string out;
    const int rc = runChild(quote(self) + " --run " + t.name, out);
    if (rc == 0) {
      std::cout << "[PASS] " << t.name << "  (" << t.requirement << ")\n";
      ++passed;
    } else {
      std::cout << "[FAIL] " << t.name << "  (" << t.requirement << ")  exit=" << rc << "\n" << out;
      ++failed;
    }
  }
  std::cout << "\n" << passed << " passed, " << failed << " failed, " << skipped << " skipped\n";
  return failed == 0 ? 0 : 1;
}

std::vector<std::string> splitLines(const std::string& s) {
  std::vector<std::string> lines;
  size_t pos = 0;
  while (pos < s.size()) {
    size_t e = s.find('\n', pos);
    if (e == std::string::npos) e = s.size();
    std::string l = s.substr(pos, e - pos);
    if (!l.empty() && l.back() == '\r') l.pop_back();
    lines.push_back(l);
    pos = e + 1;
  }
  return lines;
}

int runDiff(const char* self, const char* other, unsigned count, unsigned first) {
  unsigned mismatches = 0;
  for (unsigned seed = first; seed < first + count; ++seed) {
    std::string a, b;
    const int ra = runChild(quote(self) + " --trace " + std::to_string(seed), a);
    const int rb = runChild(quote(other) + " --trace " + std::to_string(seed), b);
    if (ra == 0 && rb == 0 && a == b) continue;
    ++mismatches;
    std::cout << "[DIFF] seed " << seed << " (exit codes " << ra << " / " << rb << ")\n";
    const auto la = splitLines(a), lb = splitLines(b);
    for (size_t i = 0; i < std::max(la.size(), lb.size()); ++i) {
      const std::string x = i < la.size() ? la[i] : "<missing>";
      const std::string y = i < lb.size() ? lb[i] : "<missing>";
      if (x != y) {
        std::cout << "  first difference at trace line " << i + 1 << ":\n"
                  << "    this : " << x << "\n    other: " << y << "\n";
        break;
      }
    }
  }
  std::cout << count - mismatches << "/" << count << " fuzz scenarios produced identical traces\n";
  return mismatches == 0 ? 0 : 1;
}

}  // namespace

Registrar::Registrar(const char* name, const char* requirement, TestFn fn, unsigned flags, const char* note) {
  registry().push_back({name, requirement, fn, flags, note});
}

void reportFailure(const char* file, int line, const std::string& message) {
  ++g_failures;
  std::cout << "    " << file << ":" << line << ": " << message << "\n";
}

void log(const std::string& message) { std::cout << "    " << message << "\n"; }

int main(int argc, char** argv, const char* variant, bool isReference, TraceFn trace) {
  const char* self = argv[0];
  if (argc == 1) finish(runAll(self, variant, isReference));
  const std::string mode = argv[1];
  if (mode == "--list") {
    for (const TestInfo& t : registry()) std::cout << t.name << "\t" << t.requirement << "\n";
    finish(0);
  }
  if (mode == "--run" && argc == 3) finish(runOne(argv[2]));
  if (mode == "--trace" && argc == 3 && trace) {
    trace(static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10)));
    finish(0);
  }
  if (mode == "--diff" && argc >= 4) {
    const unsigned count = static_cast<unsigned>(std::strtoul(argv[3], nullptr, 10));
    const unsigned first = argc >= 5 ? static_cast<unsigned>(std::strtoul(argv[4], nullptr, 10)) : 1;
    finish(runDiff(self, argv[2], count, first));
  }
  std::cerr << "usage: " << self << " [--list | --run <test> | --trace <seed> | --diff <exe> <count> [first]]\n";
  finish(2);
}

}  // namespace tf
