#include "UiListBox.hpp"
#include "../RenderSettings.hpp"
#include <algorithm>
#include <sstream>

#include "../Animation.hpp"
#include "../ArtScaler.hpp"
#include "../CompassDirection.hpp"

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
    this->font_size_id = ini_reader->getInt(name, "fontsize", 0);
    this->scrollbar_id = ini_reader->getInt(name, "scrollbar", 0);

    parseColors(ini_reader, name, "forecolor", forecolor);
    parseColors(ini_reader, name, "backcolor", backcolor);
    parseColors(ini_reader, name, "highlightcolor", highlightcolor);
    parseColors(ini_reader, name, "selectcolor", selectcolor);
    // (a list without its own selection fill uses its backcolor: the exhibit
    // list's gold-brown, the Commerce Building List's grey)
    selectbackcolor = backcolor;
    parseColors(ini_reader, name, "selectbackcolor", selectbackcolor);

    this->transparent = ini_reader->getInt(name, "transparent", 1) == 1;
    this->border = ini_reader->getInt(name, "border", 2);

    // Rows are one pixel taller than the font (GDI's spacing): 16 px for
    // Arial Bold 10, which gives the original's 29 rows in the map list
    this->item_height = std::max(8, resource_manager->getFontLineHeight(
                                        font_id, font_size_id) + 1);
    this->visible_items = std::max(1, (dy - border * 2) / item_height);

    this->centered = ini_reader->get(name, "justify") == "center";
    this->toggle_select = ini_reader->getInt(name, "toggleselect", 0) == 1;
    this->spacing = std::max(0, ini_reader->getInt(name, "spacing", 0));
    this->minicon_width = ini_reader->getInt(name, "miniconwidth", 0);
    for (const std::string& c : ini_reader->getList(name, "column"))
        this->columns.push_back(std::atoi(c.c_str()));
    this->line_height = resource_manager->getFontLineHeight(font_id, font_size_id);
    // Lists with mini icons (the exhibit list's tank icons, 30 x 30) have
    // rows tall enough for them (measured: 30 px with its frame)
    if (this->minicon_width >= 30 && !this->toggle_select) {
        this->item_height = std::max(this->item_height, 30);
        this->visible_items = std::max(1, (dy - border * 2) / item_height);
    }
    if (this->toggle_select)
        this->checkbox = resource_manager->getAnimation("ui/sharedui/checkbx/checkbx");

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

// toggleselect lists: an item is as tall as its wrapped lines (measured:
// 30 px for two lines of Arial Bold 9, 16 for one)
int UiListBox::itemHeight(const ListBoxItem& item) const {
    int lines = std::max<int>(1, (int)item.lines.size());
    return lines * line_height + (wrapped ? spacing : 2);
}

void UiListBox::addItem(const std::string& text, Animation* iconArt) {
    addItem(text);
    items.back().iconArt = iconArt;
}

void UiListBox::addRow(const std::vector<std::string>& cells, const std::vector<SDL_Color>& colors) {
    addItem(cells.empty() ? std::string() : cells.front());
    items.back().cells = cells;
    items.back().cellColors = colors;
}

void UiListBox::addItem(const std::string& text, SDL_Color color) {
    addItem(text);
    items.back().hasColor = true;
    items.back().color = color;
}

int UiListBox::fittingFrom(int first) const {
    int room = dy - border * 2, n = 0;
    for (int i = first; i < (int)items.size(); i++) {
        room -= itemHeight(items[i]);
        if (room < 0) break;
        n++;
    }
    return std::max(1, n);
}

int UiListBox::getScrollMaximum() const {
    if (toggle_select || wrapped) {
        // Scrolls until the last item is in view
        int room = dy - border * 2;
        int first = (int)items.size();
        while (first > 0 && room - itemHeight(items[first - 1]) >= 0) {
            room -= itemHeight(items[first - 1]);
            first--;
        }
        return first;
    }
    return std::max(0, (int)items.size() - visible_items);
}

