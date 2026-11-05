// addition: visible installer hand-off shared by the game and Lab. Kept in
// a separate, statically linked executable, copied outside the install folder.
// No network access: callers already verify the signed release manifest; this
// process checks the installer size/SHA-512 again immediately before running it.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <algorithm>
#include <atomic>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr UINT kStatus = WM_APP + 1;
HWND window = nullptr, closeButton = nullptr;
HFONT bodyFont = nullptr, titleFont = nullptr;
std::wstring appName, version, status = L"Preparing update...", detail;
bool lab = false;
int stage = 0;
std::atomic<bool> finished{false};
std::map<std::wstring, std::wstring> args;
COLORREF background() { return lab ? RGB(20,23,26) : RGB(37,58,44); }
COLORREF foreground() { return lab ? RGB(228,231,234) : RGB(255,228,173); }
COLORREF secondary() { return lab ? RGB(181,188,195) : RGB(208,209,179); }
COLORREF accent() { return lab ? RGB(127,214,162) : RGB(237,218,150); }

std::wstring quote(const std::wstring &value) {
  std::wstring result = L"\"";
  size_t slashes = 0;
  for (wchar_t c : value) {
    if (c == L'\\') { ++slashes; continue; }
    result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
    slashes = 0;
    result += c;
  }
  result.append(slashes * 2, L'\\');
  return result + L"\"";
}

void report(int next, const std::wstring &message) {
  auto *copy = new std::wstring(message);
  if (!PostMessageW(window, kStatus, next, reinterpret_cast<LPARAM>(copy))) delete copy;
}
void fail(const std::wstring &message) { report(4, message); finished = true; }

bool absoluteExe(const std::wstring &path) {
  return path.size() > 7 && ((path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) ||
         path.compare(0, 2, L"\\\\") == 0) &&
         _wcsicmp(path.c_str() + path.size() - 4, L".exe") == 0;
}
std::wstring folder(const std::wstring &path) { return path.substr(0, path.find_last_of(L"\\/")); }

bool expectedDigest(std::vector<BYTE> &out) {
  const std::wstring &value = args[L"--sha512"];
  if (value.size() == 128) {
    auto nibble = [](wchar_t c) -> int {
      if (c >= L'0' && c <= L'9') return c - L'0';
      if (c >= L'a' && c <= L'f') return c - L'a' + 10;
      if (c >= L'A' && c <= L'F') return c - L'A' + 10;
      return -1;
    };
    out.resize(64);
    for (size_t i = 0; i < out.size(); ++i) {
      int a = nibble(value[i * 2]), b = nibble(value[i * 2 + 1]);
      if (a < 0 || b < 0) return false;
      out[i] = static_cast<BYTE>((a << 4) | b);
    }
    return true;
  }
  DWORD length = 0;
  if (!CryptStringToBinaryW(value.c_str(), static_cast<DWORD>(value.size()), CRYPT_STRING_BASE64,
                           nullptr, &length, nullptr, nullptr) || length != 64) return false;
  out.resize(length);
  return CryptStringToBinaryW(value.c_str(), static_cast<DWORD>(value.size()), CRYPT_STRING_BASE64,
                              out.data(), &length, nullptr, nullptr) != FALSE;
}

bool verify(HANDLE file, uint64_t expectedSize, const std::vector<BYTE> &expected) {
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(file, &size) || static_cast<uint64_t>(size.QuadPart) != expectedSize) return false;
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA512_ALGORITHM, nullptr, 0)) return false;
  bool valid = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) == 0;
  std::vector<BYTE> buffer(64 * 1024);
  DWORD got = 0;
  while (valid) {
    if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr)) { valid = false; break; }
    if (!got) break;
    valid = BCryptHashData(hash, buffer.data(), got, 0) == 0;
  }
  BYTE actual[64]{};
  valid = valid && BCryptFinishHash(hash, actual, sizeof(actual), 0) == 0 &&
          std::equal(expected.begin(), expected.end(), actual);
  if (hash) BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  return valid;
}

std::string utf8(const std::wstring &value) {
  int count = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  std::string out(count, '\0');
  if (count) WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), out.data(), count, nullptr, nullptr);
  return out;
}

