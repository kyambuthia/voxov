#pragma once

#include "engine_net/net_client.hpp"

#include <memory>
#include <thread>

// Browser adapter that keeps Sokol rendering/input on the browser main thread
// while all blocking POSIX-proxy socket work runs on a pthread.
class WebNetClient {
public:
    struct SharedState;

    WebNetClient();
    ~WebNetClient();

    WebNetClient(const WebNetClient &) = delete;
    WebNetClient &operator=(const WebNetClient &) = delete;

    bool init();
    bool connect(const char *host, uint16_t port);
    void disconnect();
    void shutdown();
    void pump();
    void send_input(const NetTickInput &input);
    void send_player_state(const NetPlayerState &state);
    void set_chunk_interest(const NetChunkInterest &interest);
    bool poll_snapshot(NetSnapshot &out_snapshot);
    bool poll_chunk_state(NetChunkState &out_state);
    uint32_t local_player_id() const;
    NetProtocolInfo protocol_info() const;
    bool has_session_info() const;
    NetSessionInfo session_info() const;
    bool is_connected() const;
    bool is_initialized() const;
    NetClientConnectionState connection_state() const;
    std::string connect_target_host() const;
    uint16_t connect_target_port() const;
    NetDebugStats debug_stats() const;
    std::unordered_map<uint32_t, NetPlayerState> player_states() const;

private:
    std::shared_ptr<SharedState> shared_;
    std::thread worker_;
};
