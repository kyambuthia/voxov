#pragma once

#include "engine_input/input_state.hpp"

#include <string>

struct GuiMenuActions {
    bool start_game = false;
    bool host_local = false;
    bool join_local = false;
    bool toggle_devhud = false;
    bool toggle_noclip = false;
    bool reset_camera = false;
    bool close_menu = false;
};

class GuiMenu {
public:
    void handle_input(const InputState &input, bool devhud_enabled, bool noclip_enabled, GuiMenuActions &out_actions);
    bool open() const;
    int selected() const;
    int count() const;
    std::string build_text(bool devhud_enabled, bool noclip_enabled) const;

private:
    enum class MenuPage {
        Main,
        Multiplayer,
        Settings
    };

    int item_count() const;

    bool is_open = true;
    int selected_item = 0;
    MenuPage page = MenuPage::Main;
};
