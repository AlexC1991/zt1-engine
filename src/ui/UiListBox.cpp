#include "UiListBox.hpp"
#include <algorithm>

UiListBox::UiListBox(IniReader* ini_reader, ResourceManager* resource_manager, std::string name) {
    this->name = name;
    this->ini_reader = ini_reader;
    this->resource_manager = resource_manager;
    
    this->id = ini_reader->getInt(name, "id", 0);
    this->layer = ini_reader->getInt(name, "layer", 1);
    this->anchor = ini_reader->getInt(name, "anchor", 0);
    
    // Position and size
    this->x = ini_reader->getInt(name, "x", 0);
    this->y = ini_reader->getInt(name, "y", 0);
    this->dx = ini_reader->getInt(name, "dx", 100);
    this->dy = ini_reader->getInt(name, "dy", 200);
    
    // Font
    this->font_id = ini_reader->getInt(name, "font", 14002);
    
    // Parse colors (they come as 3 separate lines for R, G, B)
    parseColors(ini_reader, name, "forecolor", forecolor);
    parseColors(ini_reader, name, "backcolor", backcolor);
    parseColors(ini_reader, name, "highlightcolor", highlightcolor);
    parseColors(ini_reader, name, "selectcolor", selectcolor);
    parseColors(ini_reader, name, "selectbackcolor", selectbackcolor);
    
    // Other properties
    this->transparent = ini_reader->getInt(name, "transparent", 1) == 1;
    this->border = ini_reader->getInt(name, "border", 2);
    
    // Calculate item height based on font (approximate)
    this->item_height = 22;
    this->visible_items = (dy - border * 2) / item_height;
    
    SDL_Log("Created UiListBox: %s (id=%d, %dx%d, visible=%d items)", 
            name.c_str(), id, dx, dy, visible_items);
}

UiListBox::~UiListBox() {
    items.clear();
}

