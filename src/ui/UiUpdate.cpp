#include "UiUpdate.hpp"
#include "UiButton.hpp"
#include "UiLayout.hpp"
#include "UiText.hpp"
#include "UiOptions.hpp"
#include <SDL_image.h>
#include "../ResourceManager.hpp"
#include "../IniReader.hpp"
#include "../Utils.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace {
constexpr int kStatus = 7120, kWidth = 340, kHeight = 218;
constexpr int kHeading = 12101, kState = 12102, kVersion = 12103, kMessage = 12104;
constexpr int kLater = 12105, kPrimary = 12106, kClose = 12107;
std::string section(const std::string &name, const std::string &body) {
  return "[" + name + "]\r\n" + body + "\r\n\r\n";
}
std::string text(int id, int x, int y, int w, int h, int font = 3452, int size = 3453) {
  return section("Text" + std::to_string(id), "type=UIText\r\nid=" + std::to_string(id) +
      "\r\nx=" + std::to_string(x) + "\r\ny=" + std::to_string(y) + "\r\ndx=" + std::to_string(w) +
      "\r\ndy=" + std::to_string(h) + "\r\nfont=" + std::to_string(font) + "\r\nfontsize=" +
      std::to_string(size) + "\r\nforecolor=255\r\nforecolor=228\r\nforecolor=173\r\ntransparent=1\r\nlayer=2");
}
std::string button(int id, int x, int y) {
  return section("Button" + std::to_string(id), "type=UIButton\r\nid=" + std::to_string(id) +
      "\r\nx=" + std::to_string(x) + "\r\ny=" + std::to_string(y) +
      "\r\njustify=center\r\nfont=14000\r\nfontsize=14001\r\nforecolor=255\r\nforecolor=186\r\nforecolor=16\r\n"
      "highlightcolor=255\r\nhighlightcolor=223\r\nhighlightcolor=41\r\n"
      "graycolor=132\r\ngraycolor=149\r\ngraycolor=124\r\n"
      "animation=ui/gameopts/textbck/textbck\r\nlayer=3");
}
std::string layout() {
  std::string t = section("LayoutInfo", "x=center\r\ny=center\r\ndx=340\r\ndy=218\r\nid=12100\r\nlayer=6");
  t += text(kHeading, 14, 9, 284, 22, 3270, 3271);
  t += text(kState, 16, 46, 308, 21);
  t += text(kVersion, 16, 74, 308, 20);
  t += text(kMessage, 16, 104, 308, 60);
  t += button(kLater, 80, 179);
  t += button(kPrimary, 206, 179);
  t += section("Close", "type=UIButton\r\nid=12107\r\nx=309\r\ny=6\r\nanimation=ui/sharedui/close/close\r\nlayer=3");
  return t;
}
std::string bytes(uint64_t n) {
  std::ostringstream s;
  if (n >= 1024 * 1024) s << std::fixed << std::setprecision(1) << n / (1024.0 * 1024.0) << " MB";
  else s << n / 1024 << " KB";
  return s.str();
}
std::string label(Updater::State s) {
  using S = Updater::State;
  switch (s) {
    case S::Off: return "Check for Updates";
    case S::Idle: case S::Checking: return "Checking for Updates";
    case S::Available: return "Update available";
    case S::Downloading: return "Downloading update";
    case S::Verifying: return "Verifying download";
    case S::Ready: return "Ready to update";
    case S::Installing: return "Updating";
    case S::UpToDate: return "All updated";
    case S::Failed: return "Update could not finish";
  }
  return "Check for Updates";
}
bool spinning(Updater::State s) {
  return s == Updater::State::Checking || s == Updater::State::Verifying || s == Updater::State::Installing;
}
// addition: a small procedural gear; it contains no replacement game artwork.
void gear(SDL_Renderer *r, float x, float y, float radius, bool moving) {
  constexpr float pi = 3.14159265358979323846f;
  float angle = moving ? SDL_GetTicks() / 700.0f : 0;
  SDL_FPoint points[33];
  for (int i = 0; i <= 32; ++i) {
    float a = angle + i * pi / 16, d = (i % 4 == 1 || i % 4 == 2) ? radius : radius * .72f;
    points[i] = {x + std::cos(a) * d, y + std::sin(a) * d};
  }
  SDL_SetRenderDrawColor(r, 255, 228, 173, 255);
  SDL_RenderDrawLinesF(r, points, 33);
  SDL_FRect centre{x - 1, y - 1, 2, 2};
  SDL_RenderDrawRectF(r, &centre);
}
}

