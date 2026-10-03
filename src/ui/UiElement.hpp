#ifndef UI_ELEMENT_HPP
#define UI_ELEMENT_HPP

#include <vector>
#include <string>
#include <map>
#include <cstdlib>

#include <SDL2/SDL.h>

#include "UiAction.hpp"

#include "../IniReader.hpp"
#include "../ResourceManager.hpp"
#include "../Input.hpp"

#define FONT_SIZE 24

class UiElement {
public:
  virtual ~UiElement() {};

  virtual UiAction handleInputs(std::vector<Input> &inputs) = 0;
  virtual void draw(SDL_Renderer * renderer, SDL_Rect * layout_rect) = 0;

  std::string getName() {return this->name;};
  int getLayer() {return this->layer;};
  int getId() {return this->id;};  // [PATCH] Get element ID
  int getAnchor() {return this->anchor;};

  // Layout "state" flags (bits OR'd over every state= line):
  //   1 hidden, 2 disabled, 16 always hidden, 2048 toggle, 4096 sticky
  void setStateFlags(int flags) {
    this->state_flags = flags;
    this->hidden = (flags & (1 | 16)) != 0;
  }
  int getStateFlags() const { return this->state_flags; }
  bool isHidden() const { return this->hidden; }
  bool isDisabled() const { return (this->state_flags & 2) != 0; }
  void setDisabled(bool d) {
    this->state_flags = d ? (this->state_flags | 2) : (this->state_flags & ~2);
  }
  void setHidden(bool h) { this->hidden = h; }

  // Where this element is (its own rect, as it will be drawn) inside the
  // given parent rect. Elements whose size comes from their art override it.
  virtual SDL_Rect computeRect(SDL_Renderer *renderer, SDL_Rect *parent_rect) {
    (void)renderer;
    return this->getRect(this->ini_reader->getSection(this->name), parent_rect);
  }

  // Children, for layouts that draw the whole tree themselves
  const std::vector<UiElement *> &getChildren() const { return this->children; }
  // When set, draw() leaves children to the owning layout, which draws
  // every element in layer order (as the original does)
  void setLayoutDrawsChildren(bool v) { this->layout_draws_children = v; }

  // Rect last drawn at (screen/layout units), for anchors and hit tests
  SDL_Rect getLastRect() const { return this->last_rect; }

  UiElement *findById(int targetId) {
    if (this->id == targetId)
      return this;
    for (UiElement *child : this->children)
      if (UiElement *found = child->findById(targetId))
        return found;
    return nullptr;
  }

  bool hasId(int id) {
    if (id == this->id) {
      return true;
    }
    for (UiElement * child : this->children) {
      if (child->hasId(id)) {
        return true;
      }
    }
    return false;
  }

  void addChild(UiElement * new_child) {
    if(new_child->anchor == this->id) {
      this->children.push_back(new_child);
      return;
    } else {
      for (UiElement * child : this->children) {
        if (child->hasId(new_child->anchor)) {
          child->addChild(new_child);
          return;
        }
      }
    }
    SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "This code should never be reached, which was the child not added?");
  }

protected:
  IniReader * ini_reader = nullptr;
  ResourceManager * resource_manager = nullptr;
  std::string name;
  int id = 0;
  int layer = 0;
  int anchor = 0;
  int state_flags = 0;
  bool hidden = false;
  bool layout_draws_children = false;
  SDL_Rect last_rect = {0, 0, 0, 0};

  std::vector<UiElement*> children;

  void drawChildren(SDL_Renderer * renderer, SDL_Rect * parent_rect) {
    if (this->layout_draws_children)
      return;
    for (int layer=0; layer < (8 + 1); layer++) {
      for (UiElement * child : this->children) {
        if (child->layer == layer && !child->hidden) {
          child->draw(renderer, parent_rect);
        }
      }
    }
  }

  UiAction handleInputChildren(std::vector<Input> &inputs) {
    UiAction action = UiAction::NONE;
    for (UiElement * child : this->children) {
      if (child->hidden)
        continue;
      UiAction new_action = child->handleInputs(inputs);
      if (new_action != UiAction::NONE) {
        action = new_action;
      }
    }
    return action;
  }

  SDL_Rect getRect(std::map<std::string, std::string> map, SDL_Rect * layout_rect) {
    SDL_Rect rect = {0, 0, 0, 0};
    // Missing or non-numeric values count as 0 (e.g. scrollbars, which are
    // placed by the element that owns them)
    auto num = [](const std::string &v) {
      return std::atoi(v.c_str());
    };

    if (map.contains("dx")) {
      if (map["dx"] == "whole") {
        rect.w = layout_rect->w;
      } else {
        rect.w = num(map["dx"]);
      }
    }

    if (map.contains("dy") && map["dy"] != "fitfont") {
       if (map["dy"] == "whole") {
        rect.h = layout_rect->h;
      } else {
        rect.h = num(map["dy"]);
      }
    }

    if (map["x"] == "center") {
      rect.x = layout_rect->w / 2 - rect.w / 2;
    } else if (map["x"] == "right") {
      rect.x = layout_rect->w - rect.w;
    } else if (map["x"] == "left") {
      rect.x = 0;
    } else {
      rect.x = num(map["x"]);
    }

    if (map["y"] == "center") {
      rect.y = layout_rect->h / 2 - rect.h / 2;
    } else if (map["y"] == "bottom") {
      rect.y = layout_rect->h - rect.h;
    } else if (map["y"] == "top") {
      rect.y = 0;
    } else {
      rect.y = num(map["y"]);
    }

    if (map.contains("justify")) {
      if (map["justify"] == "center") {
        rect.x += rect.w / 2;
      } else if (map["justify"] == "right") {
        rect.x += rect.w;
      }
    }

    rect.x += layout_rect->x;
    rect.y += layout_rect->y;

    return rect;
  };
};

#endif // UI_ELEMENT_HPP