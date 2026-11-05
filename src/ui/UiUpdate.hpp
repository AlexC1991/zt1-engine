#ifndef UI_UPDATE_HPP
#define UI_UPDATE_HPP

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "../Input.hpp"
#include "../Updater.hpp"

class ResourceManager;
class UiLayout;

// addition: installed-game update feedback, using the original menu's version
// text metrics and the game's own buttons. Network/install work stays in Updater.
class UiUpdate {
public:
  explicit UiUpdate(ResourceManager *rm);
  ~UiUpdate();
  static std::string withStatus(const std::string &startup);
  void tick();
  void prepareFooter(UiLayout *menu);
  void open();
  void dismiss();
  bool isOpen() const { return visible; }
  // Inputs and drawing use the main menu's 800 x 600 layout coordinates.
  bool handleInputs(std::vector<Input> &inputs, UiLayout *menu);
  void draw(SDL_Renderer *renderer, UiLayout *menu);
  static int checks(SDL_Renderer *renderer, ResourceManager *rm, const std::string &out);

private:
  ResourceManager *rm;
  std::unique_ptr<UiLayout> panel;
  bool visible = false, installWhenReady = false;
  Uint32 installAt = 0;
  SDL_Rect footer = {};
  std::string error;
  std::optional<Updater::Snapshot> preview;
  Updater::Snapshot current() const;
  void refresh();
  void primary();
};
#endif