UiUpdate::UiUpdate(ResourceManager *resources) : rm(resources) {
  std::string source = layout();
  panel = std::make_unique<UiLayout>(new IniReader(source.data(), source.size()), rm);
  if (auto *t = dynamic_cast<UiText *>(panel->getElementById(kHeading))) t->setText("ZT1 Engine Update");
  for (int id : {kLater, kClose})
    if (auto *b = dynamic_cast<UiButton *>(panel->getElementById(id))) b->onClick = [this] { dismiss(); };
  if (auto *b = dynamic_cast<UiButton *>(panel->getElementById(kPrimary))) b->onClick = [this] { primary(); };
}
UiUpdate::~UiUpdate() = default;

// addition: copy the exact native Version_Label, changing only its identity and
// horizontal alignment. Its font IDs, fitfont, border, colour and shadow stay native.
std::string UiUpdate::withStatus(const std::string &startup) {
  std::string low = Utils::string_to_lower(startup);
  size_t from = low.find("[version_label]");
  if (from == std::string::npos) return startup;
  size_t end = low.find("\n[", from + 1);
  std::string copy = startup.substr(from, end == std::string::npos ? std::string::npos : end - from);
  copy.replace(0, copy.find(']') + 1, "[Update_Status]");
  // IniReader treats repeated keys as lists. Replace these scalar keys,
  // preserving the original list-valued colour and all vertical metrics.
  std::stringstream lines(copy);
  std::string line, filtered;
  while (std::getline(lines, line)) {
    auto eq = line.find('=');
    std::string key = Utils::string_to_lower(line.substr(0, eq));
    key.erase(std::remove_if(key.begin(), key.end(), [](char c) { return c == ' ' || c == '\t' || c == '\r'; }), key.end());
    if (eq != std::string::npos && (key == "id" || key == "textid" || key == "x" || key == "justify" || key == "dx")) continue;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    filtered += line + "\r\n";
  }
  filtered += "id=7120\r\ntextid=0\r\nx=18\r\njustify=left\r\ndx=342\r\n";
  return startup + "\r\n" + filtered + "\r\n";
}
Updater::Snapshot UiUpdate::current() const { return preview ? *preview : Updater::snapshot(); }
void UiUpdate::open() { visible = true; refresh(); }
void UiUpdate::dismiss() { visible = false; installWhenReady = false; installAt = 0; }
void UiUpdate::tick() {
  if (!preview) {
    Updater::Release release;
    if (Updater::takeOffer(release)) visible = true;
    if (installWhenReady && Updater::state() == Updater::State::Ready) {
      if (!installAt) installAt = SDL_GetTicks() + 300;
      if (SDL_TICKS_PASSED(SDL_GetTicks(), installAt)) {
        installWhenReady = false;
        if (!Updater::installNow(error)) visible = true;
      }
    }
    if (Updater::state() == Updater::State::Failed) { installWhenReady = false; installAt = 0; }
  }
  refresh();
}
void UiUpdate::primary() {
  if (preview) return;
  error.clear();
  auto s = Updater::state();
  if (s == Updater::State::Available) {
    installWhenReady = Updater::downloadNow();
    installAt = 0;
  } else if (s == Updater::State::Ready) {
    if (!Updater::installNow(error)) visible = true;
  } else Updater::checkNow();
}
void UiUpdate::refresh() {
  auto snap = current();
  auto say = [&](int id, const std::string &value) {
    if (auto *t = dynamic_cast<UiText *>(panel->getElementById(id)))
      if (t->getText() != value) t->setText(value);
  };
  std::string state = label(snap.state);
  if (spinning(snap.state)) state += std::string((SDL_GetTicks() / 400) % 4, '.');
  say(kState, state);
  std::string ver = Updater::version();
  if (!snap.release.version.empty()) ver += " -> " + snap.release.version;
  say(kVersion, ver);
  std::string message;
  using S = Updater::State;
  if (!error.empty()) message = error;
  else switch (snap.state) {
    case S::Available: message = "Update now downloads, verifies and installs this update, then reopens the game. Your saved zoos and settings are kept."; break;
    case S::Downloading: {
      message = bytes(snap.downloadedBytes) + " / " + bytes(snap.totalBytes);
      if (snap.bytesPerSecond > 0) message += "   " + bytes(static_cast<uint64_t>(snap.bytesPerSecond)) + "/s";
      if (snap.secondsRemaining >= 0) message += "\nAbout " + std::to_string(static_cast<int>(std::ceil(snap.secondsRemaining))) + " seconds left";
      break;
    }
    case S::Ready: message = installWhenReady ? "The download is verified. Starting the updater to install and reopen the game..."
                                            : "The download is verified. Choose Update now to install and restart."; break;
    case S::Verifying: message = "Checking the installer signature, size and file integrity."; break;
    case S::Installing: message = "The updater stays visible while the game closes, installs and reopens."; break;
    case S::UpToDate: message = "You have the latest version."; break;
    default: message = snap.status; break;
  }
  if (auto *t = dynamic_cast<UiText *>(panel->getElementById(kMessage)))
    if (t->getText() != message) t->setMessage(message);
  if (auto *b = dynamic_cast<UiButton *>(panel->getElementById(kLater))) b->setLabel("Later");
  if (auto *b = dynamic_cast<UiButton *>(panel->getElementById(kPrimary))) {
    bool active = snap.state == S::Available || snap.state == S::Ready || snap.state == S::Failed || snap.state == S::UpToDate || snap.state == S::Off;
    b->setDisabled(!active);
    b->setLabel(snap.state == S::Available || snap.state == S::Ready ? "Update now" : "Check now");
    if (!preview && !Updater::possible(nullptr)) b->setDisabled(true);
  }
}
bool UiUpdate::handleInputs(std::vector<Input> &inputs, UiLayout *menu) {
  if (visible) {
    for (const auto &in : inputs) {
      if (in.event == InputEvent::KEY_ESCAPE) { dismiss(); return true; }
      if (in.event == InputEvent::KEY_DOWN && (in.key == SDLK_RETURN || in.key == SDLK_KP_ENTER)) primary();
    }
    if (visible) panel->handleInputs(inputs);
    return true;
  }
  if (menu && footer.w > 0)
    for (const auto &in : inputs) {
      SDL_Point p = in.position;
      if (in.event == InputEvent::LEFT_CLICK && SDL_PointInRect(&p, &footer)) { open(); return true; }
    }
  return false;
}
void UiUpdate::prepareFooter(UiLayout *menu) {
  if (!menu) return;
  if (auto *t = dynamic_cast<UiText *>(menu->getElementById(kStatus))) {
    auto snap = current();
    std::string value = label(snap.state);
    if (spinning(snap.state)) value += std::string((SDL_GetTicks() / 400) % 4, '.');
    if (t->getText() != value) t->setText(value);
  }
}
void UiUpdate::draw(SDL_Renderer *r, UiLayout *menu) {
  auto snap = current();
  if (menu) {
    if (auto *t = dynamic_cast<UiText *>(menu->getElementById(kStatus))) {
      footer = t->getLastRect();
      gear(r, footer.x - 11.0f, footer.y + footer.h / 2.0f, 5, spinning(snap.state));
    }
  }
  if (!visible) return;
  SDL_Rect box{(800-kWidth)/2, (600-kHeight)/2, kWidth, kHeight};
  SDL_SetRenderDrawColor(r, 37, 58, 44, 255); SDL_RenderFillRect(r, &box);
  SDL_SetRenderDrawColor(r, 133, 147, 93, 255); SDL_RenderDrawRect(r, &box);
  SDL_RenderDrawLine(r, box.x, box.y + 33, box.x + box.w - 1, box.y + 33);
  panel->draw(r, &box);
  if (snap.state == Updater::State::Downloading && snap.totalBytes > 0) {
    SDL_Rect track{box.x + 16, box.y + 96, box.w - 32, 3};
    SDL_SetRenderDrawColor(r, 66, 86, 58, 255); SDL_RenderFillRect(r, &track);
    track.w = static_cast<int>(track.w * std::clamp(snap.downloadedBytes / static_cast<double>(snap.totalBytes), 0.0, 1.0));
    SDL_SetRenderDrawColor(r, 237, 218, 150, 255); SDL_RenderFillRect(r, &track);
  }
}

