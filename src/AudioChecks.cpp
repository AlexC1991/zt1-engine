// ours: a check (ZT_AUDIO_CHECKS=1 with --panel-shots <out>): the engine's
// sound (Audio.cpp, SDL3_mixer) as SDL2_mixer made it. Each case plays
// through SDL's "disk" audio driver - the mixer's output written to a file
// instead of the speakers (silent) - and the file is measured:
//   - a 1 kHz tone, mono at 22050 Hz as most of the game's sounds are: its
//     pitch, its level (volume 128 = the sound as it is), no distortion, the
//     same on both sides
//   - volume 64: half as loud (SDL2_mixer's Mix_Volume is linear)
//   - panning 255/0 and 64/255: each side scaled by level/255
//     (SDL2_mixer's Mix_SetPanning)
//   - a stereo sound's sides kept apart and in place
//   - the music's volume, two sounds at once adding up, loops repeating
//   - one of the game's own sounds against SDL's own decoding of it
// Results in <out>/audio-checks.txt.

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "Audio.hpp"
#include "ResourceManager.hpp"

namespace {

constexpr int kRate = 44100;
constexpr double kPi = 3.14159265358979323846;

// A WAV in memory: frames of channels 16-bit samples at rate from sample(frame, channel)
std::vector<Uint8> wav(int rate, int channels, int frames, const std::function<double(int, int)> &sample) {
  const Uint32 data = Uint32(frames) * channels * 2;
  std::vector<Uint8> out(44 + data);
  auto put32 = [&](size_t at, Uint32 v) { std::memcpy(&out[at], &v, 4); };
  auto put16 = [&](size_t at, Uint16 v) { std::memcpy(&out[at], &v, 2); };
  std::memcpy(&out[0], "RIFF", 4);
  put32(4, 36 + data);
  std::memcpy(&out[8], "WAVEfmt ", 8);
  put32(16, 16);
  put16(20, 1);
  put16(22, Uint16(channels));
  put32(24, Uint32(rate));
  put32(28, Uint32(rate * channels * 2));
  put16(32, Uint16(channels * 2));
  put16(34, 16);
  std::memcpy(&out[36], "data", 4);
  put32(40, data);
  for (int f = 0; f < frames; ++f)
    for (int c = 0; c < channels; ++c) {
      const double v = std::clamp(sample(f, c), -1.0, 1.0);
      const Sint16 s = Sint16(std::lround(v * 32767.0));
      std::memcpy(&out[44 + (size_t(f) * channels + c) * 2], &s, 2);
    }
  return out;
}

std::vector<Uint8> tone(int rate, int channels, double seconds, double amplitude, int silentChannel = -1) {
  return wav(rate, channels, int(rate * seconds), [&](int f, int c) {
    return c == silentChannel ? 0.0 : amplitude * std::sin(2.0 * kPi * 1000.0 * f / rate);
  });
}

// The mixer's output while play() runs, until every sound and the music are
// done (and a little after): stereo frames, -1..1
std::vector<float> capture(const std::string &file, const std::function<void()> &play) {
  Audio::close();
  SDL_QuitSubSystem(SDL_INIT_AUDIO);
  SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "disk", SDL_HINT_OVERRIDE);
  SDL_SetHintWithPriority(SDL_HINT_AUDIO_DISK_OUTPUT_FILE, file.c_str(), SDL_HINT_OVERRIDE);
  SDL_SetHintWithPriority(SDL_HINT_AUDIO_FORMAT, "S16", SDL_HINT_OVERRIDE);
  SDL_SetHintWithPriority(SDL_HINT_AUDIO_FREQUENCY, "44100", SDL_HINT_OVERRIDE);
  SDL_SetHintWithPriority(SDL_HINT_AUDIO_CHANNELS, "2", SDL_HINT_OVERRIDE);
  if (!Audio::open())
    return {};
  play();
  const Uint64 start = SDL_GetTicks();
  while ((Audio::playing(-1) || Audio::musicPlaying()) && SDL_GetTicks() - start < 10000)
    SDL_Delay(10);
  SDL_Delay(150);
  Audio::close();
  SDL_QuitSubSystem(SDL_INIT_AUDIO);
  std::ifstream in(file, std::ios::binary | std::ios::ate);
  const std::streamsize bytes = in.tellg();
  in.seekg(0);
  std::vector<Sint16> samples(size_t(std::max<std::streamsize>(0, bytes)) / 2);
  in.read(reinterpret_cast<char *>(samples.data()), std::streamsize(samples.size() * 2));
  std::vector<float> out;
  out.reserve(samples.size());
  for (Sint16 s : samples)
    out.push_back(s / 32768.0f);
  return out;
}

