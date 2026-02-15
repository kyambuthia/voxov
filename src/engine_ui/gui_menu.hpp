#pragma once

#include "engine_input/input_state.hpp"

#include <string>

struct GuiMenuActions {
    bool toggle_devhud = false;
    bool toggle_noclip = false;
    bool reset_camera = false;
    bool close_menu = false;
};

class GuiMenu {
public:
    void handle_input(const InputState &input, bool devhud_enabled, bool noclip_enabled, GuiMenuActions &out_actions);
    bool open() const;
    std::string build_text(bool devhud_enabled, bool noclip_enabled) const;

private:
    int item_count() const;

    bool is_open = false;
    int selected_item = 0;
};
