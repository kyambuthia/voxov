#include "engine_ui/gui_menu.hpp"

#include <algorithm>
#include <cstdio>

int GuiMenu::item_count() const {
    switch (page) {
    case MenuPage::Main:
        return 4;
    case MenuPage::Multiplayer:
        return 5;
    case MenuPage::Settings:
        return 4;
    case MenuPage::MultiplayerGuide:
        return 1;
    default:
        return 0;
    }
}

void GuiMenu::handle_input(const InputState &input, bool devhud_enabled, bool noclip_enabled, GuiMenuActions &out_actions) {
    (void)devhud_enabled;
    (void)noclip_enabled;

    if (input.menu_toggle_pressed) {
        out_actions.ui_select_sfx = true;
        is_open = !is_open;
        if (!is_open) {
            out_actions.close_menu = true;
        } else {
            page = MenuPage::Main;
            selected_item = 0;
        }
    }

    if (!is_open) {
        return;
    }

    if (input.menu_up_pressed) {
        out_actions.ui_move_sfx = true;
        selected_item--;
        if (selected_item < 0) {
            selected_item = item_count() - 1;
        }
    }
    if (input.menu_down_pressed) {
        out_actions.ui_move_sfx = true;
        selected_item++;
        if (selected_item >= item_count()) {
            selected_item = 0;
        }
    }

    if (input.menu_select_pressed) {
        out_actions.ui_select_sfx = true;
        activate_index(selected_item, devhud_enabled, noclip_enabled, out_actions);
    }
}

void GuiMenu::activate_index(int index, bool devhud_enabled, bool noclip_enabled, GuiMenuActions &out_actions) {
    (void)devhud_enabled;
    (void)noclip_enabled;
    set_selected(index);

    if (page == MenuPage::Main) {
        switch (selected_item) {
        case 0:
            out_actions.start_game = true;
            is_open = false;
            out_actions.close_menu = true;
            break;
        case 1:
            page = MenuPage::Multiplayer;
            selected_item = 0;
            break;
        case 2:
            page = MenuPage::Settings;
            selected_item = 0;
            break;
        case 3:
            is_open = false;
            out_actions.close_menu = true;
            break;
        default:
            break;
        }
        return;
    }

    if (page == MenuPage::Multiplayer) {
        switch (selected_item) {
        case 0:
            out_actions.host_local = true;
            out_actions.start_game = true;
            is_open = false;
            out_actions.close_menu = true;
            break;
        case 1:
            out_actions.host_lan = true;
            out_actions.start_game = true;
            is_open = false;
            out_actions.close_menu = true;
            break;
        case 2:
            out_actions.join_nearby = true;
            out_actions.start_game = true;
            is_open = false;
            out_actions.close_menu = true;
            break;
        case 3:
            page = MenuPage::MultiplayerGuide;
            selected_item = 0;
            break;
        case 4:
            page = MenuPage::Main;
            selected_item = 0;
            break;
        default:
            break;
        }
        return;
    }

    if (page == MenuPage::Settings) {
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
            page = MenuPage::Main;
            selected_item = 0;
            break;
        default:
            break;
        }
        return;
    }

    if (page == MenuPage::MultiplayerGuide) {
        page = MenuPage::Multiplayer;
        selected_item = 0;
    }
}

bool GuiMenu::open() const {
    return is_open;
}

GuiMenu::Page GuiMenu::page_id() const {
    switch (page) {
    case MenuPage::Main: return Page::Main;
    case MenuPage::Multiplayer: return Page::Multiplayer;
    case MenuPage::Settings: return Page::Settings;
    case MenuPage::MultiplayerGuide: return Page::MultiplayerGuide;
    default: return Page::Main;
    }
}

int GuiMenu::selected() const {
    return selected_item;
}

int GuiMenu::count() const {
    return item_count();
}

void GuiMenu::set_selected(int index) {
    const int n = item_count();
    if (n <= 0) {
        selected_item = 0;
        return;
    }
    selected_item = std::clamp(index, 0, n - 1);
}

