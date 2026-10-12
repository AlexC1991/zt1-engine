// zt1-original.exe: the original game, zoo.exe, started with our hook
// (zt1-hook.dll, Hook.cpp) loaded into it before any of its own code runs -
// the way OpenZT loads its DLL. The Lab's Play original runs it:
//   zt1-original.exe --zoo <zoo.exe> [--embed <Lab window>] [--size <w>x<h>]
//                    [--log <file>]
// zoo.exe gets this program's stdin (the Lab's commands, read by the hook)
// and what's asked is passed to the hook in the environment (ZT1_HOOK_*).
// 32-bit, like zoo.exe: LoadLibraryA is at the same address in both.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

static int fail(const char *what) {
  std::fprintf(stderr, "zt1-original: %s (error %lu)\n", what, GetLastError());
  return 1;
}

int main(int argc, char **argv) {
  std::string zoo = "C:\\Program Files (x86)\\Microsoft Games\\Zoo Tycoon\\zoo.exe";
  for (int i = 1; i + 1 < argc; i += 2) {
    if (!std::strcmp(argv[i], "--zoo"))
      zoo = argv[i + 1];
    else if (!std::strcmp(argv[i], "--embed"))
      SetEnvironmentVariableA("ZT1_HOOK_EMBED", argv[i + 1]);
    else if (!std::strcmp(argv[i], "--size"))
      SetEnvironmentVariableA("ZT1_HOOK_SIZE", argv[i + 1]);
    else if (!std::strcmp(argv[i], "--log"))
      SetEnvironmentVariableA("ZT1_HOOK_LOG", argv[i + 1]);
  }
  // (the hook: next to this program)
  char self[MAX_PATH];
  GetModuleFileNameA(nullptr, self, MAX_PATH);
  std::string hook = self;
  hook = hook.substr(0, hook.find_last_of("\\/") + 1) + "zt1-hook.dll";
  if (GetFileAttributesA(hook.c_str()) == INVALID_FILE_ATTRIBUTES)
    return fail("no zt1-hook.dll next to zt1-original.exe");
  std::string dir = zoo.substr(0, zoo.find_last_of("\\/"));

  // zoo.exe, paused before its first instruction, in its own folder (it
  // reads zoo.ini and its .ztd files from there)
  STARTUPINFOA si{};
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  PROCESS_INFORMATION pi{};
  std::string cmd = "\"" + zoo + "\"";
  if (!CreateProcessA(zoo.c_str(), cmd.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED, nullptr, dir.c_str(), &si, &pi))
    return fail(("can't start " + zoo).c_str());

  // The hook loaded into it: its path written into zoo.exe's memory, and a
  // thread of zoo.exe's calls LoadLibraryA on it
  void *remote = VirtualAllocEx(pi.hProcess, nullptr, hook.size() + 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (!remote || !WriteProcessMemory(pi.hProcess, remote, hook.c_str(), hook.size() + 1, nullptr)) {
    TerminateProcess(pi.hProcess, 1);
    return fail("can't write into zoo.exe");
  }
  auto load = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA"));
  HANDLE t = CreateRemoteThread(pi.hProcess, nullptr, 0, load, remote, 0, nullptr);
  if (!t) {
    TerminateProcess(pi.hProcess, 1);
    return fail("can't load the hook into zoo.exe");
  }
  WaitForSingleObject(t, 30000);
  DWORD loaded = 0;
  GetExitCodeThread(t, &loaded);
  CloseHandle(t);
  VirtualFreeEx(pi.hProcess, remote, 0, MEM_RELEASE);
  if (!loaded) {
    TerminateProcess(pi.hProcess, 1);
    return fail("zoo.exe couldn't load zt1-hook.dll");
  }
  std::printf("zt1-original: zoo.exe %lu started with the hook\n", pi.dwProcessId);
  std::fflush(stdout);

  // Then it runs; this waits for it and ends as it does
  ResumeThread(pi.hThread);
  CloseHandle(pi.hThread);
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 0;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  return static_cast<int>(code);
}
