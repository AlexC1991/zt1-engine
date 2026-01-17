#ifndef UI_LISTBOX_HPP
#define UI_LISTBOX_HPP

#include <vector>
#include <string>
#include <SDL2/SDL.h>

#include "UiElement.hpp"
#include "../IniReader.hpp"
#include "../ResourceManager.hpp"

struct ListBoxItem {
    std::string text;
    std::string data;
    uint32_t textId = 0;
    std::string iconPath;
    SDL_Texture* iconTex = nullptr;
    bool loadAttempted = false; // [FIX] Prevents lag if file is missing
};

class UiListBox : public UiElement {
public:
    UiListBox(IniReader* ini_reader, ResourceManager* resource_manager, std::string name);
    ~UiListBox();

    UiAction handleInputs(std::vector<Input>& inputs) override;
    void draw(SDL_Renderer* renderer, SDL_Rect* layout_rect) override;

    void addItem(const std::string& text, const std::string& data = "", const std::string& icon = "");
    void addItem(uint32_t textId, const std::string& data = "", const std::string& icon = "");
    void clear();

    int getSelectedIndex() const { return selected_index; }
    std::string getSelectedData() const;
    std::string getSelectedText() const;
    size_t getItemCount() const { return items.size(); }

    void setSelectionAction(UiAction action) { selection_action = action; }

private:
    std::vector<ListBoxItem> items;
    int selected_index = -1;
    int hover_index = -1;
    int scroll_offset = 0;
    int visible_items = 10;

    int x = 0, y = 0, dx = 100, dy = 200;

    SDL_Color forecolor = {156, 205, 183, 255};
    SDL_Color backcolor = {121, 104, 50, 255};
    SDL_Color highlightcolor = {156, 205, 183, 255};
    SDL_Color selectcolor = {255, 255, 255, 255};
    SDL_Color selectbackcolor = {121, 104, 50, 255};

    int font_id = 14002;
    int item_height = 20;

    bool transparent = true;
    int border = 2;

    UiAction selection_action = UiAction::NONE;
    SDL_Rect cached_rect = {0, 0, 0, 0};

    void parseColors(IniReader* ini, const std::string& section, const std::string& key, SDL_Color& color);
    int getItemAtPoint(int px, int py);

    SDL_Texture* loadIconTexture(SDL_Renderer* renderer, const std::string& path);
};

#endif // UI_LISTBOX_HPP