std::string GuiMenu::build_text(bool devhud_enabled, bool noclip_enabled, const std::string &multiplayer_hint) const {
    if (!is_open) {
        return std::string();
    }

    char buffer[768]{};
    if (page == MenuPage::Main) {
        std::snprintf(
            buffer,
            sizeof(buffer),
            "VOXOV\n\n%s START GAME\n%s MULTIPLAYER\n%s SETTINGS\n%s CLOSE MENU\n\nUP/DOWN + ENTER | ESC",
            selected_item == 0 ? ">" : " ",
            selected_item == 1 ? ">" : " ",
            selected_item == 2 ? ">" : " ",
            selected_item == 3 ? ">" : " ");
        return std::string(buffer);
    }

    if (page == MenuPage::Multiplayer) {
        std::snprintf(
            buffer,
            sizeof(buffer),
            "MULTIPLAYER\n\n%s HOST THIS DEVICE\n%s HOST WI-FI GAME (INVITE)\n%s JOIN NEARBY WI-FI GAME\n%s HOW HOST/JOIN/INVITE WORKS\n%s BACK\n\nUP/DOWN + ENTER | ESC",
            selected_item == 0 ? ">" : " ",
            selected_item == 1 ? ">" : " ",
            selected_item == 2 ? ">" : " ",
            selected_item == 3 ? ">" : " ",
            selected_item == 4 ? ">" : " ");
        std::string out(buffer);
        if (!multiplayer_hint.empty()) {
            out += "\n\nSTATUS: ";
            out += multiplayer_hint;
        }
        return out;
    }

    if (page == MenuPage::MultiplayerGuide) {
        std::snprintf(
            buffer,
            sizeof(buffer),
            "HOST / JOIN / INVITE\n\n1. HOST WI-FI GAME to start a LAN session.\n2. Friends on same Wi-Fi tap JOIN NEARBY.\n3. INVITE TEXT: \"Open VOXOV > Multiplayer > Join Nearby\"\n4. If no host appears, ensure same Wi-Fi and retry.\n\nSELECT TO GO BACK");
        std::string out(buffer);
        if (!multiplayer_hint.empty()) {
            out += "\n\nSTATUS: ";
            out += multiplayer_hint;
        }
        return out;
    }

    std::snprintf(
        buffer,
        sizeof(buffer),
        "SETTINGS\n\n%s DEVHUD: %s\n%s NOCLIP: %s\n%s RESET CAMERA\n%s BACK\n\nUP/DOWN + ENTER | ESC",
        selected_item == 0 ? ">" : " ",
        devhud_enabled ? "ON" : "OFF",
        selected_item == 1 ? ">" : " ",
        noclip_enabled ? "ON" : "OFF",
        selected_item == 2 ? ">" : " ",
        selected_item == 3 ? ">" : " ");
    return std::string(buffer);
}

std::string GuiMenu::page_title() const {
    switch (page) {
    case MenuPage::Main:
        return "MAIN MENU";
    case MenuPage::Multiplayer:
        return "MULTIPLAYER";
    case MenuPage::Settings:
        return "SETTINGS";
    case MenuPage::MultiplayerGuide:
        return "HOST / JOIN GUIDE";
    default:
        return "MENU";
    }
}

std::string GuiMenu::item_label(int index, bool devhud_enabled, bool noclip_enabled) const {
    switch (page) {
    case MenuPage::Main:
        switch (index) {
        case 0: return "START GAME";
        case 1: return "MULTIPLAYER";
        case 2: return "SETTINGS";
        case 3: return "CLOSE MENU";
        default: return std::string();
        }
    case MenuPage::Multiplayer:
        switch (index) {
        case 0: return "HOST THIS DEVICE";
        case 1: return "HOST WI-FI GAME";
        case 2: return "JOIN NEARBY WI-FI";
        case 3: return "HOW HOST/JOIN WORKS";
        case 4: return "BACK";
        default: return std::string();
        }
    case MenuPage::Settings:
        switch (index) {
        case 0: return std::string("DEVHUD: ") + (devhud_enabled ? "ON" : "OFF");
        case 1: return std::string("NOCLIP: ") + (noclip_enabled ? "ON" : "OFF");
        case 2: return "RESET CAMERA";
        case 3: return "BACK";
        default: return std::string();
        }
    case MenuPage::MultiplayerGuide:
        if (index == 0) {
            return "BACK";
        }
        return std::string();
    default:
        return std::string();
    }
}

std::vector<std::string> GuiMenu::guide_lines() const {
    if (page != MenuPage::MultiplayerGuide) {
        return {};
    }

    return {
        "1 HOST WI-FI GAME",
        "2 FRIEND TAPS JOIN NEARBY",
        "3 SAME WI-FI REQUIRED",
        "4 RETRY IF HOST NOT LISTED"};
}