// ours: deterministic native UI checks. These preview snapshots cannot download
// or invoke an installer; production state and the HD worker are untouched.
int UiUpdate::checks(SDL_Renderer *r, ResourceManager *rm, const std::string &out) {
  std::filesystem::create_directories(out);
  UiUpdate ui(rm);
  std::string menuSource = withStatus(rm->getGameText("ui/startup.lyt"));
  UiLayout menu(new IniReader(menuSource.data(), menuSource.size()), rm);
  std::ofstream log(out + "/checks.txt");
  int failed = 0;
  auto check = [&](bool ok, const std::string &name) { log << (ok ? "PASS " : "FAIL ") << name << '\n'; if (!ok) failed++; };
  auto *native = dynamic_cast<UiText *>(menu.getElementById(7119));
  auto *status = dynamic_cast<UiText *>(menu.getElementById(kStatus));
  if (native) native->setText(std::string("Version Number: ") + Updater::version());
  check(native && status, "version and status labels exist");
  auto *ini = menu.getIniReader();
  if (native && status)
    for (const char *key : {"font", "fontsize", "y", "dy", "border", "forecolor", "shadowtext"})
      check(ini->get(native->getName(), key) == ini->get(status->getName(), key), std::string("native footer ") + key);
  SDL_RenderSetScale(r, 1, 1);
  SDL_RenderSetViewport(r, nullptr);
  for (auto s : {Updater::State::Checking, Updater::State::Available, Updater::State::Downloading, Updater::State::Verifying, Updater::State::Ready, Updater::State::UpToDate, Updater::State::Failed}) {
    Updater::Snapshot snap{};
    snap.state = s; snap.release.version = "2099.1.1"; snap.downloadedBytes = 84 * 1024 * 1024;
    snap.totalBytes = 128 * 1024 * 1024; snap.bytesPerSecond = 6.2 * 1024 * 1024; snap.secondsRemaining = 7;
    snap.status = "Offline verification example: no network request was made.";
    ui.preview = snap; ui.visible = s != Updater::State::Checking; ui.refresh();
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255); SDL_RenderClear(r);
    SDL_Rect screen{0, 0, 800, 600}; ui.prepareFooter(&menu); menu.draw(r, &screen); ui.draw(r, &menu);
    if (native && status) check(native->getLastRect().y == status->getLastRect().y, "footer baseline " + label(s));
    if (ui.visible) {
      SDL_Rect bounds{(800-kWidth)/2, (600-kHeight)/2, kWidth, kHeight};
      for (int id : {kHeading, kState, kVersion, kMessage, kLater, kPrimary, kClose}) {
        auto rect = ui.panel->getElementById(id)->getLastRect();
        check(rect.x >= bounds.x && rect.y >= bounds.y && rect.x + rect.w <= bounds.x + bounds.w &&
              rect.y + rect.h <= bounds.y + bounds.h, "dialog contains element " + std::to_string(id));
      }
    }
    SDL_Rect capture{0, 0, 800, 600};
    SDL_Surface *image = SDL_CreateRGBSurfaceWithFormat(0, capture.w, capture.h, 32, SDL_PIXELFORMAT_ARGB8888);
    bool saved = image && SDL_RenderReadPixels(r, &capture, SDL_PIXELFORMAT_ARGB8888, image->pixels, image->pitch) == 0 &&
        IMG_SavePNG(image, (out + "/update-" + std::to_string(static_cast<int>(s)) + ".png").c_str()) == 0;
    if (image) SDL_FreeSurface(image);
    check(saved, "capture " + label(s));
    SDL_RenderPresent(r);
  }
  ui.installWhenReady = true; ui.installAt = SDL_GetTicks() + 300; ui.visible = true;
  Input escape{}; escape.event = InputEvent::KEY_ESCAPE;
  std::vector<Input> inputs{escape};
  check(ui.handleInputs(inputs, &menu) && !ui.visible && !ui.installWhenReady && !ui.installAt,
        "Escape dismisses and cancels automatic install");
  return UiOptions::updateChecks(r, rm, out) || failed ? 1 : 0;
}
