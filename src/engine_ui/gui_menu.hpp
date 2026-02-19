#pragma once

#include "engine_input/input_state.hpp"

#include <vector>
#include <string>

struct GuiMenuActions {
    bool start_game = false;
    bool host_local = false;
    bool host_lan = false;
    bool join_local = false;
    bool join_nearby = false;
    bool toggle_devhud = false;
    bool toggle_noclip = false;
    bool reset_camera = false;
    bool close_menu = false;
    bool ui_move_sfx = false;
    bool ui_select_sfx = false;
};

class GuiMenu {
public:
    enum class Page {
        Main = 0,
        Multiplayer = 1,
        Settings = 2,
        MultiplayerGuide = 3
    };

    void handle_input(const InputState &input, bool devhud_enabled, bool noclip_enabled, GuiMenuActions &out_actions);
    void activate_index(int index, bool devhud_enabled, bool noclip_enabled, GuiMenuActions &out_actions);
    bool open() const;
    int selected() const;
    int count() const;
    Page page_id() const;
    void set_selected(int index);
    std::string build_text(bool devhud_enabled, bool noclip_enabled, const std::string &multiplayer_hint = std::string()) const;
    std::string page_title() const;
    std::string item_label(int index, bool devhud_enabled, bool noclip_enabled) const;
    std::vector<std::string> guide_lines() const;

private:
    enum class MenuPage {
        Main,
        Multiplayer,
        Settings,
        MultiplayerGuide
    };

    int item_count() const;

    bool is_open = true;
    int selected_item = 0;
    MenuPage page = MenuPage::Main;
};