void UiListBox::parseColors(IniReader* ini, const std::string& section, const std::string& key, SDL_Color& color) {
    // Colors in Zoo Tycoon .lyt files are specified as 3 separate lines:
    // forecolor=255
    // forecolor=186
    // forecolor=16
    // We need to get all values and use them as R, G, B
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

void UiListBox::addItem(const std::string& text, const std::string& data) {
    ListBoxItem item;
    item.text = text;
    item.data = data;
    item.textId = 0;
    items.push_back(item);
}

void UiListBox::addItem(uint32_t textId, const std::string& data) {
    ListBoxItem item;
    item.textId = textId;
    item.data = data;
    // Get the actual text from resource manager
    item.text = resource_manager->getString(textId);
    if (item.text.empty()) {
        item.text = "String #" + std::to_string(textId);
    }
    items.push_back(item);
}

void UiListBox::clear() {
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
    
    if (index >= 0 && index < (int)items.size()) {
        return index;
    }
    return -1;
}

UiAction UiListBox::handleInputs(std::vector<Input>& inputs) {
    UiAction result = UiAction::NONE;
    
    for (const Input& input : inputs) {
        // Use position.x and position.y for coordinates
        int mx = input.position.x;
        int my = input.position.y;
        
        if (input.event == InputEvent::CURSOR_MOVE) {
            hover_index = getItemAtPoint(mx, my);
        }
        else if (input.event == InputEvent::LEFT_CLICK) {
            int clicked = getItemAtPoint(mx, my);
            if (clicked >= 0 && clicked != selected_index) {
                selected_index = clicked;
                SDL_Log("ListBox %s: Selected item %d: %s", 
                        name.c_str(), selected_index, 
                        items[selected_index].text.c_str());
                // Return selection action if one is set
                if (selection_action != UiAction::NONE) {
                    result = selection_action;
                }
            }
        }
        else if (input.event == InputEvent::SCROLL_UP) {
            // Only scroll if mouse is over this listbox
            if (getItemAtPoint(mx, my) >= 0 || 
                (mx >= cached_rect.x && mx <= cached_rect.x + cached_rect.w &&
                 my >= cached_rect.y && my <= cached_rect.y + cached_rect.h)) {
                if (scroll_offset > 0) scroll_offset--;
            }
        }
        else if (input.event == InputEvent::SCROLL_DOWN) {
            // Only scroll if mouse is over this listbox
            if (getItemAtPoint(mx, my) >= 0 ||
                (mx >= cached_rect.x && mx <= cached_rect.x + cached_rect.w &&
                 my >= cached_rect.y && my <= cached_rect.y + cached_rect.h)) {
                int max_scroll = std::max(0, (int)items.size() - visible_items);
                if (scroll_offset < max_scroll) scroll_offset++;
            }
        }
    }
    
    // Check children too
    UiAction childAction = handleInputChildren(inputs);
    if (childAction != UiAction::NONE) {
        result = childAction;
    }
    
    return result;
}

void UiListBox::draw(SDL_Renderer* renderer, SDL_Rect* layout_rect) {
    // Calculate our rectangle
    cached_rect.x = layout_rect->x + x;
    cached_rect.y = layout_rect->y + y;
    cached_rect.w = dx;
    cached_rect.h = dy;
    
    // Draw background (if not transparent)
    if (!transparent) {
        SDL_SetRenderDrawColor(renderer, backcolor.r, backcolor.g, backcolor.b, backcolor.a);
        SDL_RenderFillRect(renderer, &cached_rect);
    }
    
    // Draw border
    if (border > 0) {
        SDL_SetRenderDrawColor(renderer, forecolor.r, forecolor.g, forecolor.b, 255);
        SDL_RenderDrawRect(renderer, &cached_rect);
    }
    
    // Draw items
    int item_y = cached_rect.y + border;
    int max_items = std::min(visible_items, (int)items.size() - scroll_offset);
    
    for (int i = 0; i < max_items; i++) {
        int item_index = scroll_offset + i;
        if (item_index >= (int)items.size()) break;
        
        const ListBoxItem& item = items[item_index];
        
        SDL_Rect item_rect = {
            cached_rect.x + border,
            item_y,
            cached_rect.w - border * 2 - 20,  // Leave space for scrollbar
            item_height
        };
        
        // Determine colors based on state
        SDL_Color* bg = nullptr;
        SDL_Color* fg = &forecolor;
        
        if (item_index == selected_index) {
            bg = &selectbackcolor;
            fg = &selectcolor;
            
            // Draw selection background
            SDL_SetRenderDrawColor(renderer, bg->r, bg->g, bg->b, 180);
            SDL_RenderFillRect(renderer, &item_rect);
            
            // Draw selection border
            SDL_SetRenderDrawColor(renderer, 255, 217, 90, 255);
            SDL_RenderDrawRect(renderer, &item_rect);
        }
        else if (item_index == hover_index) {
            // Highlight on hover
            SDL_SetRenderDrawColor(renderer, highlightcolor.r, highlightcolor.g, highlightcolor.b, 100);
            SDL_RenderFillRect(renderer, &item_rect);
        }
        
        // Draw text
        if (!item.text.empty()) {
            SDL_Texture* text_texture = resource_manager->getStringTexture(
                renderer, font_id, item.text, *fg);
            
            if (text_texture) {
                int tex_w, tex_h;
                SDL_QueryTexture(text_texture, nullptr, nullptr, &tex_w, &tex_h);
                
                SDL_Rect text_rect = {
                    item_rect.x + 4,
                    item_rect.y + (item_height - tex_h) / 2,
                    tex_w,
                    tex_h
                };
                
                // Clip to item bounds
                if (text_rect.w > item_rect.w - 8) {
                    text_rect.w = item_rect.w - 8;
                }
                
                SDL_RenderCopy(renderer, text_texture, nullptr, &text_rect);
            }
        }
        
        item_y += item_height;
    }
    
    // Draw scroll indicator if needed
    if ((int)items.size() > visible_items) {
        int scrollbar_x = cached_rect.x + cached_rect.w - 15;
        int scrollbar_h = cached_rect.h - border * 2;
        int thumb_h = std::max(20, scrollbar_h * visible_items / (int)items.size());
        int max_scroll = items.size() - visible_items;
        int thumb_y = cached_rect.y + border;
        if (max_scroll > 0) {
            thumb_y += (scrollbar_h - thumb_h) * scroll_offset / max_scroll;
        }
        
        // Scrollbar track
        SDL_SetRenderDrawColor(renderer, 60, 50, 30, 200);
        SDL_Rect track = {scrollbar_x, cached_rect.y + border, 12, scrollbar_h};
        SDL_RenderFillRect(renderer, &track);
        
        // Scrollbar thumb
        SDL_SetRenderDrawColor(renderer, 150, 140, 100, 255);
        SDL_Rect thumb = {scrollbar_x + 1, thumb_y, 10, thumb_h};
        SDL_RenderFillRect(renderer, &thumb);
    }
    
    // Draw children
    drawChildren(renderer, &cached_rect);
}
