#include "engine_ui/gui_menu.hpp"

#include <algorithm>
#include <cstdio>

int GuiMenu::item_count() const {
    return 4;
}

void GuiMenu::handle_input(const InputState &input, bool devhud_enabled, bool noclip_enabled, GuiMenuActions &out_actions) {
    (void)devhud_enabled;
    (void)noclip_enabled;

    if (input.menu_toggle_pressed) {
        is_open = !is_open;
        if (!is_open) {
            out_actions.close_menu = true;
        }
    }

    if (!is_open) {
        return;
    }

    if (input.menu_up_pressed) {
        selected_item--;
        if (selected_item < 0) {
            selected_item = item_count() - 1;
        }
    }
    if (input.menu_down_pressed) {
        selected_item++;
        if (selected_item >= item_count()) {
            selected_item = 0;
        }
    }

    if (!input.menu_select_pressed) {
        return;
    }

    switch (selected_item) {
    case 0:
        out_actions.toggle_devhud = true;
        break;
    case 1:
        out_actions.toggle_noclip = true;
        break;
    case 2:
        out_actions.reset_camera = true;
        break;
    case 3:
        is_open = false;
        out_actions.close_menu = true;
        break;
    default:
        break;
    }
}

bool GuiMenu::open() const {
    return is_open;
}

std::string GuiMenu::build_text(bool devhud_enabled, bool noclip_enabled) const {
    if (!is_open) {
        return std::string();
    }

    char buffer[512]{};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "MENU\n%s DEVHUD: %s\n%s NOCLIP: %s\n%s RESET CAMERA\n%s CLOSE\n\nARROWS/W,S + ENTER | ESC",
        selected_item == 0 ? ">" : " ",
        devhud_enabled ? "ON" : "OFF",
        selected_item == 1 ? ">" : " ",
        noclip_enabled ? "ON" : "OFF",
        selected_item == 2 ? ">" : " ",
        selected_item == 3 ? ">" : " ");
    return std::string(buffer);
}
