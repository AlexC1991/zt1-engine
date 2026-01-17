#include "UiListBox.hpp"
#include <algorithm>

UiListBox::UiListBox(IniReader* ini_reader, ResourceManager* resource_manager, std::string name) {
    this->name = name;
    this->ini_reader = ini_reader;
    this->resource_manager = resource_manager;

    this->id = ini_reader->getInt(name, "id", 0);
    this->layer = ini_reader->getInt(name, "layer", 1);
    this->anchor = ini_reader->getInt(name, "anchor", 0);

    this->x = ini_reader->getInt(name, "x", 0);
    this->y = ini_reader->getInt(name, "y", 0);
    this->dx = ini_reader->getInt(name, "dx", 100);
    this->dy = ini_reader->getInt(name, "dy", 200);

    this->font_id = ini_reader->getInt(name, "font", 14002);

    parseColors(ini_reader, name, "forecolor", forecolor);
    parseColors(ini_reader, name, "backcolor", backcolor);
    parseColors(ini_reader, name, "highlightcolor", highlightcolor);
    parseColors(ini_reader, name, "selectcolor", selectcolor);
    parseColors(ini_reader, name, "selectbackcolor", selectbackcolor);

    this->transparent = ini_reader->getInt(name, "transparent", 1) == 1;
    this->border = ini_reader->getInt(name, "border", 2);

    this->item_height = 22;
    this->visible_items = (dy - border * 2) / item_height;

    SDL_Log("Created UiListBox: %s (id=%d, %dx%d, visible=%d items)",
            name.c_str(), id, dx, dy, visible_items);
}

UiListBox::~UiListBox() {
    clear();
}

void UiListBox::parseColors(IniReader* ini, const std::string& section, const std::string& key, SDL_Color& color) {
    std::vector<std::string> values = ini->getList(section, key);
    if (values.size() >= 3) {
        try {
            color.r = std::stoi(values[0]);
            color.g = std::stoi(values[1]);
            color.b = std::stoi(values[2]);
            color.a = 255;
        } catch (...) {}
    }
}

void UiListBox::addItem(const std::string& text, const std::string& data, const std::string& icon) {
    ListBoxItem item;
    item.text = text;
    item.data = data;
    item.textId = 0;
    item.iconPath = icon;
    item.iconTex = nullptr;
    item.loadAttempted = false; // Reset flag
    items.push_back(item);
}

void UiListBox::addItem(uint32_t textId, const std::string& data, const std::string& icon) {
    ListBoxItem item;
    item.textId = textId;
    item.data = data;
    item.iconPath = icon;
    item.iconTex = nullptr;
    item.loadAttempted = false; // Reset flag

    item.text = resource_manager->getString(textId);
    if (item.text.empty()) {
        item.text = "String #" + std::to_string(textId);
    }
    items.push_back(item);
}

void UiListBox::clear() {
    for (auto& item : items) {
        if (item.iconTex) {
            SDL_DestroyTexture(item.iconTex);
            item.iconTex = nullptr;
        }
    }
    items.clear();
    selected_index = -1;
    hover_index = -1;
    scroll_offset = 0;
}

std::string UiListBox::getSelectedData() const {
    if (selected_index >= 0 && selected_index < (int)items.size()) {
        return items[selected_index].data;
    }
    return "";
}

std::string UiListBox::getSelectedText() const {
    if (selected_index >= 0 && selected_index < (int)items.size()) {
        return items[selected_index].text;
    }
    return "";
}

int UiListBox::getItemAtPoint(int px, int py) {
    if (px < cached_rect.x || px > cached_rect.x + cached_rect.w ||
        py < cached_rect.y || py > cached_rect.y + cached_rect.h) {
        return -1;
    }
    int relative_y = py - cached_rect.y - border;
    int index = scroll_offset + (relative_y / item_height);
    if (index >= 0 && index < (int)items.size()) return index;
    return -1;
}

SDL_Texture* UiListBox::loadIconTexture(SDL_Renderer* renderer, const std::string& path) {
    if (path.empty()) return nullptr;

    // [FIX] Improved path detection for ZT1 assets
    // If passed "ui/scenario/iconp/iconp", it constructs:
    // Raw: "ui/scenario/iconp/N" (Assuming folder structure)
    // Pal: "ui/scenario/iconp/iconp.pal"
    if (path.find('.') == std::string::npos) {
        // Assume path is the "base name" (e.g. .../iconp)
        // Check if it looks like a folder structure
        size_t lastSlash = path.find_last_of("/\\");
        std::string folder = (lastSlash == std::string::npos) ? path : path.substr(0, lastSlash);

        // ZT1 sprites are usually inside a folder with an 'N' file
        // We will try to find "N" in that folder.
        std::string rawPath = folder + "/N";
        std::string palPath = path + ".pal";

        return resource_manager->getZt1Texture(renderer, rawPath, palPath);
    }
    else {
        return resource_manager->getTexture(renderer, path);
    }
}

