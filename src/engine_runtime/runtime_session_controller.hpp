#pragma once

#include "engine_input/input_state.hpp"
#include "engine_net/net_client.hpp"
#include "engine_net_proto/net_protocol_helpers.hpp"
#include "engine_ui/gui_menu.hpp"

#include <cstdint>
#include <functional>
#include <string>

struct RuntimeSessionSnapshot {
    NetClientConnectionState connection_state = NetClientConnectionState::Disconnected;
    bool searching_nearby = false;
    bool hosting_local = false;
    bool hosting_lan = false;
    bool has_session_info = false;
    NetSessionInfo session_info{};
    std::string connect_target_host;
    uint16_t connect_target_port = 0;
    std::string status_hint;
};

struct RuntimeSessionMenuCallbacks {
    std::function<void()> leave_session;
    std::function<void()> host_local;
    std::function<void()> host_lan;
    std::function<void()> join_nearby;
};

struct RuntimeMenuResult {
    bool ui_move_sfx = false;
    bool ui_select_sfx = false;
    bool reset_camera_requested = false;
};

class RuntimeSessionController {
public:
    void set_devhud_enabled(bool enabled);
    void set_noclip_enabled(bool enabled);
    void set_gameplay_started(bool enabled);

    bool devhud_enabled() const;
    bool noclip_enabled() const;
    bool gameplay_started() const;

    RuntimeMenuResult handle_menu_input(
        const InputState &input,
        GuiMenu &menu,
        const RuntimeSessionMenuCallbacks &callbacks);

    GuiSessionContext build_session_context(
        const RuntimeSessionSnapshot &snapshot) const;
    std::string build_multiplayer_status_text(
        const RuntimeSessionSnapshot &snapshot) const;

private:
    void apply_menu_actions(
        const GuiMenuActions &actions,
        const RuntimeSessionMenuCallbacks &callbacks,
        RuntimeMenuResult &out_result);

    bool devhud_enabled_ = false;
    bool noclip_enabled_ = false;
    bool gameplay_started_ = true;
};
