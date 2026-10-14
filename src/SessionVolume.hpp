#ifndef SESSIONVOLUME_HPP
#define SESSIONVOLUME_HPP

// This program's own volume (0 to 1), as Windows' volume mixer has it for
// it - not the speakers' and no one else's: the Lab's Play volume, for our
// engine (src/Embed.cpp) and the original game (src/original/Hook.cpp) alike.
// Whatever plays sound in it (SDL, DirectSound) plays through that. COM must
// be started on the calling thread.
// ours: Linux port (plumbing). The Windows volume mixer has no equivalent
// here, and the only caller is the Lab's Play view (src/Embed.cpp), which is
// Windows-only. The non-Windows version below says so and does nothing.
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>

inline bool setSessionVolume(float level) {
  if (level < 0)
    level = 0;
  if (level > 1)
    level = 1;
  IMMDeviceEnumerator *devices = nullptr;
  IMMDevice *device = nullptr;
  IAudioSessionManager *sessions = nullptr;
  ISimpleAudioVolume *volume = nullptr;
  bool ok = SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                       reinterpret_cast<void **>(&devices))) &&
            SUCCEEDED(devices->GetDefaultAudioEndpoint(eRender, eConsole, &device)) &&
            SUCCEEDED(device->Activate(__uuidof(IAudioSessionManager), CLSCTX_ALL, nullptr, reinterpret_cast<void **>(&sessions))) &&
            SUCCEEDED(sessions->GetSimpleAudioVolume(nullptr, FALSE, &volume)) && SUCCEEDED(volume->SetMasterVolume(level, nullptr));
  if (volume)
    volume->Release();
  if (sessions)
    sessions->Release();
  if (device)
    device->Release();
  if (devices)
    devices->Release();
  return ok;
}

#else

inline bool setSessionVolume(float level) {
  (void)level;
  return false;
}

#endif // _WIN32

#endif // SESSIONVOLUME_HPP
