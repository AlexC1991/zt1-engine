// ours: see SampleProfiler.hpp
#include "SampleProfiler.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <timeapi.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "winmm.lib")

namespace {
constexpr int kDepth = 64;

// The suspended thread's stack, from its context: no locks or allocation
// while it is stopped (it may hold the heap's)
int walk(CONTEXT ctx, DWORD64 *out) {
  int n = 0;
  while (n < kDepth && ctx.Rip) {
    out[n++] = ctx.Rip;
    DWORD64 base = 0;
    PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &base, nullptr);
    if (!fn) {
      // (a leaf: its return address on top of the stack)
      if (!ctx.Rsp)
        break;
      ctx.Rip = *reinterpret_cast<DWORD64 *>(ctx.Rsp);
      ctx.Rsp += 8;
      continue;
    }
    void *handler = nullptr;
    DWORD64 frame = 0;
    RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, ctx.Rip, fn, &ctx, &handler, &frame, nullptr);
  }
  return n;
}

void run(HANDLE target, double delay, double seconds, std::string out) {
  std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(delay * 1000)));
  timeBeginPeriod(1);
  std::vector<std::vector<DWORD64>> stacks;
  stacks.reserve(static_cast<size_t>(seconds * 1100));
  DWORD64 frames[kDepth];
  const ULONGLONG end = GetTickCount64() + static_cast<ULONGLONG>(seconds * 1000);
  while (GetTickCount64() < end) {
    if (SuspendThread(target) != static_cast<DWORD>(-1)) {
      CONTEXT ctx{};
      ctx.ContextFlags = CONTEXT_FULL;
      int n = GetThreadContext(target, &ctx) ? walk(ctx, frames) : 0;
      ResumeThread(target);
      if (n)
        stacks.emplace_back(frames, frames + n);
    }
    Sleep(1);
  }
  timeEndPeriod(1);

  HANDLE process = GetCurrentProcess();
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
  SymInitialize(process, nullptr, TRUE);
  std::unordered_map<DWORD64, std::string> names;
  auto nameOf = [&](DWORD64 a) -> const std::string & {
    auto it = names.find(a);
    if (it != names.end())
      return it->second;
    char buffer[sizeof(SYMBOL_INFO) + 512] = {};
    auto *sym = reinterpret_cast<SYMBOL_INFO *>(buffer);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 511;
    std::string module = "?";
    IMAGEHLP_MODULE64 mod{};
    mod.SizeOfStruct = sizeof(mod);
    if (SymGetModuleInfo64(process, a, &mod))
      module = mod.ModuleName;
    DWORD64 displacement = 0;
    std::string name = SymFromAddr(process, a, &displacement, sym) ? module + "!" + sym->Name : module + "!?";
    return names[a] = name;
  };
  std::map<std::string, size_t> self, total, modules;
  for (const auto &s : stacks) {
    const std::string &top = nameOf(s[0]);
    self[top]++;
    modules[top.substr(0, top.find('!'))]++;
    std::set<std::string> seen;
    for (DWORD64 a : s)
      if (seen.insert(nameOf(a)).second)
        total[nameOf(a)]++;
  }
  SymCleanup(process);
  auto ranked = [](const std::map<std::string, size_t> &m) {
    std::vector<std::pair<size_t, std::string>> v;
    for (const auto &[k, n] : m)
      v.push_back({n, k});
    std::sort(v.rbegin(), v.rend());
    return v;
  };
  std::ofstream f(out);
  const double all = std::max<size_t>(1, stacks.size());
  f << "samples " << stacks.size() << "\n\n== by module (own time)\n";
  for (const auto &[n, k] : ranked(modules))
    f << n * 100.0 / all << "%\t" << k << "\n";
  f << "\n== own time\n";
  int rows = 0;
  for (const auto &[n, k] : ranked(self))
    if (rows++ < 80)
      f << n * 100.0 / all << "%\t" << k << "\n";
  f << "\n== with what it calls\n";
  rows = 0;
  for (const auto &[n, k] : ranked(total))
    if (rows++ < 150)
      f << n * 100.0 / all << "%\t" << k << "\n";
  f.close();
  CloseHandle(target);
}
} // namespace

void SampleProfiler::startFromEnvironment() {
  const char *seconds = std::getenv("ZT_SAMPLE_PROFILE");
  if (!seconds || !*seconds)
    return;
  const char *delay = std::getenv("ZT_SAMPLE_DELAY");
  std::string out;
  if (const char *o = std::getenv("ZT_SAMPLE_PROFILE_OUT"))
    out = o;
  else if (const char *b = std::getenv("ZT_GAME_BENCH_OUT"))
    out = std::string(b) + "/samples.txt";
  else
    out = "samples.txt";
  HANDLE self = nullptr;
  DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &self,
                  THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0);
  if (!self)
    return;
  std::thread(run, self, delay ? std::atof(delay) : 25.0, std::atof(seconds), out).detach();
}
#else
void SampleProfiler::startFromEnvironment() {}
#endif
