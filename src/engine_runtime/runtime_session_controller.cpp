#include "engine_runtime/runtime_session_controller.hpp"

void RuntimeSessionController::set_devhud_enabled(bool enabled) {
    devhud_enabled_ = enabled;
}

void RuntimeSessionController::set_noclip_enabled(bool enabled) {
    noclip_enabled_ = enabled;
}

void RuntimeSessionController::set_gameplay_started(bool enabled) {
    gameplay_started_ = enabled;
}

bool RuntimeSessionController::devhud_enabled() const {
    return devhud_enabled_;
}

bool RuntimeSessionController::noclip_enabled() const {
    return noclip_enabled_;
}

bool RuntimeSessionController::gameplay_started() const {
    return gameplay_started_;
}

RuntimeMenuResult RuntimeSessionController::handle_menu_input(
    const InputState &input,
    GuiMenu &menu,
    const RuntimeSessionMenuCallbacks &callbacks) {
    GuiMenuActions actions{};
    menu.handle_input(input, devhud_enabled_, noclip_enabled_, actions);

    RuntimeMenuResult result{};
    result.ui_move_sfx = actions.ui_move_sfx;
    result.ui_select_sfx = actions.ui_select_sfx;

    apply_menu_actions(actions, callbacks, result);
    return result;
}

GuiSessionContext RuntimeSessionController::build_session_context(
    const RuntimeSessionSnapshot &snapshot) const {
    GuiSessionContext session{};
    session.connected = snapshot.connection_state == NetClientConnectionState::Connected;
    session.connecting = snapshot.connection_state == NetClientConnectionState::Connecting;
    session.searching = snapshot.searching_nearby;
    session.hosting_local = snapshot.hosting_local;
    session.hosting_lan = snapshot.hosting_lan;
    session.can_leave = session.connected || session.connecting ||
        session.searching || session.hosting_local || session.hosting_lan;
    session.status = build_multiplayer_status_text(snapshot);
    return session;
}

std::string RuntimeSessionController::build_multiplayer_status_text(
    const RuntimeSessionSnapshot &snapshot) const {
    if (snapshot.connection_state == NetClientConnectionState::Connected) {
        if (snapshot.has_session_info) {
            return net_session_status_line(snapshot.session_info);
        }
        return "Connected to game server.";
    }

    if (snapshot.searching_nearby) {
        return "Searching nearby Wi-Fi hosts...";
    }

    if (snapshot.connection_state == NetClientConnectionState::Connecting) {
        if (!snapshot.connect_target_host.empty()) {
            return "Connecting to " + snapshot.connect_target_host + ":" +
                std::to_string(snapshot.connect_target_port) + "...";
        }
        return "Connecting...";
    }

    if (snapshot.hosting_local) {
        return "Hosting this device only.";
    }

    if (snapshot.hosting_lan) {
        return "Hosting Wi-Fi game.";
    }

    return snapshot.status_hint;
}

void RuntimeSessionController::apply_menu_actions(
    const GuiMenuActions &actions,
    const RuntimeSessionMenuCallbacks &callbacks,
    RuntimeMenuResult &out_result) {
    if (actions.start_game) {
        gameplay_started_ = true;
    }
    if (actions.close_menu && !gameplay_started_) {
        gameplay_started_ = true;
    }
    if (actions.leave_session && callbacks.leave_session) {
        callbacks.leave_session();
    }
    if (actions.host_local && callbacks.host_local) {
        gameplay_started_ = true;
        callbacks.host_local();
    }
    if (actions.host_lan && callbacks.host_lan) {
        gameplay_started_ = true;
        callbacks.host_lan();
    }
    if ((actions.join_local || actions.join_nearby) && callbacks.join_nearby) {
        gameplay_started_ = true;
        callbacks.join_nearby();
    }
    if (actions.toggle_devhud) {
        devhud_enabled_ = !devhud_enabled_;
    }
    if (actions.toggle_noclip) {
        noclip_enabled_ = !noclip_enabled_;
    }
    if (actions.reset_camera) {
        out_result.reset_camera_requested = true;
    }
}
