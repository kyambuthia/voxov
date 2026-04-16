#pragma once

#include "engine/engine.hpp"
#include "engine_runtime/runtime_session_controller.hpp"

#include <string>

class RuntimeSessionFlow {
public:
    void connect(Engine &engine, const char *host, uint16_t port);
    void leave_session(Engine &engine);
    void host_local_session(Engine &engine);
    void host_lan_session(Engine &engine);
    void join_nearby_session(Engine &engine);
    void update(Engine &engine, double frame_dt);

    RuntimeSessionSnapshot build_snapshot(
        const RuntimeSessionSnapshot &engine_snapshot) const;

private:
    bool searching_nearby_ = false;
    std::string multiplayer_hint_;
    double net_connect_elapsed_ = 0.0;
    NetClientConnectionState last_connection_state_ =
        NetClientConnectionState::Disconnected;
};
