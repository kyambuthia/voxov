#include "engine_net/web_net_client.hpp"

#if defined(VOXOV_PLATFORM_WEB)

#include <emscripten/em_asm.h>
#include <emscripten/posix_socket.h>
#include <emscripten/threading.h>
#include <emscripten/websocket.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <optional>
#include <string>

struct WebNetClient::SharedState {
    mutable std::mutex mutex;
    std::atomic<bool> stop{false};
    bool initialized = false;
    std::optional<std::pair<std::string, uint16_t>> connect_request;
    bool disconnect_requested = false;
    std::optional<NetTickInput> pending_input;
    std::optional<NetPlayerState> pending_player_state;
    std::optional<NetChunkInterest> pending_interest;
    std::optional<NetSnapshot> snapshot;
    std::deque<NetChunkState> chunk_updates;
    NetClientConnectionState connection_state =
        NetClientConnectionState::Disconnected;
    uint32_t local_player_id = 0;
    NetProtocolInfo protocol{};
    bool has_session_info = false;
    NetSessionInfo session_info{};
    std::string target_host;
    uint16_t target_port = 0;
    NetDebugStats debug{};
    std::unordered_map<uint32_t, NetPlayerState> players;
};

namespace {
std::string browser_proxy_url() {
    char buffer[512]{};
    MAIN_THREAD_EM_ASM({
        const fallbackScheme = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
        // Keep the two slash tokens separate: this JavaScript is embedded in a
        // C macro, where a literal URL would otherwise start a C++ comment.
        const fallback = fallbackScheme + '/' + '/' +
            (window.location.hostname || '127.0.0.1') + ':8080';
        const value = Module.websocket?.url ||
            new URLSearchParams(window.location.search).get('proxy') || fallback;
        stringToUTF8(value, $0, $1);
    }, buffer, sizeof(buffer));
    return std::string(buffer);
}

void web_network_worker(const std::shared_ptr<WebNetClient::SharedState> &shared,
                        const std::string &proxy_url) {
    const EMSCRIPTEN_WEBSOCKET_T bridge =
        emscripten_init_websocket_to_posix_socket_bridge(proxy_url.c_str());
    uint16_t ready_state = 0;
    while (!shared->stop.load() && ready_state == 0) {
        emscripten_websocket_get_ready_state(bridge, &ready_state);
        emscripten_thread_sleep(10);
    }
    if (shared->stop.load() || ready_state != 1) {
        std::lock_guard lock(shared->mutex);
        shared->initialized = false;
        shared->connection_state = NetClientConnectionState::Disconnected;
        return;
    }

    NetClient transport;
    if (!transport.init()) {
        std::lock_guard lock(shared->mutex);
        shared->initialized = false;
        return;
    }

    while (!shared->stop.load()) {
        std::optional<std::pair<std::string, uint16_t>> connect_request;
        std::optional<NetTickInput> input;
        std::optional<NetPlayerState> player_state;
        std::optional<NetChunkInterest> interest;
        bool disconnect_requested = false;
        {
            std::lock_guard lock(shared->mutex);
            connect_request = std::move(shared->connect_request);
            shared->connect_request.reset();
            disconnect_requested = shared->disconnect_requested;
            shared->disconnect_requested = false;
            input = shared->pending_input;
            shared->pending_input.reset();
            player_state = shared->pending_player_state;
            shared->pending_player_state.reset();
            interest = shared->pending_interest;
            shared->pending_interest.reset();
        }

        if (disconnect_requested) {
            transport.disconnect();
        }
        if (connect_request.has_value()) {
            (void)transport.connect(connect_request->first.c_str(),
                                    connect_request->second);
        }
        if (interest.has_value()) {
            transport.set_chunk_interest(*interest);
        }
        if (input.has_value()) {
            transport.send_input(*input);
        }
        if (player_state.has_value()) {
            transport.send_player_state(*player_state);
        }
        transport.pump();

        std::optional<NetSnapshot> snapshot;
        NetSnapshot next_snapshot{};
        while (transport.poll_snapshot(next_snapshot)) {
            snapshot = next_snapshot;
        }
        std::deque<NetChunkState> chunks;
        NetChunkState chunk{};
        while (transport.poll_chunk_state(chunk)) {
            chunks.push_back(chunk);
        }

        {
            std::lock_guard lock(shared->mutex);
            shared->connection_state = transport.connection_state();
            shared->local_player_id = transport.local_player_id();
            shared->protocol = transport.protocol_info();
            shared->has_session_info = transport.has_session_info();
            shared->session_info = transport.session_info();
            shared->target_host = transport.connect_target_host();
            shared->target_port = transport.connect_target_port();
            shared->debug = transport.debug_stats();
            shared->players = transport.player_states();
            if (snapshot.has_value()) {
                shared->snapshot = snapshot;
            }
            while (!chunks.empty()) {
                shared->chunk_updates.push_back(chunks.front());
                chunks.pop_front();
            }
        }
        emscripten_thread_sleep(2);
    }

    transport.disconnect();
    transport.shutdown();
    emscripten_websocket_close(bridge, 1000, nullptr);
    emscripten_websocket_delete(bridge);
}
} // namespace

