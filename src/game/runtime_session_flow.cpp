#include "game/runtime_session_flow.hpp"

#include <string>

namespace {
constexpr double k_connect_timeout_seconds = 5.0;
}

void RuntimeSessionFlow::connect(Engine &engine, const char *host, uint16_t port) {
    const EngineConnectResult result = engine.connect(host, port);
    if (result == EngineConnectResult::Connected) {
        net_connect_elapsed_ = 0.0;
        multiplayer_hint_ = std::string("Connecting to ") + (host ? host : "server") +
            ":" + std::to_string(port) + "...";
        return;
    }
    if (result == EngineConnectResult::NetworkInitFailed) {
        multiplayer_hint_ = "Network init failed.";
    } else {
        multiplayer_hint_ = "Connect failed.";
    }
}

void RuntimeSessionFlow::leave_session(Engine &engine) {
    engine.leave_session();
    searching_nearby_ = false;
    net_connect_elapsed_ = 0.0;
    multiplayer_hint_ = "Left session.";
}

void RuntimeSessionFlow::host_local_session(Engine &engine) {
    engine.host_local_session();
    searching_nearby_ = false;
    net_connect_elapsed_ = 0.0;
    multiplayer_hint_ = "Hosting this device only.";
}

void RuntimeSessionFlow::host_lan_session(Engine &engine) {
    engine.host_lan_session();
    searching_nearby_ = false;
    net_connect_elapsed_ = 0.0;
    multiplayer_hint_ = "Hosting Wi-Fi game. Tell friends: Multiplayer > Join Nearby.";
}

void RuntimeSessionFlow::join_nearby_session(Engine &engine) {
    engine.join_nearby_session();
    searching_nearby_ = true;
    net_connect_elapsed_ = 0.0;
    multiplayer_hint_ = "Searching nearby Wi-Fi hosts...";
}

void RuntimeSessionFlow::update(Engine &engine, double frame_dt) {
    const RuntimeSessionSnapshot snapshot = engine.session_snapshot();
    const NetClientConnectionState connection_state = snapshot.connection_state;
    if (snapshot.hosting_lan) {
        engine.pump_lan_discovery();
    }

    if (connection_state == NetClientConnectionState::Connecting) {
        net_connect_elapsed_ += frame_dt;
        if (net_connect_elapsed_ >= k_connect_timeout_seconds) {
            engine.abort_client_session();
            multiplayer_hint_ = "Connection timed out.";
        }
    } else {
        net_connect_elapsed_ = 0.0;
    }

    if (connection_state != last_connection_state_) {
        if (connection_state == NetClientConnectionState::Connected) {
            multiplayer_hint_.clear();
        } else if (last_connection_state_ == NetClientConnectionState::Connected &&
                   multiplayer_hint_ != "Left session.") {
            multiplayer_hint_ = "Disconnected from server.";
        }
        last_connection_state_ = connection_state;
    }

    if (searching_nearby_ &&
        connection_state == NetClientConnectionState::Disconnected) {
        engine.pump_lan_discovery();
        LanHostEntry host{};
        if (engine.pop_discovered_host(host)) {
            const EngineConnectResult connect_result =
                engine.connect(host.ip.c_str(), host.port);
            if (connect_result == EngineConnectResult::Connected) {
                searching_nearby_ = false;
                net_connect_elapsed_ = 0.0;
                multiplayer_hint_ = "Joining " + host.name + " (" + host.ip + ")";
            } else {
                multiplayer_hint_ = "Join failed. Retrying discovery...";
            }
        }
    } else if (searching_nearby_ &&
               connection_state == NetClientConnectionState::Connected) {
        searching_nearby_ = false;
    }
}

RuntimeSessionSnapshot RuntimeSessionFlow::build_snapshot(
    const RuntimeSessionSnapshot &engine_snapshot) const {
    RuntimeSessionSnapshot snapshot = engine_snapshot;
    snapshot.searching_nearby = searching_nearby_;
    snapshot.status_hint = multiplayer_hint_;
    return snapshot;
}
