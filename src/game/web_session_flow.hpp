#pragma once

#include "engine_runtime/runtime_session_controller.hpp"

#include <string>

class WebSessionFlow {
public:
    void leave_session();
    void host_local_session();
    void host_lan_session();
    void join_nearby_session();
    void update(bool net_available);

    RuntimeSessionSnapshot snapshot() const;

private:
    bool hosting_local_ = false;
    bool hosting_lan_ = false;
    bool joined_ = false;
    bool searching_nearby_ = false;
    std::string status_hint_;
};