// Where the sound starts (the first frame above a whisper), and how long it
// lasts (to the last such frame)
struct Span {
  size_t first = 0, last = 0;
  bool found = false;
};
Span span(const std::vector<float> &s) {
  Span r;
  for (size_t f = 0; f + 1 < s.size(); f += 2)
    if (std::fabs(s[f]) > 0.002f || std::fabs(s[f + 1]) > 0.002f) {
      if (!r.found)
        r.first = f / 2;
      r.last = f / 2;
      r.found = true;
    }
  return r;
}

// Silent stretches inside the sound (3 or more frames of nothing on both
// sides): how many
int gaps(const std::vector<float> &s, const Span &sp) {
  int runs = 0, length = 0;
  for (size_t f = sp.first; f <= sp.last && f * 2 + 1 < s.size(); ++f) {
    if (s[f * 2] == 0.0f && s[f * 2 + 1] == 0.0f) {
      if (++length == 3)
        ++runs;
    } else {
      length = 0;
    }
  }
  return runs;
}

// One side's level (RMS) and how far it is from a pure 1 kHz sine (the
// residual of the best fit, as a fraction of the level), over frames
// [from, to)
struct Measure {
  double rms = 0, residual = 0, frequency = 0;
};
Measure measure(const std::vector<float> &s, int side, size_t from, size_t to) {
  Measure m;
  if (to <= from)
    return m;
  double sum = 0, a = 0, b = 0, ss = 0, sc = 0, cc = 0, sx = 0, cx = 0;
  int crossings = 0;
  float prev = s[from * 2 + side];
  for (size_t f = from; f < to; ++f) {
    const double x = s[f * 2 + side];
    sum += x * x;
    const double w = 2.0 * kPi * 1000.0 * double(f) / kRate;
    const double sn = std::sin(w), cs = std::cos(w);
    ss += sn * sn;
    sc += sn * cs;
    cc += cs * cs;
    sx += sn * x;
    cx += cs * x;
    if ((prev < 0) != (x < 0))
      ++crossings;
    prev = float(x);
  }
  const double n = double(to - from);
  m.rms = std::sqrt(sum / n);
  const double det = ss * cc - sc * sc;
  if (det > 0) {
    a = (sx * cc - cx * sc) / det;
    b = (cx * ss - sx * sc) / det;
  }
  double err = 0;
  for (size_t f = from; f < to; ++f) {
    const double w = 2.0 * kPi * 1000.0 * double(f) / kRate;
    const double e = s[f * 2 + side] - (a * std::sin(w) + b * std::cos(w));
    err += e * e;
  }
  m.residual = m.rms > 0 ? std::sqrt(err / n) / m.rms : 0;
  m.frequency = crossings / 2.0 / (n / kRate);
  return m;
}

} // namespace