WebNetClient::WebNetClient() : shared_(std::make_shared<SharedState>()) {}

WebNetClient::~WebNetClient() { shutdown(); }

bool WebNetClient::init() {
    std::lock_guard lock(shared_->mutex);
    if (shared_->initialized) {
        return true;
    }
    shared_->initialized = true;
    shared_->stop.store(false);
    const std::string proxy_url = browser_proxy_url();
    worker_ = std::thread(web_network_worker, shared_, proxy_url);
    return true;
}

bool WebNetClient::connect(const char *host, uint16_t port) {
    if (!host || host[0] == '\0' || port == 0) {
        return false;
    }
    std::lock_guard lock(shared_->mutex);
    shared_->connect_request = std::make_pair(std::string(host), port);
    shared_->connection_state = NetClientConnectionState::Connecting;
    shared_->target_host = host;
    shared_->target_port = port;
    return true;
}

void WebNetClient::disconnect() {
    std::lock_guard lock(shared_->mutex);
    shared_->disconnect_requested = true;
    shared_->connection_state = NetClientConnectionState::Disconnected;
    shared_->local_player_id = 0;
    shared_->players.clear();
    shared_->target_host.clear();
    shared_->target_port = 0;
}

void WebNetClient::shutdown() {
    if (!shared_) {
        return;
    }
    shared_->stop.store(true);
    if (worker_.joinable()) {
        // Browser cleanup may run while the main thread owns WebSocket event
        // dispatch. Detaching lets the shared worker state outlive this facade
        // and avoids a main-thread futex deadlock during page shutdown.
        worker_.detach();
    }
    std::lock_guard lock(shared_->mutex);
    shared_->initialized = false;
}

void WebNetClient::pump() {}

void WebNetClient::send_input(const NetTickInput &input) {
    std::lock_guard lock(shared_->mutex);
    shared_->pending_input = input;
}

void WebNetClient::send_player_state(const NetPlayerState &state) {
    std::lock_guard lock(shared_->mutex);
    shared_->pending_player_state = state;
}

void WebNetClient::set_chunk_interest(const NetChunkInterest &interest) {
    std::lock_guard lock(shared_->mutex);
    shared_->pending_interest = interest;
}

bool WebNetClient::poll_snapshot(NetSnapshot &out_snapshot) {
    std::lock_guard lock(shared_->mutex);
    if (!shared_->snapshot.has_value()) {
        return false;
    }
    out_snapshot = *shared_->snapshot;
    shared_->snapshot.reset();
    return true;
}

bool WebNetClient::poll_chunk_state(NetChunkState &out_state) {
    std::lock_guard lock(shared_->mutex);
    if (shared_->chunk_updates.empty()) {
        return false;
    }
    out_state = shared_->chunk_updates.front();
    shared_->chunk_updates.pop_front();
    return true;
}

uint32_t WebNetClient::local_player_id() const {
    std::lock_guard lock(shared_->mutex);
    return shared_->local_player_id;
}

NetProtocolInfo WebNetClient::protocol_info() const {
    std::lock_guard lock(shared_->mutex);
    return shared_->protocol;
}

bool WebNetClient::has_session_info() const {
    std::lock_guard lock(shared_->mutex);
    return shared_->has_session_info;
}

NetSessionInfo WebNetClient::session_info() const {
    std::lock_guard lock(shared_->mutex);
    return shared_->session_info;
}

bool WebNetClient::is_connected() const {
    return connection_state() == NetClientConnectionState::Connected;
}

bool WebNetClient::is_initialized() const {
    std::lock_guard lock(shared_->mutex);
    return shared_->initialized;
}

NetClientConnectionState WebNetClient::connection_state() const {
    std::lock_guard lock(shared_->mutex);
    return shared_->connection_state;
}

std::string WebNetClient::connect_target_host() const {
    std::lock_guard lock(shared_->mutex);
    return shared_->target_host;
}

uint16_t WebNetClient::connect_target_port() const {
    std::lock_guard lock(shared_->mutex);
    return shared_->target_port;
}

NetDebugStats WebNetClient::debug_stats() const {
    std::lock_guard lock(shared_->mutex);
    return shared_->debug;
}

std::unordered_map<uint32_t, NetPlayerState> WebNetClient::player_states() const {
    std::lock_guard lock(shared_->mutex);
    return shared_->players;
}

#endif
