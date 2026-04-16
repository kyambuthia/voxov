#include "game/web_session_flow.hpp"

void WebSessionFlow::leave_session() {
    hosting_local_ = false;
    hosting_lan_ = false;
    joined_ = false;
    searching_nearby_ = false;
    status_hint_ = "Left session.";
}

void WebSessionFlow::host_local_session() {
    hosting_local_ = true;
    hosting_lan_ = false;
    joined_ = true;
    searching_nearby_ = false;
    status_hint_ = "Web host active (local).";
}

void WebSessionFlow::host_lan_session() {
    hosting_local_ = false;
    hosting_lan_ = true;
    joined_ = true;
    searching_nearby_ = false;
    status_hint_ = "Web host active.";
}

void WebSessionFlow::join_nearby_session() {
    hosting_local_ = false;
    hosting_lan_ = false;
    joined_ = true;
    searching_nearby_ = true;
    status_hint_ = "Searching nearby web hosts...";
}

void WebSessionFlow::update(bool net_available) {
    if (!net_available) {
        status_hint_ = "Web net API not attached. Running local sim only.";
        searching_nearby_ = false;
        return;
    }

    if (searching_nearby_) {
        status_hint_ = "Joining nearby web host...";
        searching_nearby_ = false;
        return;
    }

    if (hosting_local_ || hosting_lan_) {
        status_hint_ = "Web host active.";
    } else if (joined_) {
        status_hint_ = "Connected to web host.";
    } else {
        status_hint_ = "Select Host/Join to use web transport hooks.";
    }
}

RuntimeSessionSnapshot WebSessionFlow::snapshot() const {
    RuntimeSessionSnapshot snapshot{};
    snapshot.connection_state =
        joined_ && !hosting_local_ && !hosting_lan_
            ? NetClientConnectionState::Connected
            : NetClientConnectionState::Disconnected;
    snapshot.searching_nearby = searching_nearby_;
    snapshot.hosting_local = hosting_local_;
    snapshot.hosting_lan = hosting_lan_;
    snapshot.status_hint = status_hint_;
    return snapshot;
}
