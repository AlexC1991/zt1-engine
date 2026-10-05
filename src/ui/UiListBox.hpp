#ifndef UI_LISTBOX_HPP
#define UI_LISTBOX_HPP

#include <vector>
#include <string>
#include <SDL2/SDL.h>

#include "UiElement.hpp"
#include "UiScrollBar.hpp"
#include "../IniReader.hpp"
#include "../ResourceManager.hpp"

struct ListBoxItem {
    std::string text;
    std::string data;
    uint32_t textId = 0;
    std::string iconPath;
    SDL_Texture* iconTex = nullptr;
    bool loadAttempted = false; // [FIX] Prevents lag if file is missing
    bool checked = true;        // toggleselect lists: the item's checkbox
    std::vector<std::string> lines; // toggleselect lists: wrapped text
    bool hasColor = false;          // its own text colour (a warning in red)
    SDL_Color color = {0, 0, 0, 255};
    // Column lists: its cells, each with its colour (alpha 0: the list's)
    std::vector<std::string> cells;
    std::vector<SDL_Color> cellColors;
    Animation* iconArt = nullptr; // an icon drawn from art (a guest's, in its colours)
};

class Animation;

class UiListBox : public UiElement, public UiScrollable {
public:
    UiListBox(IniReader* ini_reader, ResourceManager* resource_manager, std::string name);
    ~UiListBox();

    UiAction handleInputs(std::vector<Input>& inputs) override;
    void draw(SDL_Renderer* renderer, SDL_Rect* layout_rect) override;

    void addItem(const std::string& text, const std::string& data = "", const std::string& icon = "");
    void addItem(uint32_t textId, const std::string& data = "", const std::string& icon = "");
    void clear();
    // Paragraph lists (the Zookeeper Recommendations): each item wrapped
    // to the width, the layout's spacing= between them, in its own colour
    void setWrapped(bool on) { wrapped = on; }
    void addItem(const std::string& text, SDL_Color color);
    // Lists with column= offsets (the Commerce Building List): a row of
    // cells, the first from its column's left, the rest right-aligned to
    // the next column (the last to the list's edge), as measured
    void addRow(const std::vector<std::string>& cells, const std::vector<SDL_Color>& colors = {});
    // An item whose mini icon is art (not owned)
    void addItem(const std::string& text, Animation* iconArt);
    // An item's own text colour (the Message List's red news)
    void setItemColor(int index, SDL_Color color) {
        if (index >= 0 && index < (int)items.size()) { items[index].hasColor = true; items[index].color = color; }
    }

    int getSelectedIndex() const { return selected_index; }
    void setSelectedIndex(int index) { selected_index = index; }
    std::string getSelectedData() const;
    std::string getSelectedText() const;
    size_t getItemCount() const { return items.size(); }

    void setSelectionAction(UiAction action) { selection_action = action; }

    // toggleselect=1 lists (the research categories): every item has a
    // checkbox, drawn in the miniconwidth column; clicking toggles it
    bool isChecked(int index) const {
        return index >= 0 && index < (int)items.size() && items[index].checked;
    }
    void setChecked(int index, bool on) {
        if (index >= 0 && index < (int)items.size()) items[index].checked = on;
    }

    // Id of the UIScrollBar this list uses ("scrollbar=" in the layout)
    int getScrollBarId() const { return scrollbar_id; }

    // UiScrollable
    int getScrollPosition() const override { return scroll_offset; }
    int getScrollMaximum() const override;
    int getScrollPage() const override {
        return toggle_select || wrapped ? fittingFrom(scroll_offset) : visible_items;
    }
    void setScrollPosition(int position) override;
    SDL_Rect getScrollBounds() const override { return cached_rect; }

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
    int font_size_id = 0;
    int item_height = 16;
    int scrollbar_id = 0;

    bool transparent = true;
    int border = 2;

    bool centered = false; // justify=center (the content filter's list)
    bool toggle_select = false;
    bool wrapped = false;
    int spacing = 0;
    int minicon_width = 0;
    std::vector<int> columns; // column= (the first is the icon's)
    Animation* checkbox = nullptr;
    int line_height = 16;
    int itemHeight(const ListBoxItem& item) const;
    int fittingFrom(int first) const; // items that fit from first on
    void wrapItems(SDL_Renderer* renderer);

    UiAction selection_action = UiAction::NONE;
    SDL_Rect cached_rect = {0, 0, 0, 0};

    void parseColors(IniReader* ini, const std::string& section, const std::string& key, SDL_Color& color);
    int getItemAtPoint(int px, int py);

    SDL_Texture* loadIconTexture(SDL_Renderer* renderer, const std::string& path);
};

#endif // UI_LISTBOX_HPP