void UiListBox::setScrollPosition(int position) {
    scroll_offset = std::clamp(position, 0, getScrollMaximum());
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

void UiListBox::wrapItems(SDL_Renderer* renderer) {
    int room = dx - minicon_width;
    auto width = [&](const std::string& s) {
        SDL_Texture* t = resource_manager->getStringTexture(
            renderer, font_id, s, forecolor, font_size_id);
        int w = 0, h = 0;
        if (t) resource_manager->getTextSize(t, &w, &h);
        return w;
    };
    for (ListBoxItem& item : items) {
        if (!item.lines.empty()) continue;
        std::istringstream words(item.text);
        std::string word, current;
        while (words >> word) {
            std::string candidate = current.empty() ? word : current + " " + word;
            if (!current.empty() && width(candidate) > room) {
                item.lines.push_back(current);
                current = word;
            } else {
                current = candidate;
            }
        }
        if (!current.empty() || item.lines.empty()) item.lines.push_back(current);
    }
}

int UiListBox::getItemAtPoint(int px, int py) {
    if (px < cached_rect.x || px > cached_rect.x + cached_rect.w ||
        py < cached_rect.y || py > cached_rect.y + cached_rect.h) {
        return -1;
    }
    int relative_y = py - cached_rect.y - border;
    if (toggle_select || wrapped) {
        for (int i = scroll_offset; i < (int)items.size(); i++) {
            relative_y -= itemHeight(items[i]);
            if (relative_y < 0) return i;
        }
        return -1;
    }
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
            if (toggle_select && clicked >= 0) {
                items[clicked].checked = !items[clicked].checked;
                if (selection_action != UiAction::NONE) result = selection_action;
            } else if (clicked >= 0 && clicked != selected_index) {
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
                setScrollPosition(scroll_offset + 1);
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

    // Art and text are drawn in separate passes under GPU FSR
    const bool art = RenderSettings::drawsUiArt();
    if (!transparent && art) {
        SDL_SetRenderDrawColor(renderer, backcolor.r, backcolor.g, backcolor.b, backcolor.a);
        SDL_RenderFillRect(renderer, &cached_rect);
    }
    // No outline: border= is only the inset (the original's map, filter and
    // research lists have none)

    int item_y = cached_rect.y + border;
    int max_items = std::min(visible_items, (int)items.size() - scroll_offset);

    if (toggle_select || wrapped) {
        // Checkbox in the mini-icon column, then the name wrapped to the
        // rest of the width
        wrapItems(renderer);
        int count = std::min(fittingFrom(scroll_offset),
                             (int)items.size() - scroll_offset);
        for (int i = 0; i < count; i++) {
            ListBoxItem& item = items[scroll_offset + i];
            int h = itemHeight(item);
            if (toggle_select && checkbox && art) {
                int bw = 0, bh = 0;
                checkbox->queryTexture(CompassDirection::N, &bw, &bh);
                SDL_Rect box = {cached_rect.x + (minicon_width - bw) / 2,
                                item_y + (h - bh) / 2, bw, bh};
                checkbox->draw(renderer, &box, item.checked ? CompassDirection::S
                                                            : CompassDirection::N);
            }
            int ty = wrapped ? item_y : item_y + (h - (int)item.lines.size() * line_height) / 2;
            for (const std::string& line : item.lines) {
                SDL_Texture* t = resource_manager->getStringTexture(
                    renderer, font_id, line, item.hasColor ? item.color : forecolor, font_size_id);
                if (t && RenderSettings::drawsUiText()) {
                    int tw = 0, th = 0;
                    resource_manager->getTextSize(t, &tw, &th);
                    SDL_Rect dst = {cached_rect.x + minicon_width, ty, tw, th};
                    SDL_RenderCopy(renderer, t, nullptr, &dst);
                }
                ty += line_height;
            }
            item_y += h;
        }
        drawChildren(renderer, &cached_rect);
        return;
    }

    for (int i = 0; i < max_items; i++) {
        int item_index = scroll_offset + i;
        if (item_index >= (int)items.size()) break;

        ListBoxItem& item = items[item_index];

        // Measured from the original: the selection box starts 2 px left
        // of the list and runs to its right edge (the scrollbar sits outside)
        SDL_Rect item_rect = {
            cached_rect.x - 2,
            item_y,
            cached_rect.w + 2,
            item_height
        };

        SDL_Color* bg = nullptr;
        SDL_Color* fg = &forecolor;

        if (item_index == selected_index) {
            bg = &selectbackcolor;
            fg = &selectcolor;
            SDL_SetRenderDrawColor(renderer, bg->r, bg->g, bg->b, 255);
            if (art) SDL_RenderFillRect(renderer, &item_rect);
            SDL_SetRenderDrawColor(renderer, 255, 217, 90, 255);
            if (art) SDL_RenderDrawRect(renderer, &item_rect);
        }
        else if (item_index == hover_index) {
            SDL_SetRenderDrawColor(renderer, highlightcolor.r, highlightcolor.g, highlightcolor.b, 100);
            if (art) SDL_RenderFillRect(renderer, &item_rect);
        }

        // --- FIXED LAZY LOAD ---
        // Only try to load if we haven't tried before.
        if (item.iconTex == nullptr && !item.iconPath.empty() && !item.loadAttempted) {
            item.iconTex = loadIconTexture(renderer, item.iconPath);
            item.loadAttempted = true; // Stop asking if it fails!
        }

        int text_x_offset = minicon_width > 0 ? minicon_width + 2 : 2;
        if (item.iconArt) {
            int iw = 0, ih = 0;
            item.iconArt->queryTexture(CompassDirection::N, &iw, &ih);
            SDL_Rect iconRect = {item_rect.x + 3 + std::max(0, (minicon_width - 6 - iw) / 2),
                                 item_rect.y + (item_height - ih) / 2, iw, ih};
            if (art)
                item.iconArt->draw(renderer, &iconRect, CompassDirection::N);
        } else if (item.iconTex) {
            // Its own size where the list has a mini icon column, else small
            int iw = 18, ih = 18;
            if (minicon_width >= 30)
                ArtScaler::querySize(item.iconTex, &iw, &ih);
            SDL_Rect iconRect = {
                item_rect.x + 3,
                item_rect.y + (item_height - ih) / 2,
                iw, ih
            };
            SDL_SetTextureBlendMode(item.iconTex, SDL_BLENDMODE_BLEND);
            RenderSettings::applyArtScaleMode(item.iconTex);
            if (art)
                SDL_RenderCopy(renderer, item.iconTex, nullptr, &iconRect);
            if (minicon_width <= 0)
                text_x_offset = 24;
        }

        if (!item.cells.empty() && columns.size() >= 2) {
            float scale = dx > 0 ? cached_rect.w / static_cast<float>(dx) : 1.0f;
            for (size_t k = 0; k < item.cells.size() && k + 1 < columns.size(); k++) {
                const SDL_Color* c = fg;
                if (item_index != selected_index && k < item.cellColors.size() && item.cellColors[k].a)
                    c = &item.cellColors[k];
                SDL_Texture* t = resource_manager->getStringTexture(renderer, font_id, item.cells[k], *c, font_size_id);
                if (!t || !RenderSettings::drawsUiText())
                    continue;
                int tw = 0, th = 0;
                resource_manager->getTextSize(t, &tw, &th);
                int left = cached_rect.x + static_cast<int>(columns[k + 1] * scale);
                int right = cached_rect.x + (k + 2 < columns.size() ? static_cast<int>(columns[k + 2] * scale) : cached_rect.w);
                SDL_Rect dst = {k == 0 ? left + 1 : right - 1 - tw, item_rect.y + (item_height - th) / 2, tw, th};
                SDL_RenderCopy(renderer, t, nullptr, &dst);
            }
        } else if (!item.text.empty()) {
            SDL_Texture* text_texture = resource_manager->getStringTexture(
                renderer, font_id, item.text, item.hasColor && item_index != selected_index ? item.color : *fg, font_size_id);

            if (text_texture) {
                int tex_w, tex_h;
                resource_manager->getTextSize(text_texture, &tex_w, &tex_h);
                SDL_Rect text_rect = {
                    centered ? item_rect.x + (item_rect.w - tex_w) / 2
                             : item_rect.x + text_x_offset,
                    item_rect.y + (item_height - tex_h) / 2,
                    tex_w, tex_h
                };
                if (text_rect.w > item_rect.w - text_x_offset) {
                    text_rect.w = item_rect.w - text_x_offset;
                }
                if (RenderSettings::drawsUiText())
                    SDL_RenderCopy(renderer, text_texture, nullptr, &text_rect);
            }
        }
        item_y += item_height;
    }

    // The scrollbar is its own element (UiScrollBar), linked by the layout
    drawChildren(renderer, &cached_rect);
}