UiAction UiListBox::handleInputs(std::vector<Input>& inputs) {
    UiAction result = UiAction::NONE;
    for (const Input& input : inputs) {
        int mx = input.position.x;
        int my = input.position.y;

        if (input.event == InputEvent::CURSOR_MOVE) {
            hover_index = getItemAtPoint(mx, my);
        }
        else if (input.event == InputEvent::LEFT_CLICK) {
            int clicked = getItemAtPoint(mx, my);
            if (clicked >= 0 && clicked != selected_index) {
                selected_index = clicked;
                if (selection_action != UiAction::NONE) result = selection_action;
            }
        }
        else if (input.event == InputEvent::SCROLL_UP) {
            if (getItemAtPoint(mx, my) >= 0 ||
                (mx >= cached_rect.x && mx <= cached_rect.x + cached_rect.w &&
                 my >= cached_rect.y && my <= cached_rect.y + cached_rect.h)) {
                if (scroll_offset > 0) scroll_offset--;
            }
        }
        else if (input.event == InputEvent::SCROLL_DOWN) {
            if (getItemAtPoint(mx, my) >= 0 ||
                (mx >= cached_rect.x && mx <= cached_rect.x + cached_rect.w &&
                 my >= cached_rect.y && my <= cached_rect.y + cached_rect.h)) {
                int max_scroll = std::max(0, (int)items.size() - visible_items);
                if (scroll_offset < max_scroll) scroll_offset++;
            }
        }
    }
    UiAction childAction = handleInputChildren(inputs);
    if (childAction != UiAction::NONE) result = childAction;
    return result;
}

void UiListBox::draw(SDL_Renderer* renderer, SDL_Rect* layout_rect) {
    cached_rect.x = layout_rect->x + x;
    cached_rect.y = layout_rect->y + y;
    cached_rect.w = dx;
    cached_rect.h = dy;

    if (!transparent) {
        SDL_SetRenderDrawColor(renderer, backcolor.r, backcolor.g, backcolor.b, backcolor.a);
        SDL_RenderFillRect(renderer, &cached_rect);
    }
    if (border > 0) {
        SDL_SetRenderDrawColor(renderer, forecolor.r, forecolor.g, forecolor.b, 255);
        SDL_RenderDrawRect(renderer, &cached_rect);
    }

    int item_y = cached_rect.y + border;
    int max_items = std::min(visible_items, (int)items.size() - scroll_offset);

    for (int i = 0; i < max_items; i++) {
        int item_index = scroll_offset + i;
        if (item_index >= (int)items.size()) break;

        ListBoxItem& item = items[item_index];

        SDL_Rect item_rect = {
            cached_rect.x + border,
            item_y,
            cached_rect.w - border * 2 - 20,
            item_height
        };

        SDL_Color* bg = nullptr;
        SDL_Color* fg = &forecolor;

        if (item_index == selected_index) {
            bg = &selectbackcolor;
            fg = &selectcolor;
            SDL_SetRenderDrawColor(renderer, bg->r, bg->g, bg->b, 180);
            SDL_RenderFillRect(renderer, &item_rect);
            SDL_SetRenderDrawColor(renderer, 255, 217, 90, 255);
            SDL_RenderDrawRect(renderer, &item_rect);
        }
        else if (item_index == hover_index) {
            SDL_SetRenderDrawColor(renderer, highlightcolor.r, highlightcolor.g, highlightcolor.b, 100);
            SDL_RenderFillRect(renderer, &item_rect);
        }

        // --- FIXED LAZY LOAD ---
        // Only try to load if we haven't tried before.
        if (item.iconTex == nullptr && !item.iconPath.empty() && !item.loadAttempted) {
            item.iconTex = loadIconTexture(renderer, item.iconPath);
            item.loadAttempted = true; // Stop asking if it fails!
        }

        int text_x_offset = 4;
        if (item.iconTex) {
            SDL_Rect iconRect = {
                item_rect.x + 2,
                item_rect.y + (item_height - 18) / 2,
                18, 18
            };
            SDL_SetTextureBlendMode(item.iconTex, SDL_BLENDMODE_BLEND);
            SDL_RenderCopy(renderer, item.iconTex, nullptr, &iconRect);
            text_x_offset = 24;
        }

        if (!item.text.empty()) {
            SDL_Texture* text_texture = resource_manager->getStringTexture(
                renderer, font_id, item.text, *fg);

            if (text_texture) {
                int tex_w, tex_h;
                SDL_QueryTexture(text_texture, nullptr, nullptr, &tex_w, &tex_h);
                SDL_Rect text_rect = {
                    item_rect.x + text_x_offset,
                    item_rect.y + (item_height - tex_h) / 2,
                    tex_w, tex_h
                };
                if (text_rect.w > item_rect.w - text_x_offset) {
                    text_rect.w = item_rect.w - text_x_offset;
                }
                SDL_RenderCopy(renderer, text_texture, nullptr, &text_rect);
            }
        }
        item_y += item_height;
    }

    if ((int)items.size() > visible_items) {
        int scrollbar_x = cached_rect.x + cached_rect.w - 15;
        int scrollbar_h = cached_rect.h - border * 2;
        int thumb_h = std::max(20, scrollbar_h * visible_items / (int)items.size());
        int max_scroll = items.size() - visible_items;
        int thumb_y = cached_rect.y + border;
        if (max_scroll > 0) thumb_y += (scrollbar_h - thumb_h) * scroll_offset / max_scroll;

        SDL_SetRenderDrawColor(renderer, 60, 50, 30, 200);
        SDL_Rect track = {scrollbar_x, cached_rect.y + border, 12, scrollbar_h};
        SDL_RenderFillRect(renderer, &track);
        SDL_SetRenderDrawColor(renderer, 150, 140, 100, 255);
        SDL_Rect thumb = {scrollbar_x + 1, thumb_y, 10, thumb_h};
        SDL_RenderFillRect(renderer, &thumb);
    }
    drawChildren(renderer, &cached_rect);
}