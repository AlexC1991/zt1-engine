#ifndef PLATFORMCOMPAT_HPP
#define PLATFORMCOMPAT_HPP

// The small set of operating-system names the engine calls directly, in one
// place, so a caller can be written once and read the same everywhere.
//
// Windows is the target this was built for and keeps its own names untouched.
// Everywhere else the same names are provided over their POSIX counterparts.
// The Lab's networking is plain TCP underneath either way, so for sockets the
// difference really is naming; the two places where it is not (select, and
// opening a file with the desktop) are marked below.
//
// ours: Linux port (plumbing; the original game has nothing like this).

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <winsock2.h>

// One name for the receive timeout on both platforms: Winsock takes a DWORD of
// milliseconds, POSIX a struct timeval (see the other branch).
inline int setReceiveTimeout(SOCKET s, DWORD milliseconds) {
  return ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&milliseconds),
                      sizeof milliseconds);
}

#else // !_WIN32

#include <arpa/inet.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

// --- Sockets -------------------------------------------------------------
// Winsock's names and types over BSD sockets.
using SOCKET = int;
using WSADATA = int;

#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define closesocket(fd) ::close(fd)
#define MAKEWORD(a, b) (static_cast<unsigned short>(((a) & 0xff) | (((b) & 0xff) << 8)))

inline int WSAStartup(int, WSADATA *) { return 0; } // nothing to start up here
inline int WSACleanup() { return 0; }

// Windows spellings that mean the same thing here.
using DWORD = uint32_t;
using BOOL = int;

// ioctlsocket(s, FIONBIO, &n) makes a socket non-blocking. FIONBIO here wants
// an int where Winsock passes a u_long, so this narrows it. (u_long and
// FIONBIO themselves come from sys/types.h and sys/ioctl.h, unchanged.)
inline int ioctlsocket(SOCKET s, long request, u_long *argument) {
  if (request == FIONBIO) {
    int value = argument ? static_cast<int>(*argument) : 0;
    return ::ioctl(s, FIONBIO, &value);
  }
  return ::ioctl(s, request, argument);
}

// Winsock passes SO_RCVTIMEO a DWORD of milliseconds; POSIX wants a struct
// timeval. The intent is the same, the shape is not, so it gets one name.
inline int setReceiveTimeout(SOCKET s, DWORD milliseconds) {
  timeval tv{};
  tv.tv_sec = static_cast<time_t>(milliseconds / 1000);
  tv.tv_usec = static_cast<suseconds_t>((milliseconds % 1000) * 1000);
  return ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
}

// --- Dynamic libraries ---------------------------------------------------
// The Lab builds a script module and loads it while running. Windows puts the
// symbol's address behind a cast; dlsym returns it directly, so both read the
// same at the call site.
using HMODULE = void *;
using INT_PTR = intptr_t;

inline HMODULE LoadLibraryA(const char *path) { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }
inline void *GetProcAddress(HMODULE handle, const char *name) {
  return handle ? dlsym(handle, name) : nullptr;
}
inline int FreeLibrary(HMODULE handle) { return handle ? dlclose(handle) : 0; }

// --- Waiting for a socket to become writable -----------------------------
// Winsock ignores select()'s first argument; POSIX wants one more than the
// highest descriptor in the sets, so the same call is not portable as written.
// The Lab always waits on a single socket, so this gives it the POSIX meaning.
inline int selectWrite(SOCKET fd, fd_set *writeSet, timeval *timeout) {
  return ::select(static_cast<int>(fd) + 1, nullptr, writeSet, nullptr, timeout);
}

// --- Opening a file with the desktop -------------------------------------
#define SW_SHOWNORMAL 1

// Windows' ShellExecuteA is used here two ways: to open a file with whatever
// is registered for it, and to run a program with an argument (opening a
// script in the editor). The caller compares the result against 32, the way
// Windows reports trouble, so this keeps that shape rather than returning a
// plain status: above 32 means it went out, 0 means it did not and the caller
// should try its fallback. It goes through the shell so the desktop decides,
// which is what the Windows call does.
inline void *ShellExecuteA(void *, const char *, const char *target, const char *arguments,
                           const char *, int) {
  std::string command;
  if (arguments && *arguments)
    command = "command -v '" + std::string(target) + "' >/dev/null 2>&1 && ('" +
              std::string(target) + "' '" + arguments + "' >/dev/null 2>&1 &)";
  else
    command = std::string("xdg-open '") + target + "' >/dev/null 2>&1 &";
  return std::system(command.c_str()) == 0 ? reinterpret_cast<void *>(static_cast<intptr_t>(33))
                                           : nullptr;
}

// --- A few MSVC spellings of the C library -------------------------------
// The same functions here, under the names MSVC gives them.
#define TRUE 1
#define FALSE 0
#define _popen(command, mode) ::popen((command), (mode))
#define _pclose(stream) ::pclose(stream)

// MSVC's _putenv_s(name, value); setenv's third argument means "overwrite".
inline int _putenv_s(const char *name, const char *value) {
  return value && *value ? ::setenv(name, value, 1) : ::unsetenv(name);
}

#endif // !_WIN32

#endif // PLATFORMCOMPAT_HPP