std::string runAudioChecks(ResourceManager *rm, const std::string &outDir) {
  std::ostringstream report;
  int failures = 0;
  auto check = [&](const std::string &what, bool ok, const std::string &detail) {
    report << (ok ? "PASS " : "FAIL ") << what << " (" << detail << ")\n";
    if (!ok)
      ++failures;
  };
  auto near = [](double v, double want, double tolerance) { return std::fabs(v - want) <= tolerance * want; };
  auto fmt = [](double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.4f", v);
    return std::string(b);
  };
  const double full = 0.5 / std::sqrt(2.0); // (a 0.5 sine's RMS)
  // The middle of a sound, away from its edges
  auto middle = [](const Span &s) {
    const size_t len = s.last - s.first;
    return std::make_pair(s.first + len / 4, s.last - len / 4);
  };

  // A mono 22050 Hz tone at volume 128, centred
  {
    const auto bytes = tone(22050, 1, 0.4, 0.5);
    std::vector<float> s = capture(outDir + "/audio-mono.raw", [&] {
      Audio::Clip *c = Audio::load(bytes.data(), bytes.size());
      Audio::play(c, 0);
    });
    const Span sp = span(s);
    check("a tone is heard", sp.found, std::to_string(s.size() / 2) + " frames written");
    if (sp.found) {
      auto [from, to] = middle(sp);
      const Measure l = measure(s, 0, from, to), r = measure(s, 1, from, to);
      check("its pitch is kept (22050 Hz played at 44100)", near(l.frequency, 1000, 0.01), fmt(l.frequency) + " Hz");
      check("volume 128 is the sound as it is", near(l.rms, full, 0.02) && near(r.rms, full, 0.02),
            "left " + fmt(l.rms) + ", right " + fmt(r.rms) + ", want " + fmt(full));
      check("no distortion", l.residual < 0.01 && r.residual < 0.01,
            "left " + fmt(l.residual) + ", right " + fmt(r.residual));
      check("it lasts as long as it is", near(double(sp.last - sp.first) / kRate, 0.4, 0.03),
            fmt(double(sp.last - sp.first) / kRate) + " s");
      check("no silent gaps inside it", gaps(s, sp) == 0, std::to_string(gaps(s, sp)) + " gaps");
    }
  }
  // Volume 64
  {
    const auto bytes = tone(22050, 1, 0.3, 0.5);
    std::vector<float> s = capture(outDir + "/audio-volume.raw", [&] {
      Audio::Clip *c = Audio::load(bytes.data(), bytes.size());
      const int ch = Audio::play(c, 0);
      Audio::volume(ch, 64);
    });
    const Span sp = span(s);
    if (sp.found) {
      auto [from, to] = middle(sp);
      const Measure l = measure(s, 0, from, to);
      check("volume 64 is half as loud", near(l.rms, full / 2, 0.03), fmt(l.rms) + ", want " + fmt(full / 2));
    } else {
      check("volume 64 is heard", false, "silent");
    }
  }
  // Panning
  for (auto [left, right] : {std::pair<int, int>{255, 0}, std::pair<int, int>{64, 255}}) {
    const auto bytes = tone(22050, 1, 0.3, 0.5);
    std::vector<float> s = capture(outDir + "/audio-pan.raw", [&] {
      Audio::Clip *c = Audio::load(bytes.data(), bytes.size());
      const int ch = Audio::play(c, 0);
      Audio::pan(ch, left, right);
    });
    const Span sp = span(s);
    if (sp.found) {
      auto [from, to] = middle(sp);
      const Measure l = measure(s, 0, from, to), r = measure(s, 1, from, to);
      const double wl = full * left / 255.0, wr = full * right / 255.0;
      const bool ok = (wl == 0 ? l.rms < 0.001 : near(l.rms, wl, 0.03)) && (wr == 0 ? r.rms < 0.001 : near(r.rms, wr, 0.03));
      check("panning " + std::to_string(left) + "/" + std::to_string(right) + " scales each side", ok,
            "left " + fmt(l.rms) + " want " + fmt(wl) + ", right " + fmt(r.rms) + " want " + fmt(wr));
    } else {
      check("panning is heard", false, "silent");
    }
  }
  // A stereo sound: the tone on the left only stays on the left
  {
    const auto bytes = tone(44100, 2, 0.3, 0.5, 1);
    std::vector<float> s = capture(outDir + "/audio-stereo.raw", [&] {
      Audio::Clip *c = Audio::load(bytes.data(), bytes.size());
      Audio::play(c, 0);
    });
    const Span sp = span(s);
    if (sp.found) {
      auto [from, to] = middle(sp);
      const Measure l = measure(s, 0, from, to), r = measure(s, 1, from, to);
      check("a stereo sound's sides stay apart and in place", near(l.rms, full, 0.02) && r.rms < 0.001,
            "left " + fmt(l.rms) + ", right " + fmt(r.rms));
    } else {
      check("a stereo sound is heard", false, "silent");
    }
  }
  // The music at volume 32
  {
    const auto bytes = tone(22050, 1, 0.3, 0.5);
    std::vector<float> s = capture(outDir + "/audio-music.raw", [&] {
      Audio::Clip *c = Audio::load(bytes.data(), bytes.size());
      Audio::musicVolume(32);
      Audio::playMusic(c, 0);
    });
    const Span sp = span(s);
    if (sp.found) {
      auto [from, to] = middle(sp);
      const Measure l = measure(s, 0, from, to);
      check("the music's volume 32 is a quarter", near(l.rms, full / 4, 0.03), fmt(l.rms) + ", want " + fmt(full / 4));
    } else {
      check("the music is heard", false, "silent");
    }
    Audio::musicVolume(Audio::kMaxVolume);
  }
  // Two sounds at once add up
  {
    const auto bytes = tone(22050, 1, 0.3, 0.25);
    std::vector<float> s = capture(outDir + "/audio-two.raw", [&] {
      Audio::Clip *c = Audio::load(bytes.data(), bytes.size());
      const int a = Audio::play(c, 0), b = Audio::play(c, 0);
      check("two sounds get two channels", a >= 0 && b >= 0 && a != b,
            "channels " + std::to_string(a) + " and " + std::to_string(b));
    });
    const Span sp = span(s);
    if (sp.found) {
      auto [from, to] = middle(sp);
      const Measure l = measure(s, 0, from, to);
      check("two sounds at once add up", near(l.rms, full, 0.03), fmt(l.rms) + ", want " + fmt(full));
    } else {
      check("two sounds are heard", false, "silent");
    }
  }
  // Loops: played once and twice again
  {
    const auto bytes = tone(22050, 1, 0.2, 0.5);
    std::vector<float> s = capture(outDir + "/audio-loops.raw", [&] {
      Audio::Clip *c = Audio::load(bytes.data(), bytes.size());
      Audio::play(c, 2);
    });
    const Span sp = span(s);
    const double seconds = sp.found ? double(sp.last - sp.first) / kRate : 0;
    check("loops 2 plays it three times", near(seconds, 0.6, 0.03), fmt(seconds) + " s");
    check("no silent gaps where it loops", sp.found && gaps(s, sp) == 0, std::to_string(sp.found ? gaps(s, sp) : -1) + " gaps");
  }
  // One of the game's sounds, against SDL's own decoding and resampling
  if (rm) {
    int size = 0;
    void *data = rm->getFileBytes("sounds/uiclick.wav", &size);
    if (data && size > 0) {
      SDL_AudioSpec spec{};
      Uint8 *pcm = nullptr;
      Uint32 len = 0;
      std::vector<float> want;
      if (SDL_LoadWAV_IO(SDL_IOFromConstMem(data, size_t(size)), true, &spec, &pcm, &len)) {
        const SDL_AudioSpec to{SDL_AUDIO_F32, 2, kRate};
        Uint8 *conv = nullptr;
        int convLen = 0;
        if (SDL_ConvertAudioSamples(&spec, pcm, int(len), &to, &conv, &convLen)) {
          want.assign(reinterpret_cast<float *>(conv), reinterpret_cast<float *>(conv) + convLen / 4);
          SDL_free(conv);
        }
        SDL_free(pcm);
      }
      std::vector<float> s = capture(outDir + "/audio-game.raw", [&] {
        Audio::Clip *c = Audio::load(data, size_t(size));
        Audio::play(c, 0);
      });
      const Span got = span(s), ref = span(want);
      if (got.found && ref.found) {
        // (lined up where they match best - a quiet click's start is fuzzy -
        // within 64 frames of where each crosses a whisper, then compared
        // frame by frame)
        double correlation = 0, gg = 0, ww = 0;
        size_t n = 0;
        const size_t from = got.first > 64 ? got.first - 64 : 0;
        for (size_t at = from; at <= got.first + 64; ++at) {
          if (at >= s.size() / 2)
            break;
          double dot = 0, g2 = 0, w2 = 0;
          const size_t len = std::min(s.size() / 2 - at, want.size() / 2 - ref.first);
          for (size_t f = 0; f < len; ++f)
            for (int c = 0; c < 2; ++c) {
              const double g = s[(at + f) * 2 + c], w = want[(ref.first + f) * 2 + c];
              dot += g * w;
              g2 += g * g;
              w2 += w * w;
            }
          const double r = g2 > 0 && w2 > 0 ? dot / std::sqrt(g2 * w2) : 0;
          if (r > correlation) {
            correlation = r;
            gg = g2;
            ww = w2;
            n = len;
          }
        }
        n = std::max<size_t>(n, 1);
        report << "  (uiclick: " << SDL_GetAudioFormatName(spec.format) << ", " << spec.channels << " channel(s), " << spec.freq
               << " Hz; heard " << (got.last - got.first) << " frames, SDL's " << (ref.last - ref.first) << ")\n";
        check("no silent gaps inside the game's uiclick", gaps(s, got) == 0, std::to_string(gaps(s, got)) + " gaps");
        check("the game's uiclick sounds as SDL decodes it", correlation > 0.99 && near(std::sqrt(gg), std::sqrt(ww), 0.03),
              "correlation " + fmt(correlation) + ", level " + fmt(std::sqrt(gg / n)) + " want " + fmt(std::sqrt(ww / n)));
      } else {
        check("the game's uiclick is heard", false, got.found ? "SDL couldn't decode it" : "silent");
      }
    } else {
      report << "SKIP the game's uiclick (not found)\n";
    }
    std::free(data);
  }
  report << "Failures: " << failures << "\n";
  std::ofstream(outDir + "/audio-checks.txt") << report.str();
  SDL_Log("[AudioChecks]\n%s", report.str().c_str());
  return report.str();
}