void work(HANDLE parent, uint64_t expectedSize, std::vector<BYTE> expected) {
  report(0, L"Waiting for " + appName + L" to close...");
  if (WaitForSingleObject(parent, 30000) != WAIT_OBJECT_0) {
    CloseHandle(parent); fail(L"The app did not close. The update has not been installed. Close it and try again."); return;
  }
  CloseHandle(parent);
  report(1, L"Checking the downloaded installer...");
  // Deny writes/deletes until installation finishes: the checked file cannot
  // be replaced between validation and CreateProcess.
  HANDLE file = CreateFileW(args[L"--installer"].c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE || !verify(file, expectedSize, expected)) {
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    fail(L"The installer no longer matches the verified download. It was not run. Download a fresh copy."); return;
  }
  report(1, L"Installing update...");
  std::wstring command = quote(args[L"--installer"]) +
      L" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /NOCLOSEAPPLICATIONS";
  // The game installer recognises /RELAUNCH by prefix. A Lab-only flag here
  // would accidentally launch the game twice, before our acknowledged restart.
  if (lab) command += L" /RELAUNCHLAB=0";
  STARTUPINFOW startup{sizeof(startup)};
  PROCESS_INFORMATION installer{};
  if (!CreateProcessW(args[L"--installer"].c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                       nullptr, folder(args[L"--installer"]).c_str(), &startup, &installer)) {
    CloseHandle(file); fail(L"The installer could not start (Windows error " + std::to_wstring(GetLastError()) + L")."); return;
  }
  CloseHandle(installer.hThread);
  WaitForSingleObject(installer.hProcess, INFINITE);
  DWORD exitCode = 1;
  GetExitCodeProcess(installer.hProcess, &exitCode);
  CloseHandle(installer.hProcess);
  CloseHandle(file);
  if (exitCode != 0) { fail(L"Installation did not finish (installer exit code " + std::to_wstring(exitCode) + L"). Your saved data is kept."); return; }
  report(2, L"Starting the updated " + appName + L"...");
  BYTE random[16]{};
  if (BCryptGenRandom(nullptr, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
    fail(L"The update installed, but a secure restart could not be prepared. Open the app manually."); return;
  }
  const wchar_t *hex = L"0123456789abcdef";
  std::wstring token;
  for (BYTE byte : random) { token += hex[byte >> 4]; token += hex[byte & 15]; }
  wchar_t own[32768]{};
  GetModuleFileNameW(nullptr, own, 32768);
  const std::wstring ack = folder(own) + L"\\launch-" + token + L".ack";
  command = quote(args[L"--launch"]) + L" --update-ack " + quote(ack) + L" --update-token " + token;
  PROCESS_INFORMATION launched{};
  if (!CreateProcessW(args[L"--launch"].c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                       folder(args[L"--launch"]).c_str(), &startup, &launched)) {
    fail(L"The update installed, but the app could not reopen. Open it from your usual shortcut."); return;
  }
  CloseHandle(launched.hThread);
  const std::string required = utf8(token) + "\n" + utf8(version) + "\n";
  const ULONGLONG deadline = GetTickCount64() + 120000;
  bool ready = false;
  while (GetTickCount64() < deadline) {
    std::ifstream input(ack, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (contents == required) { ready = true; break; }
    if (WaitForSingleObject(launched.hProcess, 250) == WAIT_OBJECT_0) break;
  }
  CloseHandle(launched.hProcess);
  DeleteFileW(ack.c_str());
  if (!ready) { fail(L"The update installed, but the new version did not confirm it was ready. Open the app and check its version."); return; }
  report(3, L"All updated. " + appName + L" is ready.");
  finished = true;
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == kStatus) {
    auto *copy = reinterpret_cast<std::wstring *>(lparam);
    stage = static_cast<int>(wparam); status = *copy; delete copy;
    EnableWindow(closeButton, stage >= 3);
    InvalidateRect(hwnd, nullptr, TRUE);
    if (stage == 3) SetTimer(hwnd, 1, 1200, nullptr);
    return 0;
  }
  if (message == WM_TIMER && stage == 3) { DestroyWindow(hwnd); return 0; }
  if (message == WM_COMMAND && LOWORD(wparam) == 1 && stage >= 3) { DestroyWindow(hwnd); return 0; }
  if (message == WM_CLOSE) { if (finished) DestroyWindow(hwnd); return 0; }
  if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
  if (message == WM_ERASEBKGND) return 1;
  if (message == WM_CTLCOLORBTN) { SetTextColor(reinterpret_cast<HDC>(wparam), foreground()); return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE)); }
  if (message == WM_PAINT) {
    PAINTSTRUCT paint{}; HDC dc = BeginPaint(hwnd, &paint);
    RECT area{}; GetClientRect(hwnd, &area);
    HBRUSH fill = CreateSolidBrush(background()); FillRect(dc, &area, fill); DeleteObject(fill);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, foreground()); SelectObject(dc, titleFont);
    RECT title{18,16,area.right-18,42}; std::wstring titleText = appName + L" Update";
    DrawTextW(dc, titleText.c_str(), -1, &title, DT_SINGLELINE);
    SelectObject(dc, bodyFont); SetTextColor(dc, secondary());
    RECT v{18,44,area.right-18,64}; std::wstring vtext = L"Version " + version;
    DrawTextW(dc, vtext.c_str(), -1, &v, DT_SINGLELINE);
    SetTextColor(dc, stage == 4 ? RGB(244,174,154) : accent());
    RECT said{18,78,area.right-18,130}; DrawTextW(dc, status.c_str(), -1, &said, DT_WORDBREAK);
    if (stage != 4) {
      const wchar_t *labels[]{L"Close app", L"Install update", L"Reopen app", L"Confirm new version"};
      for (int i = 0; i < 4; ++i) {
        SetTextColor(dc, i == stage ? accent() : secondary());
        RECT line{18,136+i*23,area.right-18,158+i*23};
        std::wstring text = (i < stage ? L"\x2713  " : i == stage ? L"\x203a  " : L"\x00b7  ") + std::wstring(labels[i]);
        DrawTextW(dc, text.c_str(), -1, &line, DT_SINGLELINE);
      }
    }
    EndPaint(hwnd, &paint); return 0;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  int count = 0; wchar_t **values = CommandLineToArgvW(GetCommandLineW(), &count);
  if (!values || count < 15 || count % 2 != 1) { if (values) LocalFree(values); return 2; }
  for (int i = 1; i + 1 < count; i += 2) args[values[i]] = values[i+1];
  LocalFree(values);
  lab = args[L"--app"] == L"lab";
  if (!lab && args[L"--app"] != L"game") return 2;
  appName = lab ? L"ZT1 Lab" : L"ZT1 Engine";
  version = args[L"--version"];
  if (version.empty() || !std::all_of(version.begin(), version.end(), [](wchar_t c) { return (c >= L'0' && c <= L'9') || c == L'.'; })) return 2;
  uint64_t bytes = 0; DWORD pid = 0;
  try { bytes = std::stoull(args[L"--size"]); pid = static_cast<DWORD>(std::stoul(args[L"--parent"])); } catch (...) { return 2; }
  std::vector<BYTE> expected;
  if (!pid || !bytes || bytes > 2ULL*1024*1024*1024 || !absoluteExe(args[L"--installer"]) ||
      !absoluteExe(args[L"--launch"]) || !expectedDigest(expected) ||
      (args[L"--ready-event"].empty() && args[L"--ready-file"].empty())) return 2;
  HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, pid);
  if (!parent) return 2;
  SetProcessDPIAware();
  WNDCLASSW cls{}; cls.hInstance=instance; cls.lpfnWndProc=windowProc; cls.lpszClassName=L"ZT1UpdateHelper";
  cls.hCursor=LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&cls);
  bodyFont=CreateFontW(-13,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,lab?L"Segoe UI":L"Arial");
  titleFont=CreateFontW(-15,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,lab?L"Segoe UI":L"Arial");
  const int width=380,height=310;
  window=CreateWindowExW(0,cls.lpszClassName,(appName+L" Update").c_str(),WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,
      (GetSystemMetrics(SM_CXSCREEN)-width)/2,(GetSystemMetrics(SM_CYSCREEN)-height)/2,width,height,nullptr,nullptr,instance,nullptr);
  if (!window) { CloseHandle(parent); return 2; }
  closeButton=CreateWindowW(L"BUTTON",L"Close",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,280,238,76,27,window,reinterpret_cast<HMENU>(1),instance,nullptr);
  SendMessageW(closeButton,WM_SETFONT,reinterpret_cast<WPARAM>(bodyFont),TRUE); EnableWindow(closeButton,FALSE);
  ShowWindow(window,SW_SHOWNORMAL); UpdateWindow(window);
  if (!args[L"--ready-event"].empty()) {
    HANDLE ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,args[L"--ready-event"].c_str());
    if (ready) { SetEvent(ready); CloseHandle(ready); }
  }
  if (!args[L"--ready-file"].empty()) {
    std::ofstream ready(args[L"--ready-file"],std::ios::binary|std::ios::trunc); ready << "ready";
  }
  std::thread worker(work,parent,bytes,std::move(expected));
  MSG message{};
  while (GetMessageW(&message,nullptr,0,0)>0) { TranslateMessage(&message); DispatchMessageW(&message); }
  worker.join(); DeleteObject(bodyFont); DeleteObject(titleFont);
  return stage == 3 ? 0 : 1;
}
